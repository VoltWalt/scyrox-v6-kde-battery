#include "scyroxdevice.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QDateTime>
#include <QElapsedTimer>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
#include <string.h>
#include <cstdio>
#include <algorithm>

const QVector<int> ScyroxDevice::VOLTAGE_THRESHOLDS = {
    3050, 3420, 3480, 3540, 3600, 3660, 3720, 3760, 3800, 3840,
    3880, 3920, 3940, 3960, 3980, 4000, 4020, 4040, 4060, 4080, 4110
};

namespace {

// Walk a HID report descriptor as a stream of items and collect the Report IDs
// it declares. Scanning raw bytes for 0x85 0x08 would also match payload data
// that happens to sit inside another item, which can select the wrong
// interface (the dongle exposes three, and only one speaks this protocol).
QSet<int> hidReportIds(const QByteArray &descriptor)
{
    QSet<int> ids;
    const int n = descriptor.size();
    int i = 0;
    while (i < n) {
        const quint8 prefix = quint8(descriptor.at(i));
        if (prefix == 0xFE) {                     // long item
            if (i + 2 >= n)
                break;
            i += 3 + int(quint8(descriptor.at(i + 1)));
            continue;
        }
        int size = prefix & 0x03;
        if (size == 3)
            size = 4;
        // Report ID item: bType=0b10 (global) => 0x84 with the size bits masked
        if ((prefix & 0xFC) == 0x84 && size >= 1 && i + 1 < n)
            ids.insert(int(quint8(descriptor.at(i + 1))));
        i += 1 + size;
    }
    return ids;
}

} // namespace

ScyroxDevice::ScyroxDevice(QObject *parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
    , m_scanTimer(new QTimer(this))
{
    m_scanTimer->setInterval(SCAN_INTERVAL_MS);
    connect(m_scanTimer, &QTimer::timeout, this, &ScyroxDevice::scanDevices);

    // P0: without this connection pollBattery() is only ever called once, from
    // the constructor, and the displayed level never changes again.
    m_pollTimer->setInterval(POLL_INTERVAL_WIRELESS_MS);
    m_pollInterval = POLL_INTERVAL_WIRELESS_MS;
    connect(m_pollTimer, &QTimer::timeout, this, &ScyroxDevice::pollBattery);

    scanDevices();
    pollBattery();

    m_scanTimer->start();
    m_pollTimer->start();
}

ScyroxDevice::~ScyroxDevice()
{
    closeDevice();
}

int ScyroxDevice::voltageToLevel(int voltageMv, bool charging)
{
    voltageMv = qBound(0, voltageMv, 4200);

    if (voltageMv >= VOLTAGE_THRESHOLDS.last())
        return charging ? 99 : 100;

    for (int i = 0; i < VOLTAGE_THRESHOLDS.size(); ++i) {
        if (voltageMv >= VOLTAGE_THRESHOLDS[i])
            continue;
        if (i == 0)
            return 1;
        const int lower = VOLTAGE_THRESHOLDS[i - 1];
        const int upper = VOLTAGE_THRESHOLDS[i];
        const double segmentSize = (upper - lower) / 5.0;
        const double interpolated = (voltageMv - lower) / segmentSize + (i - 1) * 5.0;
        int level = int(interpolated + 0.5);
        if (level == 0 || level == 15)
            level += 1;
        return qBound(0, level, 100);
    }
    return 100;
}

bool ScyroxDevice::supportsReportId8(const QString &hidrawPath)
{
    QFile file(QStringLiteral("/sys/class/hidraw/%1/device/report_descriptor").arg(hidrawPath));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return hidReportIds(file.readAll()).contains(REPORT_ID);
}

bool ScyroxDevice::discoverDevices()
{
    m_devices.clear();

    const QDir hidrawDir(QStringLiteral("/sys/class/hidraw"));
    const QStringList entries = hidrawDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &entry : entries) {
        QFile uevent(hidrawDir.filePath(entry + QStringLiteral("/device/uevent")));
        if (!uevent.open(QIODevice::ReadOnly))
            continue;

        const QString content = QString::fromUtf8(uevent.readAll());
        uevent.close();

        int vid = 0, pid = 0;
        bool hasPhys = false;
        for (const QString &line : content.split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("HID_PHYS="))) {
                hasPhys = true;
            } else if (line.startsWith(QLatin1String("HID_ID="))) {
                const QStringList parts = line.mid(7).split(QLatin1Char(':'));
                if (parts.size() == 3) {
                    vid = parts[1].toUInt(nullptr, 16);
                    pid = parts[2].toUInt(nullptr, 16);
                }
            }
        }

        if (!hasPhys)
            continue;   // a virtual (uhid) device has no physical path
        if (vid != VENDOR_ID)
            continue;
        if (pid != PRODUCT_ID_WIRED && pid != PRODUCT_ID_WIRELESS)
            continue;
        if (!supportsReportId8(entry))
            continue;

        DeviceInfo info;
        info.path = QStringLiteral("/dev/") + entry;
        info.node = entry;
        info.mode = (pid == PRODUCT_ID_WIRED) ? QStringLiteral("wired")
                                              : QStringLiteral("wireless");
        info.pid = pid;

        // Key backoffs on the kernel HID instance name, not on hidrawN: the
        // node number is reused across replugs, so a freshly plugged mouse
        // would otherwise inherit the previous device's backoff.
        const QString target = QFileInfo(hidrawDir.filePath(entry + QStringLiteral("/device"))).symLinkTarget();
        info.ident = target.isEmpty() ? entry : target.section(QLatin1Char('/'), -1);

        m_devices.append(info);
    }

    std::sort(m_devices.begin(), m_devices.end(), [](const DeviceInfo &a, const DeviceInfo &b) {
        if (a.mode != b.mode)
            return a.mode == QLatin1String("wired");   // cable wins over radio
        return a.path < b.path;
    });

    return !m_devices.isEmpty();
}

