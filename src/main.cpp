#include "mainwindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("radiacode-monitor"));
    QApplication::setOrganizationName(QStringLiteral("QtRadiacode"));
#ifndef RADIACODE_MONITOR_VERSION
#define RADIACODE_MONITOR_VERSION "0.0.0"
#endif
    QApplication::setApplicationVersion(QStringLiteral(RADIACODE_MONITOR_VERSION));
    // Window / taskbar icon (all platforms); Windows .exe also embeds icons/*.ico via .rc
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    MainWindow w;
    w.show();
    return app.exec();
}
