#include "scyroxdevice.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDateTime>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
#include <string.h>

const QVector<int> ScyroxDevice::VOLTAGE_THRESHOLDS = {
    3050, 3420, 3480, 3540, 3600, 3660, 3720, 3760, 3800, 3840,
    3880, 3920, 3940, 3960, 3980, 4000, 4020, 4040, 4060, 4080, 4110
};

ScyroxDevice::ScyroxDevice(QObject *parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    connect(m_pollTimer, &QTimer::timeout, this, &ScyroxDevice::pollBattery);

    // Try to load cached state
    loadState();

    // Initial poll
    pollBattery();

    // Start polling
    m_pollTimer->start();
}

ScyroxDevice::~ScyroxDevice()
{
    closeDevice();
}

int ScyroxDevice::voltageToLevel(int voltageMv, bool charging)
{
    if (voltageMv >= VOLTAGE_THRESHOLDS.last())
        return charging ? 99 : 100;

    for (int i = 0; i < VOLTAGE_THRESHOLDS.size(); ++i) {
        if (voltageMv >= VOLTAGE_THRESHOLDS[i])
            continue;
        if (i == 0)
            return 1;
        int lower = VOLTAGE_THRESHOLDS[i - 1];
        int upper = VOLTAGE_THRESHOLDS[i];
        double segmentSize = (upper - lower) / 5.0;
        double interpolated = (voltageMv - lower) / segmentSize + (i - 1) * 5.0;
        int level = (int)(interpolated + 0.5);
        if (level == 0 || level == 15)
            level += 1;
        return qBound(0, level, 100);
    }
    return 100;
}

bool ScyroxDevice::discoverDevice()
{
    QDir hidrawDir("/sys/class/hidraw");
    const QStringList entries = hidrawDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &entry : entries) {
        QFile uevent(hidrawDir.filePath(entry + "/device/uevent"));
        if (!uevent.open(QIODevice::ReadOnly))
            continue;

        QString content = QString::fromUtf8(uevent.readAll());
        uevent.close();

        // Parse HID_ID
        int vid = 0, pid = 0;
        for (const QString &line : content.split('\n')) {
            if (line.startsWith("HID_ID=")) {
                QStringList parts = line.mid(6).split(':');
                if (parts.size() == 3) {
                    vid = parts[1].toUInt(nullptr, 16);
                    pid = parts[2].toUInt(nullptr, 16);
                }
                break;
            }
        }

        if (vid != VENDOR_ID)
            continue;
        if (pid != PRODUCT_ID_WIRED && pid != PRODUCT_ID_WIRELESS)
            continue;

        m_devicePath = "/dev/" + entry;
        m_data.mode = (pid == PRODUCT_ID_WIRED) ? "wired" : "wireless";
        return true;
    }

    return false;
}

bool ScyroxDevice::openDevice()
{
    if (m_devicePath.isEmpty()) {
        if (!discoverDevice())
            return false;
    }

    m_fd = open(m_devicePath.toUtf8().constData(), O_RDWR | O_NONBLOCK);
    if (m_fd < 0) {
        qWarning() << "Scyrox: Cannot open" << m_devicePath << ":" << strerror(errno);
        return false;
    }

    return true;
}

void ScyroxDevice::closeDevice()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

QByteArray ScyroxDevice::buildReport(int command)
{
    // 16-byte buffer (report ID is prepended separately)
    QByteArray buf(16, 0);
    buf[0] = (char)command;
    // bytes 1-3 are already 0
    // byte 4 is payload length + type offset (0 for mouse)
    // bytes 5-14 are payload (0 for battery/online commands)

    // Checksum: byte15 = (0x55 - (sum(buf[0..14]) & 0xFF) - 8) & 0xFF
    int sum = 0;
    for (int i = 0; i < 15; ++i)
        sum += (unsigned char)buf[i];
    buf[15] = (char)((0x55 - (sum & 0xFF) - REPORT_ID) & 0xFF);

    // Prepend report ID
    QByteArray report;
    report.append((char)REPORT_ID);
    report.append(buf);

    return report;
}

bool ScyroxDevice::verifyChecksum(const QByteArray &response)
{
    if (response.size() != REPORT_LENGTH)
        return false;

    int sum = 0;
    for (int i = 0; i < REPORT_LENGTH; ++i)
        sum += (unsigned char)response[i];

    return (sum & 0xFF) == 0x55;
}

bool ScyroxDevice::transact(int command, QByteArray &response)
{
    if (m_fd < 0) {
        if (!openDevice())
            return false;
    }

    QByteArray report = buildReport(command);

    // Send command
    ssize_t written = write(m_fd, report.constData(), report.size());
    if (written != report.size()) {
        qWarning() << "Scyrox: Write failed:" << strerror(errno);
        closeDevice();
        return false;
    }

    // Wait for response with timeout
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(m_fd, &fds);

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    int ret = select(m_fd + 1, &fds, nullptr, nullptr, &tv);
    if (ret <= 0) {
        qWarning() << "Scyrox: Timeout waiting for response";
        closeDevice();
        return false;
    }

    // Read response
    char buf[64];
    ssize_t n = read(m_fd, buf, sizeof(buf));
    if (n < 2) {
        qWarning() << "Scyrox: Read failed:" << strerror(errno);
        closeDevice();
        return false;
    }

    response = QByteArray(buf, n);

    // Verify response
    if (response[0] != (char)REPORT_ID || response[1] != (char)command) {
        qWarning() << "Scyrox: Unexpected response";
        return false;
    }

    if (!verifyChecksum(response)) {
        qWarning() << "Scyrox: Invalid checksum";
        return false;
    }

    return true;
}