bool ScyroxDevice::openDevice(const QString &path)
{
    if (m_fd >= 0 && m_devicePath == path)
        return true;

    closeDevice();

    m_fd = ::open(path.toUtf8().constData(), O_RDWR | O_NONBLOCK);
    if (m_fd < 0) {
        const int err = errno;
        const QString reason = QString::fromLocal8Bit(strerror(err));
        if (reason != m_lastOpenError) {
            qWarning() << "Scyrox: cannot open" << path << ":" << reason;
            if (err == EACCES)
                qWarning() << "Scyrox: the hidraw node is root-only."
                              " Install config/99-scyrox.rules (README,"
                              " \"Device permissions\"), then replug the device.";
        }
        m_lastOpenError = reason;
        // Qt suppresses qWarning() whenever stderr is not a console, and a
        // tray app is normally started without one — so the reason also goes
        // into the published state, where the tooltip can show it.
        m_data.error = (err == EACCES)
            ? tr("No permission for %1 — install config/99-scyrox.rules").arg(path)
            : tr("Cannot open %1 (%2)").arg(path, reason);
        return false;
    }

    m_lastOpenError.clear();
    m_data.error.clear();
    m_devicePath = path;
    return true;
}

void ScyroxDevice::closeDevice()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_devicePath.clear();
    m_deviceIdent.clear();
    m_devicePid = 0;
    m_failures = 0;
    m_data.error.clear();   // the device is simply gone, not broken
}

QByteArray ScyroxDevice::buildReport(int command)
{
    QByteArray buf(16, 0);
    buf[0] = char(command);

    int sum = 0;
    for (int i = 0; i < 15; ++i)
        sum += quint8(buf[i]);
    buf[15] = char((0x55 - (sum & 0xFF) - REPORT_ID) & 0xFF);

    QByteArray report;
    report.append(char(REPORT_ID));
    report.append(buf);
    return report;
}

bool ScyroxDevice::verifyChecksum(const QByteArray &response)
{
    if (response.size() != REPORT_LENGTH)
        return false;

    int sum = 0;
    for (int i = 0; i < REPORT_LENGTH; ++i)
        sum += quint8(response[i]);

    return (sum & 0xFF) == 0x55;
}

void ScyroxDevice::drainInput()
{
    if (m_fd < 0)
        return;

    // The fd is O_NONBLOCK, so an empty queue ends the loop immediately. This
    // discards late replies to earlier commands: seen live, a BatteryLevel
    // frame was consumed as the answer to DeviceOnline, which made an online
    // mouse look disconnected.
    char buf[64];
    for (int i = 0; i < 16; ++i) {
        if (::read(m_fd, buf, sizeof(buf)) <= 0)
            break;
    }
}

