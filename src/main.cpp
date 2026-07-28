#include "mainwindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("radiacode-monitor"));
    QApplication::setOrganizationName(QStringLiteral("QtRadiacode"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    // Window / taskbar icon (all platforms); Windows .exe also embeds icons/*.ico via .rc
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    MainWindow w;
    w.show();
    return app.exec();
}
