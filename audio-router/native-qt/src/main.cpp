#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTimer>

#include "router/ui/main_window.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("VDO.Ninja");
    QApplication::setOrganizationDomain("vdo.ninja");
    QApplication::setApplicationName("VDOCable");
    QApplication::setApplicationVersion(APP_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription("VDO Cable for VDO.Ninja");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption smokeTestOption("smoke-test", "Launch the UI and exit automatically after a short delay.");
    QCommandLineOption autoExitMsOption("auto-exit-ms", "Exit after the given number of milliseconds.", "ms", "0");
    parser.addOption(smokeTestOption);
    parser.addOption(autoExitMsOption);
    parser.process(app);

    router::ui::MainWindow window;
    window.show();

    int autoExitMs = parser.value(autoExitMsOption).toInt();
    if (parser.isSet(smokeTestOption) && autoExitMs <= 0) {
        autoExitMs = 2500;
    }
    if (autoExitMs > 0) {
        QTimer::singleShot(autoExitMs, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