bool ScyroxDevice::transact(int command, QByteArray &response)
{
    if (m_fd < 0)
        return false;

    drainInput();

    const QByteArray report = buildReport(command);
    if (::write(m_fd, report.constData(), report.size()) != report.size()) {
        qWarning() << "Scyrox: write failed:" << strerror(errno);
        return false;
    }

    // Mirror of the reference _recv(): wait up to one second, discarding any
    // frame that does not belong to this command. A plain select()+read() would
    // accept the first frame in the queue whatever it is. The fd is never
    // closed here — a silent mouse is normal (sleep), and dropping the handle
    // on every timeout caused open/close churn; the caller decides when three
    // consecutive failures mean the source is gone.
    QElapsedTimer elapsed;
    elapsed.start();
    response.clear();

    while (elapsed.elapsed() < RECV_TIMEOUT_MS) {
        const qint64 remaining = RECV_TIMEOUT_MS - elapsed.elapsed();

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(m_fd, &fds);
        struct timeval tv;
        tv.tv_sec = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;

        if (::select(m_fd + 1, &fds, nullptr, nullptr, &tv) <= 0)
            continue;                                   // ran out of time

        char buf[64];
        const ssize_t n = ::read(m_fd, buf, sizeof(buf));
        if (n != REPORT_LENGTH)
            continue;                                   // short read: not ours

        const QByteArray frame(buf, int(n));
        if (frame.at(0) != char(REPORT_ID) || frame.at(1) != char(command)
            || !verifyChecksum(frame))
            continue;                                   // stale or foreign frame

        response = frame;
        return true;
    }
    return false;
}

QString ScyroxDevice::deviceAddress(const QByteArray &onlineResponse)
{
    if (onlineResponse.size() < 10)
        return QString();
    return QStringLiteral("%1%2%3")
        .arg(quint8(onlineResponse.at(7)), 2, 16, QChar('0'))
        .arg(quint8(onlineResponse.at(8)), 2, 16, QChar('0'))
        .arg(quint8(onlineResponse.at(9)), 2, 16, QChar('0'));
}

ScyroxDevice::Link ScyroxDevice::queryOnline(QByteArray &response)
{
    if (!transact(COMMAND_ONLINE, response))
        return Link::NoResponse;
    if (response.size() < 10)
        return Link::NoResponse;
    // Byte 6 is the online flag; byte 7..9 the device address. Verified against
    // the reference daemon and live hardware (asleep: 00, awake: 01).
    return (response.at(6) == 1) ? Link::Online : Link::Offline;
}

bool ScyroxDevice::readBattery()
{
    QByteArray response;
    if (!transact(COMMAND_BATTERY, response))
        return false;
    if (response.size() < 10)
        return false;

    const int rawLevel = quint8(response.at(6));
    if (rawLevel > 100)
        return false;

    // displayLevel is intentionally left alone here: smoothing has to compare
    // the new target against the value currently on screen, so overwriting it
    // first made smoothLevel() a no-op.
    m_data.rawLevel = rawLevel;
    m_data.charging = (response.at(7) == 1);
    m_data.voltageMv = (int(quint8(response.at(8))) << 8) | quint8(response.at(9));
    m_data.connected = true;
    m_data.cached = false;
    m_data.lastUpdate = QDateTime::currentSecsSinceEpoch();
    return true;
}

int ScyroxDevice::smoothLevel(int targetLevel, qint64 now)
{
    if (!m_data.connected || m_data.cached)
        return targetLevel;

    const qint64 elapsed = now - m_lastStepTime;
    if (elapsed < 0 || elapsed > STATE_RESET_SECONDS)
        return targetLevel;   // first reading, or a long gap: snap to reality

    const int stepCount = int(elapsed / SMOOTH_STEP_SECONDS);
    if (stepCount == 0 || m_data.displayLevel == targetLevel)
        return m_data.displayLevel;

    const int diff = targetLevel - m_data.displayLevel;
    const int adjustment = qMin(qAbs(diff), stepCount);
    return m_data.displayLevel + (diff > 0 ? adjustment : -adjustment);
}

void ScyroxDevice::setPollInterval(int ms)
{
    // Calling setInterval() on a *running* timer restarts it. scanDevices()
    // fires every 5 s, so an unconditional set here would reset the 60 s
    // countdown forever and pollBattery() would never run (measured: 0 fires in
    // 12 s). Only write when the value really changes.
    if (m_pollInterval == ms)
        return;
    m_pollInterval = ms;
    m_pollTimer->setInterval(ms);
}

