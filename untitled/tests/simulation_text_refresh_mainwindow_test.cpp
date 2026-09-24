#include "../src/app/MainWindow.h"
#include "../src/app/AppController.h"
#include "../src/core/ProjectManager.h"
#include "../src/workspaces/program_verification/ui/GCodeEditor.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QListWidget>
#include <iostream>
#include <windows.h>

static bool check(bool ok, const char *message)
{
    if (!ok) std::cerr << "FAIL: " << message << '\n';
    return ok;
}

int main(int argc, char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc, argv);
    QTemporaryDir config;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, config.path());
    MainWindow window;
    auto *editor = window.findChild<GCodeEditor *>();
    auto *sim = window.findChild<SimulationController *>();
    if (!check(editor && sim, "simulation widgets exist")) return 2;
    auto play = [&]() { return QMetaObject::invokeMethod(&window, "onSimPlay", Qt::DirectConnection); };
    editor->setPlainText("G90\nG0 X10\nG1 X20 F100\nG1 X30\n");
    if (!play() || !check(sim->toolPath().last().x() == 30, "initial path")) return 3;
    double progress = -1;
    QObject::connect(sim, &SimulationController::progressChanged, [&](double value) { progress = value; });
    QMetaObject::invokeMethod(sim, "onTick", Qt::DirectConnection);
    QMetaObject::invokeMethod(&window, "onSimPause", Qt::DirectConnection);
    const double paused = progress;
    int reloads = 0;
    QObject::connect(sim, &SimulationController::toolPathReady, [&]() { ++reloads; });
    play();
    if (!check(reloads == 0 && progress == paused && sim->isRunning(), "unchanged pause resumes without reload")) return 4;
    QMetaObject::invokeMethod(sim, "onTick", Qt::DirectConnection);
    if (!check(progress > paused, "resume advances from paused position")) return 5;
    editor->setPlainText("G90\nG0 X40\nG1 X50 F100\n");
    play();
    if (!check(!sim->toolPath().isEmpty() && sim->toolPath().last().x() == 50,
               "edited text must replace previously loaded trajectory")) return 6;
    editor->clear();
    play();
    if (!check(sim->toolPath().isEmpty() && !sim->isRunning(), "clearing text cannot replay old trajectory")) return 7;

    ProgramEntry program;
    program.id = "expanded-preview";
    program.name = "expanded-preview";
    program.gcodeText = "G90\nG0 X60\n";
    program.expandedGcodeText = "G90\nG0 X70\nG1 X80 F100\n";
    AppController::instance().projectManager()->setPrograms({program});
    auto *list = window.findChild<QListWidget *>("programList");
    if (!check(list && list->count() == 1, "generated snapshot listed")) return 8;
    list->setCurrentRow(-1);
    list->setCurrentRow(0);
    play();
    if (!check(!sim->toolPath().isEmpty() && sim->toolPath().last().x() == 80,
               "unmodified generated program retains expanded preview")) return 9;
    editor->setPlainText("G90\nG0 X90\n");
    play();
    if (!check(sim->toolPath().last().x() == 90, "editing generated text replaces expanded preview")) return 10;
    std::cout << "PASS: edited text, clear, pause/resume, expanded preview\n";
    return 0;
}
