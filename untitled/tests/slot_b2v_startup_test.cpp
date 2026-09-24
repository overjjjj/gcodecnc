#include "../src/app/MainWindow.h"
#include "../src/app/AppController.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include "../src/workspaces/process_design/ui/FeatureListPanel.h"
#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include "../src/workspaces/process_design/ui/SetupOriginDialog.h"
#include "../src/workspaces/process_design/ui/StockDefinitionDialog.h"
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include "../src/workspaces/process_design/ui/ParameterEditorDialog.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/gcode/ProgramSnapshotStatus.h"
#include "../src/workspaces/program_verification/safety/GCodeSafetyValidator.h"
#include "../src/workspaces/process_design/operations/SlotOperationState.h"
#include <QToolButton>
#include <QToolBar>
#include "../src/core/Settings.h"
#include <QFileDialog>
#include <QTimer>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QEvent>
#include <QSplitter>
#include <functional>
#include <iostream>
#include <windows.h>

class DialogObserver : public QObject
{
public:
    std::function<void(QWidget *)> handle;
    bool eventFilter(QObject *object, QEvent *event) override
    {
        auto *dialog = qobject_cast<QDialog *>(object);
        if (!dialog || event->type() != QEvent::Show) return false;
        QTimer::singleShot(0, dialog, [this, dialog]() { handle(dialog); });
        return true; // Avoid the unsupported Windows system-menu paint in offscreen Qt.
    }
};