void ScyroxDevice::syncIntervalForMode()
{
    if (m_data.mode == m_intervalMode)
        return;   // mode unchanged: leave the running timer alone
    m_intervalMode = m_data.mode;
    setPollInterval(preferredPollInterval());
}

int ScyroxDevice::preferredPollInterval() const
{
    return (m_data.mode == QLatin1String("wired")) ? POLL_INTERVAL_WIRED_MS
                                                    : POLL_INTERVAL_WIRELESS_MS;
}

void ScyroxDevice::publish()
{
    if (m_data.connected != m_emitted.connected)
        emit connectionChanged(m_data.connected);

    if (m_data != m_emitted) {
        m_emitted = m_data;
        emit dataChanged(m_data);
    }
}

void ScyroxDevice::scanDevices()
{
    discoverDevices();

    // Forget backoffs that have expired or belong to a unit that is no longer
    // attached — the kernel HID instance counter grows on every replug, so
    // pruning keeps this map from growing without bound over a long session.
    if (!m_backoff.isEmpty()) {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        QSet<QString> present;
        for (const DeviceInfo &info : m_devices)
            present.insert(info.ident);
        for (auto it = m_backoff.begin(); it != m_backoff.end();) {
            if (it.value() <= now || !present.contains(it.key()))
                it = m_backoff.erase(it);
            else
                ++it;
        }
    }

    // Drop the current interface when it disappears from sysfs.
    if (m_fd >= 0 && !m_devicePath.isEmpty()) {
        bool present = false;
        for (const DeviceInfo &info : m_devices) {
            if (info.path == m_devicePath) {
                present = true;
                break;
            }
        }
        if (!present) {
            closeDevice();
            m_data.connected = false;
            m_data.cached = false;
            loadState(m_data.address);   // best effort: keep a level on screen
        }
    }

    // Attach to a candidate while we hold no open interface.
    if (m_fd < 0 && !m_devices.isEmpty()) {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        for (const DeviceInfo &info : m_devices) {
            if (m_backoff.value(info.ident, 0) > now)
                continue;
            if (openDevice(info.path)) {
                m_deviceIdent = info.ident;
                m_devicePid = info.pid;
                m_data.mode = info.mode;
                break;
            }
            m_backoff[info.ident] = now + ATTACH_BACKOFF_SECONDS;
        }
    }

    if (m_devices.isEmpty()) {
        m_data.mode.clear();
        m_data.error.clear();
    } else {
        // Keep pid/mode in sync for an interface that stayed open the whole
        // time (a cable can be plugged in beside a dongle that never drops).
        for (const DeviceInfo &info : m_devices) {
            if (info.path == m_devicePath) {
                m_devicePid = info.pid;
                m_data.mode = info.mode;
                break;
            }
        }
    }

    syncIntervalForMode();
    publish();
}

void ScyroxDevice::onPollFailure()
{
    ++m_failures;
    setPollInterval(RETRY_INTERVAL_MS);   // come back in 5 s, not a full cycle

    if (m_failures < MAX_FAILURES) {
        publish();
        return;
    }

    qWarning() << "Scyrox: no response after" << m_failures << "attempts, backing off"
               << m_deviceIdent;
    m_backoff[m_deviceIdent] = QDateTime::currentSecsSinceEpoch() + DROP_BACKOFF_SECONDS;
    closeDevice();                        // resets m_failures
    m_data.connected = false;
    m_data.cached = false;
    publish();
}

void ScyroxDevice::pollBattery()
{
    if (m_fd < 0) {
        scanDevices();                    // also publishes whatever it finds
        if (m_fd < 0) {
            setPollInterval(preferredPollInterval());
            return;
        }
    }

    const bool isWired = (m_devicePid == PRODUCT_ID_WIRED);

    if (!isWired) {
        // The dongle answers DeviceOnline on the mouse's behalf, so ask before
        // spending a BatteryLevel query on a radio link that is down.
        QByteArray online;
        const Link link = queryOnline(online);

        if (link == Link::NoResponse) {
            onPollFailure();
            return;
        }
        if (link == Link::Offline) {
            // The dongle answers DeviceOnline from its own state — the sleeping
            // mouse never hears it, so re-checking every few seconds costs the
            // mouse nothing (pure USB traffic to a mains-powered dongle) while
            // shrinking the window in which the only thing we can show is the
            // cached reading. The previous full-cycle wait meant the mouse had
            // almost always fallen asleep before we looked, so the tray sat on
            // stale numbers indefinitely.
            setPollInterval(OFFLINE_RETRY_MS);
            if (loadState(m_data.address)) {
                m_data.connected = true;
                m_data.cached = true;
            }
            publish();
            return;
        }

        // One exchange yields both the link state and the address.
        const QString addr = deviceAddress(online);
        if (!addr.isEmpty())
            m_data.address = addr;
    }

    if (!readBattery()) {
        onPollFailure();
        return;
    }

    m_failures = 0;
    setPollInterval(preferredPollInterval());

    const int target = (m_data.voltageMv > 0)
                           ? voltageToLevel(m_data.voltageMv, m_data.charging)
                           : m_data.rawLevel;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    m_data.displayLevel = smoothLevel(target, now);
    m_lastStepTime = now;

    saveState();
    publish();
}

