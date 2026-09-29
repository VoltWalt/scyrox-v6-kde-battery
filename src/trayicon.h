#pragma once

#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QSettings>
#include "scyroxdevice.h"

class TrayIcon : public QSystemTrayIcon
{
    Q_OBJECT

public:
    explicit TrayIcon(ScyroxDevice *device, QObject *parent = nullptr);
    ~TrayIcon() override;

private slots:
    void onDataChanged(const ScyroxData &data);
    void onConnectionChanged(bool connected);
    void onSettings();
    void onQuit();

private:
    void resetPresentation(const QString &error = QString());
    void updateTooltip(const ScyroxData &data);
    void updateIcon(const ScyroxData &data);
    void checkNotifications(const ScyroxData &data);
    QString formatVoltage(int mv) const;

    ScyroxDevice *m_device;
    QMenu *m_menu;
    QAction *m_settingsAction;
    QAction *m_quitAction;
    QSettings m_settings;

    int m_lastLevel = -1;
    bool m_lastCharging = false;
    bool m_lastConnected = false;
    QString m_lastTooltip;   // avoid re-sending an identical tooltip

    // Notification tracking
    bool m_lowNotified = false;
    bool m_criticalNotified = false;
};