QString ScyroxDevice::deviceAddress(const QByteArray &onlineResponse)
{
    // Address is at bytes 7-9 of the response
    if (onlineResponse.size() < 10)
        return QString();
    return QString("%1%2%3")
        .arg((unsigned char)onlineResponse[7], 2, 16, QChar('0'))
        .arg((unsigned char)onlineResponse[8], 2, 16, QChar('0'))
        .arg((unsigned char)onlineResponse[9], 2, 16, QChar('0'));
}

bool ScyroxDevice::readOnline()
{
    QByteArray response;
    if (!transact(COMMAND_ONLINE, response))
        return false;

    // Response byte 5: 1 = online, 0 = offline
    if (response.size() < 7)
        return false;

    return response[5] == 1;
}

bool ScyroxDevice::readBattery()
{
    QByteArray response;
    if (!transact(COMMAND_BATTERY, response))
        return false;

    if (response.size() < 10)
        return false;

    // Parse response
    int rawLevel = (unsigned char)response[6];
    bool charging = (response[7] == 1);
    int voltageMv = ((unsigned char)response[8] << 8) | (unsigned char)response[9];

    // Validate
    if (rawLevel < 0 || rawLevel > 100)
        return false;

    // Calculate display level from voltage
    int targetLevel = (voltageMv > 0) ? voltageToLevel(voltageMv, charging) : rawLevel;

    // Smooth: move at most 1% per 10 seconds
    int displayLevel = targetLevel;
    if (m_data.connected && !m_data.cached) {
        int diff = targetLevel - m_data.displayLevel;
        if (diff != 0) {
            // Simple smoothing - in production, use time-based
            displayLevel = m_data.displayLevel + (diff > 0 ? 1 : -1);
        }
    }

    m_data.rawLevel = rawLevel;
    m_data.displayLevel = displayLevel;
    m_data.charging = charging;
    m_data.voltageMv = voltageMv;
    m_data.connected = true;
    m_data.cached = false;

    return true;
}

void ScyroxDevice::pollBattery()
{
    bool wasConnected = m_data.connected;

    if (!m_data.connected || m_fd < 0) {
        // Try to connect
        if (!openDevice()) {
            if (wasConnected) {
                m_data.connected = false;
                emit connectionChanged(false);
                emit dataChanged(m_data);
            }
            return;
        }
    }

    // Check if device is online
    if (!readOnline()) {
        // Device offline - use cached state
        if (loadState()) {
            m_data.connected = true;
            m_data.cached = true;
            emit dataChanged(m_data);
        } else if (wasConnected) {
            m_data.connected = false;
            emit connectionChanged(false);
            emit dataChanged(m_data);
        }
        return;
    }

    // Read battery
    if (!readBattery()) {
        if (wasConnected) {
            m_data.connected = false;
            emit connectionChanged(false);
            emit dataChanged(m_data);
        }
        return;
    }

    // Get device address
    QByteArray onlineResponse;
    if (transact(COMMAND_ONLINE, onlineResponse)) {
        m_data.address = deviceAddress(onlineResponse);
    }

    // Save state
    saveState();

    // Notify
    if (!wasConnected) {
        emit connectionChanged(true);
    }
    emit dataChanged(m_data);
}

void ScyroxDevice::saveState()
{
    QString stateDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(stateDir);

    QFile file(stateDir + "/state.json");
    if (!file.open(QIODevice::WriteOnly))
        return;

    QJsonObject obj;
    obj["rawLevel"] = m_data.rawLevel;
    obj["displayLevel"] = m_data.displayLevel;
    obj["charging"] = m_data.charging;
    obj["voltageMv"] = m_data.voltageMv;
    obj["mode"] = m_data.mode;
    obj["address"] = m_data.address;
    obj["timestamp"] = QDateTime::currentSecsSinceEpoch();

    file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    file.close();
}

bool ScyroxDevice::loadState()
{
    QString stateDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QFile file(stateDir + "/state.json");
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject())
        return false;

    QJsonObject obj = doc.object();

    // Check if state is too old (30 minutes)
    qint64 timestamp = obj["timestamp"].toVariant().toLongLong();
    if (QDateTime::currentSecsSinceEpoch() - timestamp > 1800)
        return false;

    m_data.rawLevel = obj["rawLevel"].toInt();
    m_data.displayLevel = obj["displayLevel"].toInt();
    m_data.charging = obj["charging"].toBool();
    m_data.voltageMv = obj["voltageMv"].toInt();
    m_data.mode = obj["mode"].toString();
    m_data.address = obj["address"].toString();
    m_data.connected = true;
    m_data.cached = true;

    return true;
}
