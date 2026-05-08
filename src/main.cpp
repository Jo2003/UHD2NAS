#include <QApplication>
#include <QIcon>
#include <QStyleFactory>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("UHD2NAS");
    app.setApplicationName("UHD2NAS");
    app.setApplicationVersion("1.0.0");
    app.setWindowIcon(QIcon(":/uhd2nas.ico"));

    // Use the classic Windows Vista/7 style instead of the modern Windows 11 look
#ifdef Q_OS_WIN
    if (QStyleFactory::keys().contains("windowsvista"))
        app.setStyle(QStyleFactory::create("windowsvista"));
    else if (QStyleFactory::keys().contains("Windows"))
        app.setStyle(QStyleFactory::create("Windows"));
#endif

    MainWindow w;
    w.show();
    return app.exec();
}
