#include "trayicon.h"
#include "batteryicon.h"
#include "settingsdialog.h"
#include <QApplication>

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

    // Initial state
    if (m_device->isConnected()) {
        onDataChanged(m_device->data());
    } else {
        setIcon(BatteryIcon::render(0, false));
        setToolTip(tr("Scyrox V6 - Not connected"));
        m_lastConnected = false;
    }

    show();
}

TrayIcon::~TrayIcon()
{
    delete m_menu;
}

void TrayIcon::onDataChanged(const ScyroxData &data)
{
    updateIcon(data);
    updateTooltip(data);
}

void TrayIcon::onConnectionChanged(bool connected)
{
    if (!connected) {
        setIcon(BatteryIcon::render(0, false));
        setToolTip(tr("Scyrox V6 - Not connected"));
        m_lastConnected = false;
        m_lastLevel = -1;
        m_lastCharging = false;
    }
}

void TrayIcon::updateIcon(const ScyroxData &data)
{
    // Only re-render if something changed
    if (data.displayLevel != m_lastLevel || data.charging != m_lastCharging || !m_lastConnected) {
        setIcon(BatteryIcon::render(data.displayLevel, data.charging));
        m_lastLevel = data.displayLevel;
        m_lastCharging = data.charging;
        m_lastConnected = true;
    }
}

void TrayIcon::updateTooltip(const ScyroxData &data)
{
    QString status = data.charging ? tr("Charging") : tr("Discharging");
    QString voltage = formatVoltage(data.voltageMv);
    QString mode = (data.mode == "wired") ? tr("Wired") : tr("Wireless");

    QString tooltip = QString("Scyrox V6\n%1: %2%\n%3\n%4: %5")
                          .arg(status)
                          .arg(data.displayLevel)
                          .arg(voltage)
                          .arg(tr("Mode"))
                          .arg(mode);

    if (data.cached)
        tooltip += tr("\n(cached)");

    setToolTip(tooltip);
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
    dlg.exec();
}

void TrayIcon::onQuit()
{
    QApplication::quit();
}
