#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/workspaces/process_design/ui/ParameterEditorDialog.h"
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include "../src/workspaces/process_design/ui/FeatureListPanel.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include "../src/workspaces/process_design/operations/SlotOperationState.h"
#include "../src/app/controllers/SlotOperationController.h"
#include "../src/core/ProjectManager.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/strategies/StrategyFactory.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/gcode/ProgramSnapshotStatus.h"
#include <QFile>
#include <QSettings>
#include <QDir>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QTemporaryDir>
#include <QTreeWidgetItemIterator>
#include <QApplication>
#include <QTimer>
#include <QMessageBox>
#include <QLabel>
#include <QFont>
#include <QFontDatabase>
#include <iostream>

static int fail(int line)
{
    std::cerr << "UI assertion failed at line " << line << '\n';
    return 1;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    const auto config = QDir::current().absoluteFilePath("../outputs/slot-b2u/ui-fixtures/config");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, config);
    QDir().mkpath("../outputs/slot-b2u/ui-fixtures");
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    app.setFont(QFont("Microsoft YaHei", 9));
    const QString phase = qEnvironmentVariable("SLOT_UI_PHASE", "green");
    int feedbackFailures = 0;
    QTimer::singleShot(15000, [&]() {
        auto *modal = QApplication::activeModalWidget();
        std::cerr << "UI watchdog: " << (modal ? modal->metaObject()->className() : "no modal")
                  << " / " << (modal ? modal->windowTitle().toStdString() : "") << '\n';
        if (auto *message = qobject_cast<QMessageBox *>(modal)) std::cerr << message->text().toStdString() << '\n';
        std::_Exit(90);
    });
    StrategyPanel panel;
    MachiningFeature f; f.kind = FeatureKind::Slot; f.subType = "blind_slot";
    f.length = 40; f.width = 12; f.depth = 4; f.region = FaceRegion::Front;
    panel.setActiveRegion(FaceRegion::Front);
    panel.setContourFeature(f);
    int proposals = 0;
    QObject::connect(&panel, &StrategyPanel::operationProposalConfirmed,
                     [&](const OperationProposal &) { ++proposals; });
    QMetaObject::invokeMethod(&panel, "onConfirmOperation");
    bool openedParameters = false;
    QTimer::singleShot(0, [&]() {
        if (auto *dialog = qobject_cast<ParameterEditorDialog *>(QApplication::activeModalWidget())) {
            openedParameters = true; dialog->reject();
        }
    });
    QMetaObject::invokeMethod(&panel, "onEditHoleParams");
    app.processEvents();
    if (proposals || openedParameters) {
        std::cerr << "slot must select geometry before parameter edit or final proposal\n";
        return fail(__LINE__);
    }
    ProjectManager project; FeatureListPanel features; OperationListPanel operations;
    project.setSourceFileFingerprint("model-v1"); project.setActiveRegion(FaceRegion::Front);
    SetupOrigin origin; origin.confirmed = true; project.setSetupOrigin(origin);
    StockDefinition stock; stock.confirmed = true; project.setStockDefinition(stock);
    auto second = f; second.center.setX(50);
    project.setFeatures({f, second}); features.setFeatures(project.features());
    features.selectFeature(0);
    auto convert = [](const MachiningFeature &source) {
        ContourFeature g; g.subType = source.subType; g.center = source.center;
        g.length = source.length; g.width = source.width; g.depth = source.depth;
        g.angle = source.angle; g.axis = source.axis; g.region = source.region; return g;
    };
    SlotOperationController controller(&project, &features, &panel, &operations, convert,
        [](const OperationProposal &, const ToolEntry &) { return QString(); }, nullptr);
    QStringList errors;
    QObject::connect(&controller, &SlotOperationController::errorOccurred, [&](const QString &error) { errors << error; });
    int writes = 0;
    QObject::connect(&operations, &OperationListPanel::operationsEdited, [&](const QList<MachiningOperation> &ops) {
        ++writes; project.setOperations(ops);
    });
    QObject::connect(&features, &FeatureListPanel::featureSelected, [&](int index) { panel.setContourFeature(project.features()[index]); });
    auto *tree = features.findChild<QTreeWidget *>();
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toInt() >= 0) (*it)->setCheckState(0, Qt::Checked);
    }
    int selections = 0, cancelAt = 2;
    std::function<void(ParameterEditorDialog *)> parameterAction;
    QTimer dialogs;
    QObject::connect(&dialogs, &QTimer::timeout, [&]() {
        if (auto *selection = dynamic_cast<SelectionChainDialog *>(QApplication::activeModalWidget())) {
            ++selections;
            if (selections == cancelAt) selection->reject();
            else { selection->findChild<QListWidget *>("selectionCandidates")->setCurrentRow(0); selection->accept(); }
        } else if (auto *editor = qobject_cast<ParameterEditorDialog *>(QApplication::activeModalWidget())) {
            if (parameterAction) { auto action = parameterAction; parameterAction = {}; action(editor); }
            else editor->reject();
        }
    });
    dialogs.start(0);
    QMetaObject::invokeMethod(&panel, "onConfirmOperation");
    if (selections != 2 || writes || !operations.operations().isEmpty() ||
        panel.findChild<QPushButton *>("editStrategyParameters")->isEnabled()) {
        std::cerr << "second slot cancellation must discard the complete non-executable draft\n"; return fail(__LINE__);
    }
    cancelAt = -1;
    QMetaObject::invokeMethod(&panel, "onConfirmOperation");
    panel.retranslateUi();
    if (!panel.findChild<QPushButton *>("editStrategyParameters")->isEnabled() || writes) return fail(__LINE__);
    panel.resize(420, 950);
    panel.grab().save("../outputs/slot-b2u/ui-fixtures/selected-slot-draft.png");
    cancelAt = selections + 1;
    panel.findChild<QPushButton *>("reselectSlotGeometry")->click();
    panel.grab().save("../outputs/slot-b2u/ui-fixtures/" + phase + "-reselect-cancel.png");
    bool staleSummary = false;
    for (auto *label : panel.findChildren<QLabel *>())
        staleSummary |= label->text().contains(QString::fromUtf8("已选 ")) || label->text().contains("selected slots");
    if (staleSummary || panel.findChild<QPushButton *>("editStrategyParameters")->isEnabled() ||
        !panel.findChild<QPushButton *>("strategyPrimaryAction")->isEnabled()) {
        std::cerr << "cancelled geometry must clear selected summary and keep parameters disabled\n";
        ++feedbackFailures;
    }
    cancelAt = -1;
    QMetaObject::invokeMethod(&panel, "onConfirmOperation");
    QMetaObject::invokeMethod(&panel, "onEditHoleParams"); // cancel
    if (writes) return fail(__LINE__);
    auto setValue = [](ParameterEditorDialog *editor, const QString &key, double value) {
        auto *table = editor->findChild<QTableWidget *>();
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 0)->data(Qt::UserRole).toString() == key) table->item(row, 1)->setText(QString::number(value));
    };
    parameterAction = [&](ParameterEditorDialog *editor) {
        auto *table = editor->findChild<QTableWidget *>();
        for (int row = 0; row < table->rowCount(); ++row) {
            const QString key = table->item(row, 0)->data(Qt::UserRole).toString();
            if (key == "slotLength" || key == "slotWidth" || key == "angle") { std::cerr << "editable geometry leaked\n"; std::abort(); }
        }
        setValue(editor, "depth", 2);
        editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    QMetaObject::invokeMethod(&panel, "onEditHoleParams");
    QMetaObject::invokeMethod(&panel, "onConfirmOperation");
    if (writes != 1 || operations.operations().size() != 2) {
        std::cerr << "final confirmation must commit precisely one selected batch: " << errors.join(";").toStdString() << '\n'; return fail(__LINE__);
    }
    const auto first = operations.operations()[0], next = operations.operations()[1];
    if (first.contourFeature.depth != 4 || first.params.get("depth") != 2 ||
        first.selectionEvidence.orderedGeometryIds == next.selectionEvidence.orderedGeometryIds) return fail(__LINE__);
    ProgramGenerationService generator(
        [](const QString &id) { return StrategyFactory::instance().strategy(id); },
        [](int id) { return ToolLibrary::instance().tool(id); },
        [&project, convert](const ContourFeature &candidate) {
            int matches = 0;
            for (const auto &source : project.features())
                if (sameSlotGeometry(candidate, convert(source))) ++matches;
            return matches == 1;
        });
    Cq8PostProcessor post; PostProcessorOptions postOptions;
    postOptions.safeStartBlocks = QStringList{"G17 G40 G49 G80", "G21", "G90", "G54", "G94"};
    const auto oldProgram = generator.generate(operations.operations(), post, postOptions, {});
    if (!oldProgram.ok) { std::cerr << oldProgram.errors.join(";").toStdString(); return fail(__LINE__); }
    project.upsertProgram(oldProgram.snapshot);
    QTemporaryDir temp(QDir::current().absoluteFilePath("../outputs/slot-b2u/ui-fixtures/workflow-XXXXXX")); const auto saved = temp.filePath("workflow.cnext"); project.saveToFile(saved);
    auto editButton = [&]() { return operations.findChild<QToolButton *>("editSlotOperation"); };
    operations.selectOperationById(first.id);
    if (!editButton() || !editButton()->isEnabled()) { std::cerr << "single slot edit action missing\n"; return fail(__LINE__); }
    editButton()->click(); // cancel
    if (writes != 1 || project.isModified()) return fail(__LINE__);
    parameterAction = [&](ParameterEditorDialog *editor) {
        setValue(editor, "depth", 8); editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    editButton()->click();
    if (writes != 1 || project.isModified()) { std::cerr << "invalid edit mutated project\n"; return fail(__LINE__); }
    parameterAction = [&](ParameterEditorDialog *editor) {
        setValue(editor, "helixRadius", 99); editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    editButton()->click();
    if (writes != 1 || project.isModified()) return fail(__LINE__);
    int correctionVisits = 0;
    parameterAction = [&](ParameterEditorDialog *editor) {
        ++correctionVisits;
        const int selectedTool = editor->findChild<QComboBox *>("slotEditTool")->currentData().toInt();
        setValue(editor, "feedRate", 456); setValue(editor, "depth", 8);
        editor->grab().save("../outputs/slot-b2u/ui-fixtures/" + phase + "-invalid-edit.png");
        parameterAction = [&, editor, selectedTool](ParameterEditorDialog *retry) {
            ++correctionVisits;
            if (retry != editor || retry->params().get("feedRate") != 456 || retry->params().get("depth") != 8 ||
                retry->findChild<QComboBox *>("slotEditTool")->currentData().toInt() != selectedTool) {
                std::cerr << "retry must preserve dialog, feed, depth and tool\n"; ++feedbackFailures;
            }
            retry->grab().save("../outputs/slot-b2u/ui-fixtures/" + phase + "-correction.png");
            setValue(retry, "depth", 1.5);
            retry->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        };
        editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    editButton()->click();
    if (correctionVisits != 2) { std::cerr << "invalid depth must remain correctable in the same edit session\n"; return fail(__LINE__); }
    if (writes != 2 || operations.operations()[0].id != first.id || operations.operations()[0].params.get("feedRate") != 456 ||
        !sameSlotOperation(operations.operations()[1], next)) { std::cerr << "valid edit did not replace once\n"; return fail(__LINE__); }
    project.saveToFile(saved);
    parameterAction = [&](ParameterEditorDialog *editor) { editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click(); };
    editButton()->click();
    if (writes != 2 || project.isModified()) { std::cerr << "unchanged confirmation must be a no-op\n"; return fail(__LINE__); }
    const auto stale = ProgramSnapshotStatus::evaluate(operations.operations(), oldProgram.snapshot.sourceOperationIds,
        oldProgram.snapshot.sourceOperationFingerprint, {}, {}, oldProgram.snapshot.toolFingerprint, oldProgram.snapshot.toolFingerprint);
    if (stale.state != ProgramSnapshotState::Changed || stale.okForOutput()) return fail(__LINE__);
    ProjectManager reopened;
    if (!reopened.loadFromFile(saved) || reopened.operations().size() != 2 ||
        !sameSlotOperation(reopened.operations()[0], operations.operations()[0]) ||
        !sameSlotOperation(reopened.operations()[1], next)) { std::cerr << "save/reopen changed slot operation\n"; return fail(__LINE__); }
    // Recalculate from stored final geometry and normalized cutting parameters after reopening.
    for (auto operation : reopened.operations()) {
        const auto path = StrategyFactory::instance().strategy(operation.strategyId)->generate(operation.contourFeature,
            ToolLibrary::instance().tool(operation.toolId), slotCuttingParams(operation.params));
        if (!path.ok || path.gcode.isEmpty()) return fail(__LINE__);
    }
    const auto newProgram = generator.generate(reopened.operations(), post, postOptions, {});
    const auto fresh = ProgramSnapshotStatus::evaluate(reopened.operations(), newProgram.snapshot.sourceOperationIds,
        newProgram.snapshot.sourceOperationFingerprint, {}, {}, newProgram.snapshot.toolFingerprint, newProgram.snapshot.toolFingerprint);
    if (!newProgram.ok || !fresh.okForOutput() || !newProgram.snapshot.gcodeText.contains("F456") ||
        !newProgram.snapshot.gcodeText.contains("Z-1.500") || reopened.operations()[0].contourFeature.depth != 4) {
        std::cerr << "edited program must use new feed/depth and become fresh\n"; return fail(__LINE__);
    }
    QFile oldCode("../outputs/slot-b2u/ui-fixtures/ui-before-edit.nc");
    if (oldCode.open(QIODevice::WriteOnly)) oldCode.write(oldProgram.snapshot.gcodeText.toUtf8());
    QFile newCode("../outputs/slot-b2u/ui-fixtures/ui-after-edit.nc");
    if (newCode.open(QIODevice::WriteOnly)) newCode.write(newProgram.snapshot.gcodeText.toUtf8());
    reopened.upsertProgram(newProgram.snapshot);
    reopened.saveToFile("../outputs/slot-b2u/ui-fixtures/ui-edited-project.cnext");
    std::cout << "UI lifecycle: old Changed/output blocked, reopened recalculation passed, new Fresh F456 depth1.5/real4\n";
    features.selectFeature(1); // Editing must retain the first operation even when the sidebar points elsewhere.
    parameterAction = [&](ParameterEditorDialog *editor) {
        QTimer::singleShot(0, []() {
            auto *selection = dynamic_cast<SelectionChainDialog *>(QApplication::activeModalWidget());
            if (!selection) std::abort();
            selection->findChild<QListWidget *>("selectionCandidates")->setCurrentRow(0);
            selection->accept();
        });
        editor->findChild<QPushButton *>("reselectEditedSlot")->click();
        editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    editButton()->click();
    if (writes != 2 || project.isModified()) { std::cerr << "reselection substituted sidebar geometry\n"; return fail(__LINE__); }
    parameterAction = [&](ParameterEditorDialog *editor) {
        QTimer::singleShot(0, []() {
            auto *selection = dynamic_cast<SelectionChainDialog *>(QApplication::activeModalWidget());
            if (!selection) std::abort();
            selection->reject();
        });
        editor->findChild<QPushButton *>("reselectEditedSlot")->click();
        setValue(editor, "feedRate", 777);
        editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    editButton()->click();
    if (writes != 2 || project.isModified()) { std::cerr << "cancelled reselection reused old evidence\n"; return fail(__LINE__); }
    parameterAction = [&](ParameterEditorDialog *editor) {
        editor->findChild<QComboBox *>("slotEditTool")->setCurrentIndex(-1);
        editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    editButton()->click();
    if (writes != 2 || project.isModified()) return fail(__LINE__);
    const auto beforeContextChange = operations.operations();
    const int errorsBeforeContextChange = errors.size();
    parameterAction = [&](ParameterEditorDialog *editor) {
        project.setWorkOffset("G55");
        QTimer::singleShot(0, []() {
            auto *selection = dynamic_cast<SelectionChainDialog *>(QApplication::activeModalWidget());
            if (!selection) std::abort();
            selection->findChild<QListWidget *>("selectionCandidates")->setCurrentRow(0);
            selection->accept();
        });
        editor->findChild<QPushButton *>("reselectEditedSlot")->click();
        setValue(editor, "feedRate", 789);
        editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    editButton()->click();
    if (writes != 2 || errors.size() != errorsBeforeContextChange + 1 || !errors.last().contains("Setup") ||
        !sameSlotOperation(operations.operations()[0], beforeContextChange[0]) ||
        !sameSlotOperation(operations.operations()[1], beforeContextChange[1])) {
        std::cerr << "reselection must not refresh an edit session after Setup changes\n"; return fail(__LINE__);
    }
    project.setWorkOffset("G54");
    // Every context or membership change invalidates an already selected draft, including a language refresh.
    QMetaObject::invokeMethod(&panel, "onConfirmOperation");
    project.setWorkOffset("G55"); panel.retranslateUi();
    if (panel.findChild<QPushButton *>("editStrategyParameters")->isEnabled()) return fail(__LINE__);
    QMetaObject::invokeMethod(&panel, "onConfirmOperation");
    project.setSourceFileFingerprint("model-v2"); panel.retranslateUi();
    if (panel.findChild<QPushButton *>("editStrategyParameters")->isEnabled()) return fail(__LINE__);
    QMetaObject::invokeMethod(&panel, "onConfirmOperation");
    features.selectFeature(0); panel.retranslateUi();
    if (panel.findChild<QPushButton *>("editStrategyParameters")->isEnabled()) return fail(__LINE__);
    if (writes != 2) return fail(__LINE__);
    for (const auto &change : QList<std::function<void()>>{
        [&]() { project.setSetupOrigin(origin); },
        [&]() { project.setSetupRotation(QQuaternion::fromEulerAngles(0, 0, 90)); },
        [&]() { project.setActiveRegion(FaceRegion::Back); },
        [&]() { for (QTreeWidgetItemIterator it(tree); *it; ++it) if ((*it)->data(0, Qt::UserRole).toInt() == 1) (*it)->setCheckState(0, Qt::Unchecked); },
        [&]() { auto openSlot = f; openSlot.subType = "open_slot"; panel.setContourFeature(openSlot); }
    }) {
        project.setActiveRegion(FaceRegion::Front); panel.setContourFeature(f);
        QMetaObject::invokeMethod(&panel, "onConfirmOperation");
        if (!panel.findChild<QPushButton *>("editStrategyParameters")->isEnabled()) return fail(__LINE__);
        change(); panel.retranslateUi();
        if (panel.findChild<QPushButton *>("editStrategyParameters")->isEnabled() || writes != 2) return fail(__LINE__);
    }
    StrategyPanel shallowPanel;
    shallowPanel.setActiveRegion(FaceRegion::Front);
    auto deep = f; deep.depth = 100;
    shallowPanel.setContourFeature(deep); shallowPanel.setSlotGeometry({convert(deep)});
    auto *confirmShallow = shallowPanel.findChild<QPushButton *>("strategyPrimaryAction");
    if (confirmShallow->isEnabled()) return fail(__LINE__);
    parameterAction = [&](ParameterEditorDialog *editor) {
        setValue(editor, "depth", 2);
        editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    };
    QMetaObject::invokeMethod(&shallowPanel, "onEditHoleParams");
    if (!confirmShallow->isEnabled()) { std::cerr << "shallower legal depth must refresh confirmation availability\n"; return fail(__LINE__); }
    dialogs.stop();
    return feedbackFailures ? 1 : 0;
}
