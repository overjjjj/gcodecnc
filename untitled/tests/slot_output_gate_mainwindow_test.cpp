#include "../src/app/MainWindow.h"
#include "../src/app/AppController.h"
#include "../src/core/ProjectManager.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/gcode/ProgramSnapshotStatus.h"
#include "../src/workspaces/program_verification/safety/GCodeSafetyValidator.h"
#include "../src/workspaces/program_verification/ui/GCodeEditor.h"
#include <QTemporaryDir>
#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QDialog>
#include <QEvent>
#include <QFileDialog>
#include <QListWidget>
#include <QMessageBox>
#include <QTimer>
#include <iostream>
#include <windows.h>

class OutputDialogObserver : public QObject
{
public:
    int rejected = 0;
    int outputDialogs = 0;
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() != QEvent::Show) return false;
        if (auto *message = qobject_cast<QMessageBox *>(object)) {
            const bool expected = message->text().contains("cutter envelope", Qt::CaseInsensitive);
            rejected += expected;
            std::cout << "warning: " << message->text().toStdString() << '\n';
            QTimer::singleShot(0, message, &QDialog::accept);
            return true; // Offscreen Qt/Windows cannot render QMessageBox's native system menu.
        } else if (auto *dialog = qobject_cast<QDialog *>(object)) {
            // Never choose a file, connect to a machine, or click a send button, including in RED runs.
            ++outputDialogs;
            std::cerr << "unexpected output dialog: " << dialog->metaObject()->className() << '\n';
            QTimer::singleShot(0, dialog, &QDialog::reject);
            return true;
        }
        return false;
    }
};

int main(int argc, char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::cerr << "output gate: application startup\n";
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc, argv);
    if (app.arguments().size() != 3) {
        std::cerr << "usage: test <project-dir> <output-dir>\n";
        return 80;
    }
    const QDir projectDir(app.arguments().at(1));
    const QDir outputDir(app.arguments().at(2));
    if (!QDir::setCurrent(projectDir.absolutePath()) || !QDir().mkpath(outputDir.absolutePath())) return 81;
    QTemporaryDir config(outputDir.filePath("settings-XXXXXX"));
    if (!config.isValid()) return 82;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, config.path());
    std::cerr << "output gate: constructing main window\n";
    QTimer::singleShot(15000, []() { std::cerr << "output gate UI timed out\n"; std::_Exit(90); });
    MainWindow window;
    std::cerr << "output gate: main window constructed\n";
    auto *project = AppController::instance().projectManager();
    ProjectManager fixture;
    SetupOrigin origin; origin.confirmed = true; fixture.setSetupOrigin(origin);
    StockDefinition stock; stock.confirmed = true; fixture.setStockDefinition(stock);
    fixture.setActiveRegion(FaceRegion::Front);
    MachiningOperation operation;
    operation.id = "legacy-linear-floor"; operation.opType = OperationType::Contour;
    operation.strategyId = "mill_tapered_slot"; operation.params.set("depth", 4);
    operation.contourFeature.length = 40; operation.contourFeature.width = 12; operation.contourFeature.depth = 4;
    operation.contourFeature.region = FaceRegion::Front;
    for (const auto &tool : ToolLibrary::instance().allTools())
        if (tool.type == "end_mill" && tool.diameter == 6) operation.toolId = tool.id;
    if (operation.toolId <= 0) return 6;
    operation.markToolpathValid(); fixture.setOperations({operation});
    ProgramEntry old;
    old.id = "old-linear-program"; old.sourceOperationIds = QStringList{operation.id};
    old.sourceOperationFingerprint = ProgramSnapshotFingerprint::calculate({operation}, old.sourceOperationIds);
    old.toolFingerprint = ProgramSnapshotFingerprint::calculateTools({operation}, old.sourceOperationIds,
        [](int id) { return ToolLibrary::instance().tool(id); });
    old.setupFingerprint = fixture.setupFingerprint();
    old.gcodeText = QString("G17 G40 G49 G80\nG21\nG90\nG54\nG94\nT%1 M6\nS2000 M3\nM8\nG0 Z50\nG0 X-17 Y0\nG0 Z3\nG1 Z0 F200\nG1 X0 Z-2 F800\nG0 Z50\nM5\nM9\nM30\n").arg(operation.toolId);
    const auto safety = GCodeSafetyValidator::validate(old.gcodeText);
    if (!safety.ok) { std::cerr << safety.messages.join(";").toStdString() << '\n'; return 7; }
    fixture.setPrograms({old});
    const QString path = outputDir.filePath("mainwindow-legacy.cnext");
    if (!fixture.saveToFile(path) || !project->loadFromFile(path)) return 1;
    std::cerr << "output gate: legacy project loaded\n";
    auto *list = window.findChild<QListWidget *>("programList");
    if (!list || list->count() != 1) return 2;
    list->setCurrentRow(-1); list->setCurrentRow(0);
    auto *editor = window.findChild<GCodeEditor *>();
    const auto loaded = project->programById(old.id);
    if (!editor || editor->toPlainText() != old.gcodeText || project->operations()[0].toolpathState != ToolpathState::Valid ||
        loaded.sourceOperationFingerprint != ProgramSnapshotFingerprint::calculate(project->operations(), old.sourceOperationIds) ||
        loaded.setupFingerprint != project->setupFingerprint() ||
        loaded.toolFingerprint != ProgramSnapshotFingerprint::calculateTools(project->operations(), old.sourceOperationIds,
            [](int id) { return ToolLibrary::instance().tool(id); })) return 3;
    OutputDialogObserver observer; app.installEventFilter(&observer);
    for (const char *action : {"onExportGCode", "onSendToMachine"}) {
        if (!QMetaObject::invokeMethod(&window, action, Qt::DirectConnection)) return 4;
    }
    if (observer.rejected != 2 || observer.outputDialogs != 0 || project->programById(old.id).gcodeText != old.gcodeText) {
        std::cerr << "both real output actions must reject the legacy Valid program before opening output dialogs\n";
        return 5;
    }
    std::cout << "PASS real MainWindow export/send: 2 policy rejections, 0 output dialogs, old code preserved\n";
    return 0;
}
