#include "trayicon.h"
#include "batteryicon.h"
#include "settingsdialog.h"
#include <QApplication>
#include <QStringList>

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
        resetPresentation();
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

void TrayIcon::resetPresentation()
{
    m_lastTooltip.clear();
    setIcon(BatteryIcon::render(0, false));
    setToolTip(tr("Scyrox V6 - Not connected"));
    m_lastLevel = -1;
    m_lastCharging = false;
    m_lastConnected = false;
    m_lowNotified = false;
    m_criticalNotified = false;
}

void TrayIcon::updateIcon(const ScyroxData &data)
{
    if (data.displayLevel == m_lastLevel && data.charging == m_lastCharging
        && m_lastConnected)
        return;

    setIcon(BatteryIcon::render(data.displayLevel, data.charging));
    m_lastLevel = data.displayLevel;
    m_lastCharging = data.charging;
    m_lastConnected = true;
}

void TrayIcon::updateTooltip(const ScyroxData &data)
{
    const bool detailed = m_settings.value("showTooltip", true).toBool();
    const bool showPct = m_settings.value("showPercentage", true).toBool();

    QStringList lines;
    lines << QStringLiteral("Scyrox V6");

    if (showPct) {
        const QString status = data.charging ? tr("Charging") : tr("Discharging");
        lines << QStringLiteral("%1: %2%").arg(status).arg(data.displayLevel);
    }

    if (detailed) {
        lines << formatVoltage(data.voltageMv);
        const QString mode = (data.mode == QLatin1String("wired")) ? tr("Wired")
                                                                   : tr("Wireless");
        lines << QStringLiteral("%1: %2").arg(tr("Mode"), mode);
        if (data.cached)
            lines << tr("(cached)");
    }

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
