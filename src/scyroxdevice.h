#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QFile>
#include <QVector>
#include <QDateTime>
#include <QHash>

struct ScyroxData {
    int rawLevel = 0;        // 0-100 straight from the firmware
    int displayLevel = 0;    // smoothed 0-100 shown in the tray
    bool charging = false;
    int voltageMv = 0;       // millivolts
    bool connected = false;
    bool cached = false;     // true while showing state.json instead of live data
    QString mode;            // "wired" or "wireless"
    QString address;         // device address from the DeviceOnline response
    QString error;           // why the device could not be opened, empty if fine
    qint64 lastUpdate = 0;   // timestamp of the last successful read

    // lastUpdate is deliberately excluded: it changes on every poll and would
    // otherwise make every poll look like a UI-visible change.
    bool operator==(const ScyroxData &o) const
    {
        return rawLevel == o.rawLevel && displayLevel == o.displayLevel
            && charging == o.charging && voltageMv == o.voltageMv
            && connected == o.connected && cached == o.cached
            && mode == o.mode && address == o.address && error == o.error;
    }
    bool operator!=(const ScyroxData &o) const { return !(*this == o); }
};

class ScyroxDevice : public QObject
{
    Q_OBJECT

public:
    explicit ScyroxDevice(QObject *parent = nullptr);
    ~ScyroxDevice() override;

    ScyroxData data() const { return m_data; }
    bool isConnected() const { return m_data.connected; }

    static int voltageToLevel(int voltageMv, bool charging);

signals:
    void dataChanged(const ScyroxData &data);
    void connectionChanged(bool connected);

private slots:
    void pollBattery();
    void scanDevices();

private:
    struct DeviceInfo {
        QString path;    // /dev/hidraw7
        QString node;    // hidraw7
        QString ident;   // kernel HID instance, e.g. 0003:3554:F5F7.0008
        QString mode;    // "wired" or "wireless"
        int pid = 0;
    };

    // Outcome of a single exchange with the dongle. NoResponse (nothing usable
    // came back) must be told apart from Offline (the dongle answered that the
    // mouse radio is down): the first is a failure and is retried quickly, the
    // second is normal for a sleeping mouse and keeps the usual cadence.
    enum class Link { NoResponse, Offline, Online };

    bool discoverDevices();
    bool openDevice(const QString &path);
    void closeDevice();

    bool transact(int command, QByteArray &response);
    void drainInput();
    Link queryOnline(QByteArray &response);
    bool readBattery();

    QByteArray buildReport(int command);
    bool verifyChecksum(const QByteArray &response);
    QString deviceAddress(const QByteArray &onlineResponse);
    bool supportsReportId8(const QString &hidrawPath);
    int smoothLevel(int targetLevel, qint64 now);

    // Timer handling. setInterval() restarts a *running* QTimer, so the poll
    // interval may only be written when the value actually changes.
    void setPollInterval(int ms);
    void syncIntervalForMode();
    int preferredPollInterval() const;

    void onPollFailure();
    void publish();               // emit only when something really changed

    // State persistence
    void saveState();
    bool loadState(const QString &address);

    // Device management
    QVector<DeviceInfo> m_devices;
    QString m_devicePath;
    QString m_deviceIdent;
    int m_devicePid = 0;
    int m_fd = -1;
    QTimer *m_pollTimer;
    QTimer *m_scanTimer;
    ScyroxData m_data;
    ScyroxData m_emitted;         // last state handed to the UI

    // Smoothing state
    qint64 m_lastStepTime = 0;

    // Failure tracking
    int m_failures = 0;
    QHash<QString, qint64> m_backoff;   // keyed by kernel HID instance name

    // Timer / persistence bookkeeping
    int m_pollInterval = -1;      // interval currently programmed
    QString m_intervalMode;       // mode the programmed interval was derived from
    quint64 m_stateSignature = 0; // content signature of the last state.json write
    bool m_stateSignatureValid = false;

    // Last open() failure text. Backoff retries the open every 15 s, so the
    // message is printed once per distinct reason instead of being repeated.
    QString m_lastOpenError;

    // Protocol constants
    static constexpr int VENDOR_ID = 0x3554;
    static constexpr int PRODUCT_ID_WIRED = 0xF5F6;
    static constexpr int PRODUCT_ID_WIRELESS = 0xF5F7;
    static constexpr int REPORT_ID = 8;
    static constexpr int REPORT_LENGTH = 17;
    static constexpr int COMMAND_BATTERY = 4;
    static constexpr int COMMAND_ONLINE = 3;
    static constexpr int POLL_INTERVAL_WIRELESS_MS = 60000;  // radio traffic costs mouse battery
    static constexpr int POLL_INTERVAL_WIRED_MS = 15000;     // cable: free, show charge progress
    static constexpr int SCAN_INTERVAL_MS = 5000;            // cheap sysfs plug-event scan
    static constexpr int RETRY_INTERVAL_MS = 5000;           // come back fast after a failed query
    static constexpr int SMOOTH_STEP_SECONDS = 10;           // at most 1% per 10 seconds
    static constexpr int STATE_RESET_SECONDS = 1800;         // cached state older than 30 min is dropped
    static constexpr int MAX_FAILURES = 3;
    static constexpr int ATTACH_BACKOFF_SECONDS = 15;
    static constexpr int DROP_BACKOFF_SECONDS = 10;
    static constexpr int RECV_TIMEOUT_MS = 1000;

    // Voltage thresholds for level conversion
    static const QVector<int> VOLTAGE_THRESHOLDS;
};
