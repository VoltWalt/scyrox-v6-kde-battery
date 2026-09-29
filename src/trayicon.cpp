#include "trayicon.h"
#include "batteryicon.h"
#include "settingsdialog.h"
#include <QApplication>
#include <QStringList>
#include <QDateTime>
#include <QTimer>

TrayIcon::TrayIcon(ScyroxDevice *device, QObject *parent)
    : QSystemTrayIcon(parent)
    , m_device(device)
    , m_settings("scyrox", "scyrox-v6")
{
    m_menu = new QMenu();

    m_settingsAction = m_menu->addAction(tr("Settings..."));
    m_menu->addSeparator();
    m_quitAction = m_menu->addAction(tr("Quit"));

    setContextMenu(m_menu);

    connect(m_settingsAction, &QAction::triggered, this, &TrayIcon::onSettings);
    connect(m_quitAction, &QAction::triggered, this, &TrayIcon::onQuit);
    connect(m_device, &ScyroxDevice::dataChanged, this, &TrayIcon::onDataChanged);
    connect(m_device, &ScyroxDevice::connectionChanged, this, &TrayIcon::onConnectionChanged);

    // ScyroxDevice emits before this object exists, so seed the tray from the
    // state it already holds instead of waiting for the next change.
    onDataChanged(m_device->data());

    // A cached reading is only re-published when its numbers move, but the
    // "last read N ago" line has to keep counting up. Cheap: one string
    // compare every 30 s, and updateTooltip() skips setToolTip() unless the
    // text actually changed.
    m_ageTimer = new QTimer(this);
    m_ageTimer->setInterval(30000);
    connect(m_ageTimer, &QTimer::timeout, this, [this]() {
        const ScyroxData d = m_device->data();
        if (d.connected)
            updateTooltip(d);
    });
    m_ageTimer->start();

    show();
}

TrayIcon::~TrayIcon()
{
    delete m_menu;
}

void TrayIcon::onDataChanged(const ScyroxData &data)
{
    if (!data.connected) {
        // The device layer also emits the stale payload alongside the
        // disconnect; rendering it here would undo the "not connected" icon.
        // data.error explains *why* nothing is reachable — without it a
        // permission problem is invisible when the app starts from autostart.
        resetPresentation(data.error);
        return;
    }

    updateIcon(data);
    updateTooltip(data);
    checkNotifications(data);
}

void TrayIcon::onConnectionChanged(bool connected)
{
    if (!connected)
        resetPresentation();
    // Coming back online needs no special handling: dataChanged follows and
    // repaints because updateIcon() sees m_lastConnected == false.
}

void TrayIcon::resetPresentation(const QString &error)
{
    setIcon(BatteryIcon::render(0, false));
    const QString text = error.isEmpty()
        ? tr("Scyrox V6 - Not connected")
        : tr("Scyrox V6 - Not connected\n%1").arg(error);
    m_lastTooltip = text;
    setToolTip(text);
    m_lastLevel = -1;
    m_lastCharging = false;
    m_lastConnected = false;
    m_lowNotified = false;
    m_criticalNotified = false;
}

void TrayIcon::updateIcon(const ScyroxData &data)
{
    // The lightning bolt would be drawn from a cached flag describing a cable
    // that may have been unplugged since, so it belongs to live readings only.
    const bool charging = data.charging && !data.cached;

    if (data.displayLevel == m_lastLevel && charging == m_lastCharging && m_lastConnected)
        return;

    setIcon(BatteryIcon::render(data.displayLevel, charging));
    m_lastLevel = data.displayLevel;
    m_lastCharging = charging;
    m_lastConnected = true;
}

void TrayIcon::updateTooltip(const ScyroxData &data)
{
    const bool detailed = m_settings.value("showTooltip", true).toBool();
    const bool showPct = m_settings.value("showPercentage", true).toBool();

    QStringList lines;
    lines << QStringLiteral("Scyrox V6");

    if (!data.error.isEmpty())
        lines << data.error;

    if (showPct) {
        if (data.cached) {
            // Deliberately no "Charging"/"Discharging" word: that flag records
            // the moment of the last read, and the cable may have been pulled
            // since. Asserting it is what made the tray keep claiming the mouse
            // was charging long after it stopped.
            lines << QStringLiteral("%1%").arg(data.displayLevel);
        } else {
            const QString status = data.charging ? tr("Charging") : tr("Discharging");
            lines << QStringLiteral("%1: %2%").arg(status).arg(data.displayLevel);
        }
    }

    if (detailed) {
        lines << formatVoltage(data.voltageMv);
        const QString mode = (data.mode == QLatin1String("wired")) ? tr("Wired")
                                                                   : tr("Wireless");
        lines << QStringLiteral("%1: %2").arg(tr("Mode"), mode);
    }

    // Shown whenever the numbers are cached, regardless of the detail setting,
    // because it is the reason they may not be current.
    if (data.cached)
        lines << tr("Last read %1 ago").arg(formatAge(data.lastUpdate));

    const QString text = lines.join(QLatin1Char('\n'));
    if (text == m_lastTooltip)
        return;   // the device layer publishes every 5 s; don't push twice

    m_lastTooltip = text;
    setToolTip(text);
}

void TrayIcon::checkNotifications(const ScyroxData &data)
{
    if (!m_settings.value("notificationsEnabled", true).toBool())
        return;
    if (data.cached)
        return;   // a cached reading describes the past, not the mouse

    const int low = qBound(5, m_settings.value("lowThreshold", 20).toInt(), 50);
    const int critical = qBound(1, m_settings.value("criticalThreshold", 10).toInt(), low - 1);

    if (data.displayLevel <= critical) {
        if (m_criticalNotified)
            return;
        m_criticalNotified = true;
        m_lowNotified = true;   // a critical alert also satisfies the low one,
                                // otherwise the next poll repeats as a warning
        showMessage(tr("Scyrox V6 Battery Critical"),
                    tr("Battery is at %1%. Please charge your mouse.").arg(data.displayLevel),
                    QSystemTrayIcon::Critical);
        return;
    }

    if (data.displayLevel <= low) {
        if (m_lowNotified)
            return;
        m_lowNotified = true;
        showMessage(tr("Scyrox V6 Battery Low"),
                    tr("Battery is at %1%.").arg(data.displayLevel),
                    QSystemTrayIcon::Warning);
        return;
    }

    // Above both thresholds: re-arm for the next discharge cycle.
    m_lowNotified = false;
    m_criticalNotified = false;
}

QString TrayIcon::formatVoltage(int mv) const
{
    if (mv <= 0)
        return tr("Voltage: —");
    return tr("Voltage: %1 mV").arg(mv);
}

QString TrayIcon::formatAge(qint64 timestamp) const
{
    if (timestamp <= 0)
        return tr("an unknown time");

    const qint64 seconds = QDateTime::currentSecsSinceEpoch() - timestamp;
    if (seconds < 0)
        return tr("just now");
    if (seconds < 60)
        return tr("less than a minute");
    if (seconds < 3600)
        return tr("%1 min").arg(seconds / 60);
    if (seconds < 86400)
        return tr("%1 h %2 min").arg(seconds / 3600).arg((seconds % 3600) / 60);
    return tr("%1 d %2 h").arg(seconds / 86400).arg((seconds % 86400) / 3600);
}

void TrayIcon::onSettings()
{
    SettingsDialog dlg(&m_settings);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_lastTooltip.clear();   // the options may have changed the wording
    onDataChanged(m_device->data());
}

void TrayIcon::onQuit()
{
    QApplication::quit();
}
