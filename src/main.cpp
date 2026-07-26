#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("radiacode-monitor"));
    QApplication::setOrganizationName(QStringLiteral("QtRadiacode"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    MainWindow w;
    w.show();
    return app.exec();
}
