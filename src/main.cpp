#include <QApplication>
#include "trayicon.h"
#include "scyroxdevice.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Scyrox V6");
    app.setApplicationDisplayName("Scyrox V6 Battery Indicator");
    app.setOrganizationName("Scyrox");
    app.setQuitOnLastWindowClosed(false);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning() << "Scyrox: System tray is not available on this system";
        return 1;
    }

    ScyroxDevice device;
    TrayIcon tray(&device);

    return app.exec();
}