int main(int argc, char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::cerr << "B2V: application startup\n";
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
    std::cerr << "B2V: constructing main window\n";
    MainWindow window;
    std::cerr << "B2V: main window constructed\n";
    auto *mainToolbar = window.findChild<QToolBar *>("mainToolBar");
    if (!mainToolbar) return 60;
    QComboBox *postCombo = nullptr;
    for (auto *combo : mainToolbar->findChildren<QComboBox *>()) {
        if (combo->findData(QStringLiteral("cq8")) < 0) continue;
        if (postCombo) return 61;
        postCombo = combo;
    }
    if (!postCombo) return 62;
    const int cq8Index = postCombo->findData(QStringLiteral("cq8"));
    if (postCombo->currentIndex() == cq8Index) {
        if (postCombo->count() < 2) return 63;
        postCombo->setCurrentIndex((cq8Index + 1) % postCombo->count());
    }
    postCombo->setCurrentIndex(cq8Index);
    if (postCombo->currentData().toString() != "cq8" ||
        Settings::instance().postProcessorId() != "cq8") return 64;
    std::cerr << "CQ8 selected through mainToolBar QComboBox currentIndexChanged\n";

    const QString path = QString::fromUtf8("测试模型/槽.stp");
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly)) return 1;
    const QString hash = QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256).toHex().toUpper();
    if (hash != "39D1BB74350AA020AD1AD688BC3C32F12CE13C57DB0C1278E9BE79BBC7A38045") return 2;
    bool imported = false;
    auto &controller = AppController::instance();
    QObject::connect(&controller, &AppController::stepImported, [&](const QString &) { imported = true; });
    QObject::connect(&controller, &AppController::errorOccurred, [](const QString &error) { std::cerr << error.toStdString() << '\n'; });
    controller.importStep(path);
    if (!imported || controller.projectManager()->mesh().isEmpty()) return 3;
    auto *project = controller.projectManager();
    int unexpectedMessages = 0;
    int geometryDialogs = 0;
    int parameterDialogs = 0;
    int setupDialogs = 0;
    bool cancelGeometry = false, cancelParameters = false, editParameters = false, reselectCancel = false;
    bool expectStale = false, expectLegacyGeometry = false;
    int legacyGeometryMessages = 0;
    int staleMessages = 0, fileDialogs = 0, selectionRequiredMessages = 0;
    QString fileDialogPath;
    QString lifecyclePath;
    DialogObserver observer;
    observer.handle = [&](QWidget *modal) {
        std::cerr << "MODAL " << modal->metaObject()->className() << '\n';
        if (auto *message = qobject_cast<QMessageBox *>(modal)) {
            if (expectStale && message->windowTitle() == "G-code snapshot is stale") ++staleMessages;
            else if (expectLegacyGeometry && message->text().contains("reselect the source slot")) ++legacyGeometryMessages;
            else if (reselectCancel && message->text() == "SELECTION_EVIDENCE_REQUIRED") ++selectionRequiredMessages;
            else ++unexpectedMessages;
            std::cerr << "MESSAGE " << message->text().toStdString() << '\n';
            message->reject();
        } else if (auto *selection = dynamic_cast<SelectionChainDialog *>(modal)) {
            ++geometryDialogs;
            if (cancelGeometry) { selection->reject(); return; }
            selection->findChild<QListWidget *>("selectionCandidates")->setCurrentRow(0);
            selection->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        } else if (auto *editor = qobject_cast<ParameterEditorDialog *>(modal)) {
            ++parameterDialogs;
            if (cancelParameters || (reselectCancel && selectionRequiredMessages > 0)) { editor->reject(); return; }
            if (reselectCancel) {
                cancelGeometry = true;
                editor->findChild<QPushButton *>("reselectEditedSlot")->click();
                cancelGeometry = false;
            }
            auto *table = editor->findChild<QTableWidget *>();
            for (int row = 0; row < table->rowCount(); ++row) {
                const QString key = table->item(row, 0)->data(Qt::UserRole).toString();
                if (key == "depth") table->item(row, 1)->setText(editParameters ? "0.3" : "0.5");
                if (key == "stepDown") table->item(row, 1)->setText("0.1");
                if (key == "feedRate") table->item(row, 1)->setText(reselectCancel ? "777" : (editParameters ? "456" : "100"));
            }
            editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        } else if (auto *file = qobject_cast<QFileDialog *>(modal)) {
            ++fileDialogs;
            if (fileDialogPath.isEmpty()) { ++unexpectedMessages; file->reject(); return; }
            file->selectFile(fileDialogPath);
            QMetaObject::invokeMethod(file, "accept");
        } else if (dynamic_cast<SetupOriginDialog *>(modal) || dynamic_cast<StockDefinitionDialog *>(modal)) {
            ++setupDialogs;
            modal->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        } else {
            ++unexpectedMessages;
            qobject_cast<QDialog *>(modal)->reject();
        }
    };
    app.installEventFilter(&observer);
    int setupFace = -1;
    QVector3D setupPoint;
    for (const auto &triangle : project->mesh().triangles) {
        if (triangle.normal.z() < -0.999f) { setupFace = triangle.faceIndex; setupPoint = triangle.v0; break; }
    }
    if (setupFace < 1) return 10;
    std::cerr << "SETUP face=" << setupFace << '\n';
    QMetaObject::invokeMethod(&window, "onSetFrontFace", Q_ARG(bool, true));
    ViewportWidget *viewport = nullptr;
    for (auto *candidate : window.findChildren<ViewportWidget *>()) {
        if (qobject_cast<QSplitter *>(candidate->parentWidget())) viewport = candidate;
    }
    if (!viewport) return 16;
    viewport->facePointPicked(setupFace, setupPoint);
    viewport->facePointPicked(setupFace, setupPoint);
    std::cerr << "SETUP re-recognition rotation=" << project->setupRotation().scalar() << '\n';
    if (qAbs(project->setupRotation().scalar()) > 0.01f ||
        project->activeRegion() != FaceRegion::Front) return 17;
    QMetaObject::invokeMethod(&window, "onEditSetupOrigin");
    QMetaObject::invokeMethod(&window, "onEditStockDefinition");
    if (!project->setupOrigin().confirmed || !project->stockDefinition().confirmed) return 11;
    QJsonArray features;
    int index = 0;
    for (const auto &feature : controller.projectManager()->features()) {
        QJsonArray faces;
        for (int face : feature.faceIndices) faces.append(face);
        features.append(QJsonObject{{"index", index++}, {"kind", int(feature.kind)},
            {"subtype", feature.subType}, {"region", int(feature.region)},
            {"center", QJsonArray{feature.center.x(), feature.center.y(), feature.center.z()}},
            {"axis", QJsonArray{feature.axis.x(), feature.axis.y(), feature.axis.z()}},
            {"length", feature.length}, {"width", feature.width}, {"depth", feature.depth},
            {"angle", feature.angle}, {"faces", faces}});
    }
    QFile output(outputDir.filePath("cq8-real-slot-setup-features.json"));
    if (!output.open(QIODevice::WriteOnly)) return 4;
    output.write(QJsonDocument(QJsonObject{{"model_sha256", hash}, {"features", features},
        {"setup_confirmed", true}, {"setup_face", setupFace}, {"main_window_constructed", true},
        {"source", path}, {"step_imported_signal", imported}}).toJson());
    output.close();
    ToolEntry smallTool; smallTool.id = 9001; smallTool.name = "B2V isolated D1 end mill";
    smallTool.type = "end_mill"; smallTool.diameter = 1; smallTool.fluteLen = 6;
    smallTool.totalLen = 30; smallTool.shankDia = 1; smallTool.flutes = 2;
    ToolLibrary::instance().addTool(smallTool);
    ToolLibrary::instance().saveToFile(outputDir.filePath("cq8-real-setup-tools.json"));
    int slotIndex = -1;
    for (int i = 0; i < project->features().size(); ++i)
        if (project->features()[i].kind == FeatureKind::Slot && project->features()[i].region == FaceRegion::Front) { slotIndex = i; break; }
    if (slotIndex < 0) return 12;
    std::cerr << "SELECT slot=" << slotIndex << '\n';
    auto *panel = window.findChild<StrategyPanel *>();
    window.findChild<FeatureListPanel *>()->selectFeature(slotIndex);
    cancelGeometry = true;
    QMetaObject::invokeMethod(panel, "onConfirmOperation");
    if (!project->operations().isEmpty() || geometryDialogs != 1) return 30;
    std::cerr << "LIFECYCLE create geometry cancel: zero operations\n";
    cancelGeometry = false;
    QMetaObject::invokeMethod(panel, "onConfirmOperation");
    if (!project->operations().isEmpty() || geometryDialogs != 2) return 18;
    cancelParameters = true;
    QMetaObject::invokeMethod(panel, "onEditHoleParams");
    if (!project->operations().isEmpty()) return 31;
    std::cerr << "LIFECYCLE create parameter cancel: zero operations\n";
    cancelParameters = false;
    for (auto *combo : panel->findChildren<QComboBox *>()) {
        const int index = combo->findData(9001);
        if (index >= 0) combo->setCurrentIndex(index);
    }
    QMetaObject::invokeMethod(panel, "onEditHoleParams");
    QMetaObject::invokeMethod(panel, "onConfirmOperation");
    std::cerr << "OPERATIONS " << project->operations().size() << '\n';
    if (project->operations().size() != 1) return 13;
    const auto operation = project->operations().first();
    if (operation.toolId != 9001 || operation.strategyId != "mill_blind_slot" ||
        !operation.selectionEvidence.explicitUserSelection || operation.geometryRefs.isEmpty() ||
        operation.params.get("depth") != 0.5 || operation.params.get("feedRate") != 100 ||
        parameterDialogs != 2 || setupDialogs != 2 || unexpectedMessages != 0) return 19;
    const auto sourceSlot = project->features()[slotIndex];
    std::cerr << "TOPZ sourceBottom=" << sourceSlot.center.z()
              << " sourceDepth=" << sourceSlot.depth
              << " contourTop=" << operation.contourFeature.center.z() << '\n';
    if (qAbs(sourceSlot.center.z() + 5.0) > 1.0e-5 || qAbs(sourceSlot.depth - 5.0) > 1.0e-5 ||
        qAbs(operation.contourFeature.center.z()) > 1.0e-5) return 45;
    QMetaObject::invokeMethod(window.findChild<OperationListPanel *>(), "onGenerateProgram");
    std::cerr << "PROGRAMS " << project->programs().size() << '\n';
    if (project->programs().isEmpty()) return 14;
    const auto program = project->programs().last();
    if (!program.gcodeText.contains("Z-0.500")) return 46;
    if (program.postProcessorId != "cq8" ||
        !program.gcodeText.contains("Post-processor: CQ8 Macro B")) return 65;
    const auto safety = GCodeSafetyValidator::validate(program.gcodeText);
    const auto snapshot = ProgramSnapshotStatus::evaluate(project->operations(), program.sourceOperationIds,
        program.sourceOperationFingerprint, project->setupFingerprint(), program.setupFingerprint,
        ProgramSnapshotFingerprint::calculateTools(project->operations(), program.sourceOperationIds,
            [](int id) { return ToolLibrary::instance().tool(id); }), program.toolFingerprint);
    std::cerr << "CHECK safety=" << safety.ok << " snapshot=" << int(snapshot.state)
              << " messages=" << safety.messages.join(";").toStdString() << '\n';
    if (!safety.ok || snapshot.state != ProgramSnapshotState::Fresh || unexpectedMessages != 0) return 20;
    QFile nc(outputDir.filePath("cq8-real-setup-first.nc"));
    if (!nc.open(QIODevice::WriteOnly)) return 15;
    nc.write(project->programs().last().gcodeText.toUtf8());
    if (!project->saveToFile(outputDir.filePath("cq8-real-setup-first.cnext"))) return 21;
    QFile receipt(outputDir.filePath("cq8-real-setup-result.json"));
    if (!receipt.open(QIODevice::WriteOnly)) return 22;
    receipt.write(QJsonDocument(QJsonObject{{"model_sha256", hash}, {"setup_face", setupFace},
        {"setup_rotation_w", project->setupRotation().scalar()}, {"active_region", int(project->activeRegion())},
        {"setup_origin_confirmed", project->setupOrigin().confirmed},
        {"stock_confirmed", project->stockDefinition().confirmed}, {"feature_index", slotIndex},
        {"operation_id", operation.id}, {"operation_count", project->operations().size()},
        {"geometry_refs", QJsonArray::fromStringList(operation.geometryRefs)},
        {"selection_id", operation.selectionEvidence.id}, {"strategy", operation.strategyId},
        {"tool_id", operation.toolId}, {"params", QJsonObject::fromVariantMap(operation.params.values)},
        {"program_id", program.id}, {"program_count", project->programs().size()},
        {"snapshot_fresh", snapshot.state == ProgramSnapshotState::Fresh}, {"offline_safety_ok", safety.ok},
        {"geometry_dialogs", geometryDialogs}, {"parameter_dialogs", parameterDialogs},
        {"setup_dialogs", setupDialogs}, {"unexpected_messages", unexpectedMessages}}).toJson());
    auto *operations = window.findChild<OperationListPanel *>();
    operations->selectOperationById(operation.id);
    auto *edit = operations->findChild<QToolButton *>("editSlotOperation");
    if (!edit || !edit->isEnabled()) return 32;
    cancelParameters = true;
    edit->click();
    cancelParameters = false;
    if (!sameSlotOperation(operation, project->operations().first())) return 33;
    std::cerr << "LIFECYCLE edit cancel: original operation unchanged\n";
    editParameters = true;
    edit->click();
    if (project->operations().size() != 1) return 34;
    const auto edited = project->operations().first();
    if (edited.id != operation.id || edited.params.get("depth") != 0.3 ||
        edited.params.get("feedRate") != 456 || !(edited.selectionEvidence == operation.selectionEvidence) ||
        edited.geometryRefs != operation.geometryRefs ||
        !sameSlotGeometry(edited.contourFeature, operation.contourFeature)) return 35;
    const auto statusFor = [&](const ProgramEntry &entry) {
        return ProgramSnapshotStatus::evaluate(project->operations(), entry.sourceOperationIds,
            entry.sourceOperationFingerprint, project->setupFingerprint(), entry.setupFingerprint,
            ProgramSnapshotFingerprint::calculateTools(project->operations(), entry.sourceOperationIds,
                [](int id) { return ToolLibrary::instance().tool(id); }), entry.toolFingerprint);
    };
    if (statusFor(program).state != ProgramSnapshotState::Changed) return 36;
    expectStale = true;
    QMetaObject::invokeMethod(&window, "onExportGCode");
    expectStale = false;
    if (staleMessages != 1 || fileDialogs != 0 || unexpectedMessages != 0) return 37;
    std::cerr << "LIFECYCLE edit: same ID, F456 depth0.3, old Changed, export blocked\n";
    QMetaObject::invokeMethod(operations, "onGenerateProgram");
    const auto regenerated = project->programs().last();
    if (regenerated.postProcessorId != "cq8" ||
        !regenerated.gcodeText.contains("Post-processor: CQ8 Macro B")) return 66;
    if (statusFor(regenerated).state != ProgramSnapshotState::Fresh ||
        !regenerated.gcodeText.contains("F456") || !regenerated.gcodeText.contains("Z-0.300")) return 38;
    QFile newNc(outputDir.filePath("cq8-lifecycle-edited.nc"));
    if (!newNc.open(QIODevice::WriteOnly)) return 39;
    newNc.write(regenerated.gcodeText.toUtf8()); newNc.close();
    std::cerr << "LIFECYCLE regenerate: Fresh F456 Z-0.300\n";
    lifecyclePath = QDir::current().absoluteFilePath(outputDir.filePath("cq8-lifecycle-" + edited.id + ".cnext"));
    fileDialogPath = lifecyclePath;
    QMetaObject::invokeMethod(&window, "onSaveProject");
    if (!QFile::exists(lifecyclePath) || project->isModified()) return 40;
    const auto setupFingerprint = project->setupFingerprint();
    QMetaObject::invokeMethod(&window, "onOpenProject");
    fileDialogPath.clear();
    if (project->operations().size() != 1 || !sameSlotOperation(edited, project->operations().first()) ||
        project->setupFingerprint() != setupFingerprint || project->programs().size() != 2 ||
        project->currentProgramId() != regenerated.id || project->programs().last().id != regenerated.id ||
        project->programs().last().gcodeText != regenerated.gcodeText ||
        project->programs().last().postProcessorId != "cq8" ||
        project->programs().first().postProcessorId != "cq8" ||
        project->programs().first().gcodeText != program.gcodeText ||
        statusFor(project->programs().last()).state != ProgramSnapshotState::Fresh || fileDialogs != 2) return 41;
    std::cerr << "LIFECYCLE MainWindow save/open: operation, evidence, geometry, parameters, Setup, program match\n";
    operations->selectOperationById(edited.id);
    if (!edit->isEnabled()) return 44;
    const int geometryBeforeReselect = geometryDialogs;
    reselectCancel = true;
    edit->click();
    reselectCancel = false;
    if (project->operations().size() != 1 || !sameSlotOperation(edited, project->operations().first()) ||
        project->isModified() || geometryDialogs != geometryBeforeReselect + 1 || selectionRequiredMessages != 1 || unexpectedMessages != 0) return 42;
    std::cerr << "LIFECYCLE reselection cancel: F777 not committed, original evidence preserved in stored operation\n";
    QFile lifecycleReceipt(outputDir.filePath("cq8-lifecycle-result.json"));
    if (!lifecycleReceipt.open(QIODevice::WriteOnly)) return 43;
    lifecycleReceipt.write(QJsonDocument(QJsonObject{{"model_sha256", hash}, {"operation_id", edited.id},
        {"operation_count", project->operations().size()}, {"selection_id", edited.selectionEvidence.id},
        {"geometry_refs", QJsonArray::fromStringList(edited.geometryRefs)},
        {"params", QJsonObject::fromVariantMap(edited.params.values)}, {"old_snapshot_changed", true},
        {"stale_export_warnings", staleMessages}, {"output_file_dialogs_before_regenerate", 0},
        {"new_program_id", regenerated.id}, {"post_processor_id", regenerated.postProcessorId},
        {"cq8_header_verified", true}, {"main_toolbar_selection_verified", true}, {"new_snapshot_fresh", true}, {"mainwindow_save_open", true}, {"project_path", lifecyclePath},
        {"create_geometry_cancel_zero", true}, {"create_parameter_cancel_zero", true},
        {"edit_cancel_unchanged", true}, {"reselect_cancel_no_commit", true},
        {"file_dialogs", fileDialogs}, {"selection_required_warnings", selectionRequiredMessages}, {"unexpected_messages", unexpectedMessages},
        {"source_bottom_z", sourceSlot.center.z()}, {"contour_top_z", edited.contourFeature.center.z()},
        {"first_target_z", -0.5}, {"edited_target_z", -0.3}, {"machining_safety_accepted", false}}).toJson());
    fileDialogPath = projectDir.filePath("tests/fixtures/slot-b2v-legacy-wrong-z.cnext");
    QMetaObject::invokeMethod(&window, "onOpenProject");
    fileDialogPath.clear();
    if (project->operations().size() != 1 || qAbs(project->operations().first().contourFeature.center.z() + 2.5) > 1e-5) return 50;
    const auto legacy = project->operations().first();
    const auto legacyStatus = statusFor(project->programs().last());
    std::cerr << "LEGACY state=" << int(legacyStatus.state) << " top=" << legacy.contourFeature.center.z() << '\n';
    if (legacyStatus.state != ProgramSnapshotState::Changed) return 51;
    expectStale = true;
    const int oldDialogs = fileDialogs, oldStale = staleMessages;
    QMetaObject::invokeMethod(&window, "onExportGCode");
    QMetaObject::invokeMethod(&window, "onSendToMachine");
    expectStale = false;
    if (fileDialogs != oldDialogs || staleMessages != oldStale + 2) return 52;
    const int oldPrograms = project->programs().size();
    expectLegacyGeometry = true;
    operations->generateProgramRequested(project->operations());
    expectLegacyGeometry = false;
    if (project->programs().size() != oldPrograms || legacyGeometryMessages != 1 || unexpectedMessages != 0) return 53;
    operations->recalculateRequested(project->operations());
    if (project->operations().first().toolpathState != ToolpathState::Error ||
        !sameSlotGeometry(legacy.contourFeature, project->operations().first().contourFeature)) return 54;
    std::cerr << "LEGACY rejected: Changed, output gated, generation no snapshot, recalculation Error, geometry unchanged\n";
    std::cout << "PASS real lifecycle and operation " << project->operations().first().id.toStdString() << '\n';
    return 0;
}
