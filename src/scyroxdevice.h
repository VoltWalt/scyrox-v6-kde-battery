#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QFile>
#include <QVector>

struct ScyroxData {
    int rawLevel = 0;        // 0-100 from firmware
    int displayLevel = 0;    // smoothed 0-100
    bool charging = false;
    int voltageMv = 0;       // millivolts
    bool connected = false;
    bool cached = false;     // true if from state file
    QString mode;            // "wired" or "wireless"
    QString address;         // device address from online response
};

class ScyroxDevice : public QObject
{
    Q_OBJECT

public:
    explicit ScyroxDevice(QObject *parent = nullptr);
    ~ScyroxDevice() override;

    ScyroxData data() const { return m_data; }
    bool isConnected() const { return m_data.connected; }

    // Voltage to level conversion (from Scyrox S-Center curve)
    static int voltageToLevel(int voltageMv, bool charging);

signals:
    void dataChanged(const ScyroxData &data);
    void connectionChanged(bool connected);

private slots:
    void pollBattery();

private:
    bool discoverDevice();
    bool openDevice();
    void closeDevice();
    bool transact(int command, QByteArray &response);
    bool readBattery();
    bool readOnline();
    QByteArray buildReport(int command);
    bool verifyChecksum(const QByteArray &response);
    QString deviceAddress(const QByteArray &onlineResponse);

    // State persistence
    void saveState();
    bool loadState();

    QString m_devicePath;
    int m_fd = -1;
    QTimer *m_pollTimer;
    ScyroxData m_data;

    // Protocol constants
    static constexpr int VENDOR_ID = 0x3554;
    static constexpr int PRODUCT_ID_WIRED = 0xF5F6;
    static constexpr int PRODUCT_ID_WIRELESS = 0xF5F7;
    static constexpr int REPORT_ID = 8;
    static constexpr int REPORT_LENGTH = 17;
    static constexpr int COMMAND_BATTERY = 4;
    static constexpr int COMMAND_ONLINE = 3;
    static constexpr int POLL_INTERVAL_MS = 30000; // 30 seconds

    // Voltage thresholds for level conversion
    static const QVector<int> VOLTAGE_THRESHOLDS;
};
