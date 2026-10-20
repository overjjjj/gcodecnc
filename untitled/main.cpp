#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include "src/app/MainWindow.h"
#include "src/app/AppController.h"
#include "src/core/Settings.h"
#include "src/strategies/StrategyFactory.h"
#include "src/tool/ToolLibrary.h"
#include "src/workspaces/program_verification/simulation/SimulationController.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QTranslator>
#include <QLibraryInfo>
#include <QSettings>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

int main(int argc, char *argv[])
{
    // Prefer software OpenGL for stability across mixed Qt/MinGW/driver setups.
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication a(argc, argv);
    a.setOrganizationName("JIANGJUREN");
    a.setApplicationName("CNEXT-CAM");
    a.setApplicationVersion("0.1.0+20261019.p0");
    const bool diagnostic = a.arguments().contains(QStringLiteral("--diagnostic-hidden-smoke"));
    const bool activeSimulation = diagnostic && a.arguments().contains(QStringLiteral("--diagnostic-active-simulation"));
    const QString diagnosticPath = QCoreApplication::applicationDirPath() + QStringLiteral("/diagnostic-smoke.json");
    QJsonObject diagnosticReport;
    int simulationTicks = 0;
    if (diagnostic) {
        // Run only from a disposable runtime copy: tools.json also lives beside this EXE.
        const QString settingsPath = QCoreApplication::applicationDirPath() + QStringLiteral("/diagnostic-settings");
        if (!QDir().mkpath(settingsPath)) return 2;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsPath);
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsPath);
    }

    // Load translation
    QString lang = Settings::instance().language();  // "zh_CN" or "en_US"
    static QTranslator qtTr;
    static QTranslator appTr;

    if (qtTr.load("qt_" + lang,
                  QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        a.installTranslator(&qtTr);

    if (appTr.load(QDir(QCoreApplication::applicationDirPath()).filePath("translations/" + lang)) ||
        appTr.load(QDir(QCoreApplication::applicationDirPath()).filePath(lang)) ||
        appTr.load(":/translations/" + lang))
        a.installTranslator(&appTr);

    // Bootstrap singletons
    StrategyFactory::instance();   // registers all strategies

    int result = 0;
    {
        MainWindow w;
        if (diagnostic) w.setAttribute(Qt::WA_DontShowOnScreen);
        w.show();
        if (diagnostic) {
            QTimer::singleShot(0, &w, [&]() {
                diagnosticReport["eventLoopEntered"] = true;
                diagnosticReport["applicationVersion"] = a.applicationVersion();
                diagnosticReport["windowTitle"] = w.windowTitle();
                auto *simulation = w.findChild<SimulationController *>();
                if (activeSimulation && simulation) {
                    simulation->loadGCode(QStringLiteral("G90 G17 G21\nG0 Z50\nG0 X0 Y0\nG1 X100 F100\nM30\n"), false);
                    simulation->setSpeed(0.01);
                    QObject::connect(simulation, &SimulationController::progressChanged, &w, [&](double fraction) {
                        if (fraction > 0) ++simulationTicks;
                    });
                    simulation->play();
                }
                QTimer::singleShot(750, &w, [&, simulation]() {
                    bool hidden = w.testAttribute(Qt::WA_DontShowOnScreen);
#ifdef Q_OS_WIN
                    hidden = hidden && !IsWindowVisible(reinterpret_cast<HWND>(w.winId()));
#endif
                    const bool running = simulation && simulation->isRunning();
                    diagnosticReport["nativeWindowHidden"] = hidden;
                    diagnosticReport["simulationActiveAtQuit"] = running;
                    diagnosticReport["activeSimulationRequested"] = activeSimulation;
                    diagnosticReport["simulationTicks"] = simulationTicks;
                    diagnosticReport["toolCount"] = ToolLibrary::instance().allTools().size();
                    a.exit(hidden && (!activeSimulation || (running && simulationTicks > 0)) ? 0 : 2);
                });
            });
        }
        result = a.exec();
        if (diagnostic) diagnosticReport["eventLoopReturned"] = true;
    }
    if (diagnostic) {
        diagnosticReport["mainWindowDestroyed"] = true;
        diagnosticReport["eventLoopExitCode"] = result;
        QFile report(diagnosticPath);
        if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
        const QByteArray bytes = QJsonDocument(diagnosticReport).toJson();
        if (report.write(bytes) != bytes.size()) return 2;
    }
    return result;
}