void ScyroxDevice::saveState()
{
    // Written only when the values actually moved: a fixed cadence would put a
    // state file on disk every minute for data that rarely changes.
    QJsonObject obj;
    obj[QLatin1String("rawLevel")] = m_data.rawLevel;
    obj[QLatin1String("displayLevel")] = m_data.displayLevel;
    obj[QLatin1String("charging")] = m_data.charging;
    obj[QLatin1String("voltageMv")] = m_data.voltageMv;
    obj[QLatin1String("mode")] = m_data.mode;
    obj[QLatin1String("address")] = m_data.address;
    obj[QLatin1String("timestamp")] = qint64(0);

    const quint64 signature = qHash(QString::fromUtf8(
        QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    if (m_stateSignatureValid && signature == m_stateSignature)
        return;

    const QString stateDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(stateDir))
        return;

    obj[QLatin1String("timestamp")] = QDateTime::currentSecsSinceEpoch();
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    // Write to a side file and rename so a crash mid-write cannot leave a
    // truncated state.json behind.
    const QString finalPath = stateDir + QStringLiteral("/state.json");
    const QString tempPath = finalPath + QStringLiteral(".tmp");

    QFile tmp(tempPath);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Scyrox: cannot write" << tempPath;
        return;
    }
    const bool written = tmp.write(payload) == payload.size();
    tmp.flush();
    tmp.close();
    if (!written) {
        QFile::remove(tempPath);
        return;
    }
    if (::rename(tempPath.toLocal8Bit().constData(),
                 finalPath.toLocal8Bit().constData()) != 0) {
        qWarning() << "Scyrox: cannot replace state file:" << strerror(errno);
        QFile::remove(tempPath);
        return;
    }
    m_stateSignature = signature;
    m_stateSignatureValid = true;
}

bool ScyroxDevice::loadState(const QString &address)
{
    const QString stateDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QFile file(stateDir + QStringLiteral("/state.json"));
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject())
        return false;

    const QJsonObject obj = doc.object();

    const qint64 timestamp = obj[QLatin1String("timestamp")].toVariant().toLongLong();
    if (timestamp <= 0 || QDateTime::currentSecsSinceEpoch() - timestamp > STATE_RESET_SECONDS)
        return false;

    // Refuse a cached reading that belongs to the other transport or to a
    // different unit: a wired level is meaningless on the dongle and vice versa.
    const QString savedMode = obj[QLatin1String("mode")].toString();
    if (!savedMode.isEmpty() && !m_data.mode.isEmpty() && savedMode != m_data.mode)
        return false;

    const QString savedAddress = obj[QLatin1String("address")].toString();
    if (!address.isEmpty() && !savedAddress.isEmpty() && savedAddress != address)
        return false;

    m_data.rawLevel = obj[QLatin1String("rawLevel")].toInt();
    m_data.displayLevel = obj[QLatin1String("displayLevel")].toInt();
    m_data.voltageMv = obj[QLatin1String("voltageMv")].toInt();
    // The level drifts over hours, so reusing it is fine. Charging is not
    // reusable: it flips the instant a cable is plugged or pulled, and we only
    // reach this path because the mouse is asleep and cannot tell us. Keeping
    // the saved flag made the tray keep claiming "Charging" long after the
    // mouse came off charge — the caller therefore sees charging = false for
    // any cached reading and must not present it as a fact.
    m_data.charging = false;
    m_data.mode = savedMode;
    m_data.address = savedAddress;
    m_data.connected = true;
    m_data.cached = true;
    m_data.lastUpdate = timestamp;

    return true;
}
