#include "../src/app/MainWindow.h"
#include "../src/app/AppController.h"
#include "../src/core/ProjectManager.h"
#include "../src/core/SetupOrigin.h"
#include "../src/core/StockDefinition.h"
#include "../src/postprocessor/FanucPostProcessor.h"
#include "../src/strategies/StrategyFactory.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/workspaces/process_design/ui/HoleSelectionDialog.h"
#include "../src/workspaces/process_design/ui/HoleFeatureGrouping.h"
#include "../src/workspaces/process_design/ui/FeatureListPanel.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include "../src/workspaces/process_design/selection/FeatureIdentity.h"
#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QPainter>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTimer>

#include <cmath>
#include <functional>
#include <iostream>

static void settle(int milliseconds = 300)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

static bool setFeatureChecked(QTreeWidget *tree, int featureIndex, bool checked)
{
    if (!tree) return false;
    std::function<bool(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        if (!item) return false;
        if (item->childCount() == 0 &&
            item->data(0, Qt::UserRole).toInt() == featureIndex) {
            item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
            return true;
        }
        for (int i = 0; i < item->childCount(); ++i)
            if (visit(item->child(i))) return true;
        return false;
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (visit(tree->topLevelItem(i))) return true;
    return false;
}

static void clearFeatureChecks(QTreeWidget *tree)
{
    if (!tree) return;
    std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        if (!item) return;
        if (item->childCount() == 0 && (item->flags() & Qt::ItemIsUserCheckable))
            item->setCheckState(0, Qt::Unchecked);
        for (int i = 0; i < item->childCount(); ++i) visit(item->child(i));
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i) visit(tree->topLevelItem(i));
}

static void saveWorkflowScreenshot(QWidget &window, QWidget *parameterWindow,
                                   const QString &path)
{
    QPixmap canvas = window.grab();
    const QPixmap dialog = parameterWindow ? parameterWindow->grab() : QPixmap();
    if (!dialog.isNull()) {
        const QPoint position((canvas.width() - dialog.width()) / 2,
                              (canvas.height() - dialog.height()) / 2);
        QPainter painter(&canvas);
        painter.fillRect(QRect(position + QPoint(8, 8), dialog.size()),
                         QColor(0, 0, 0, 55));
        painter.drawPixmap(position, dialog);
    }
    canvas.save(path);
}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc, argv);
    if (argc < 3) {
        std::cerr << "usage: g73_real_hole_ui_test model.step output-dir [target-exe]\n";
        return 1;
    }

    const QString modelPath = QString::fromLocal8Bit(argv[1]);
    const QString outputDir = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(outputDir);

    ToolEntry drill;
    drill.id = 973;
    drill.type = QStringLiteral("drill");
    drill.name = QStringLiteral("G73 real-hole fixture drill");
    drill.diameter = 3.0;
    drill.fluteLen = 80.0;
    drill.totalLen = 120.0;
    ToolLibrary::instance().addTool(drill);

    ToolEntry boringBar;
    boringBar.id = 976;
    boringBar.type = QStringLiteral("boring_bar");
    boringBar.name = QStringLiteral("G76/G86 real-hole fixture boring bar");
    boringBar.diameter = 3.0;
    boringBar.fluteLen = 80.0;
    boringBar.totalLen = 120.0;
    ToolLibrary::instance().addTool(boringBar);

    MainWindow window;
    window.resize(1500, 900);
    window.show();
    std::cerr << "STAGE window-ready" << std::endl;
    AppController &controller = AppController::instance();
    std::cerr << "STAGE import-start" << std::endl;
    controller.importStep(modelPath);
    std::cerr << "STAGE import-returned" << std::endl;
    settle(500);

    ProjectManager *project = controller.projectManager();
    if (!project || project->mesh().isEmpty()) {
        std::cerr << "FAIL real STEP import produced no mesh\n";
        return 2;
    }
    int holeCount = 0;
    for (const MachiningFeature &feature : project->features()) {
        if (feature.kind == FeatureKind::Hole || feature.kind == FeatureKind::Thread) {
            ++holeCount;
        }
    }
    if (holeCount == 0) {
        std::cerr << "FAIL real STEP import produced no hole features\n";
        return 3;
    }
    std::cerr << "STAGE holes=" << holeCount << std::endl;

    project->setActiveRegion(FaceRegion::Front);
    SetupOrigin origin;
    origin.confirmed = true;
    project->setSetupOrigin(origin);
    StockDefinition stock;
    stock.confirmed = true;
    project->setStockDefinition(stock);
    if (project->setupFingerprint().isEmpty()) {
        std::cerr << "FAIL test setup was not confirmed\n";
        return 4;
    }
    QMetaObject::invokeMethod(&window, "onStepImported", Qt::DirectConnection,
                              Q_ARG(QString, modelPath));
    settle();
    project = controller.projectManager();

    QToolButton *g73Button = nullptr;
    for (QToolButton *button : window.findChildren<QToolButton *>()) {
        if (button->text() == QStringLiteral("G73")) {
            g73Button = button;
            break;
        }
    }
    if (!g73Button || !g73Button->isEnabled()) {
        std::cerr << "FAIL G73 command is missing or disabled\n";
        return 5;
    }
    QString emptySelectionMessage;
    QTimer::singleShot(150, [&] {
        if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            emptySelectionMessage = message->text();
            message->accept();
        }
    });
    g73Button->click();
    if (!emptySelectionMessage.contains(QStringLiteral("请先在左侧孔列表勾选加工点"))) {
        std::cerr << "FAIL empty sidebar selection was not blocked with the required message\n";
        return 5;
    }

    FeatureListPanel *featurePanel = window.findChild<FeatureListPanel *>();
    QTreeWidget *featureTree = featurePanel ? featurePanel->findChild<QTreeWidget *>() : nullptr;
    int checkedHoleIndex = -1;
    if (featureTree) {
        for (int i = 0; i < project->features().size(); ++i) {
            const auto &feature = project->features().at(i);
            if ((feature.kind == FeatureKind::Hole || feature.kind == FeatureKind::Thread) &&
                feature.depth > 0.0) {
                checkedHoleIndex = i;
                break;
            }
        }
        setFeatureChecked(featureTree, checkedHoleIndex, true);
    }
    if (checkedHoleIndex < 0) {
        std::cerr << "FAIL real STEP sidebar did not expose a valid hole checkbox\n";
        return 5;
    }

    bool secondaryDialogSeen = false;
    QTimer::singleShot(500, [&] {
        for (QDialog *dialog : window.findChildren<QDialog *>()) {
            if (auto *candidate = dynamic_cast<HoleSelectionDialog *>(dialog)) {
                if (candidate->isVisible()) {
                    secondaryDialogSeen = true;
                    candidate->reject();
                }
            }
        }
    });
    g73Button->click();
    std::cerr << "STAGE g73-clicked" << std::endl;
    settle();

    HoleSelectionDialog *selection = nullptr;
    for (QDialog *dialog : window.findChildren<QDialog *>()) {
        if (auto *candidate = dynamic_cast<HoleSelectionDialog *>(dialog)) {
            selection = candidate;
            break;
        }
    }
    if (secondaryDialogSeen || (selection && selection->isVisible())) {
        std::cerr << "FAIL G73 command reopened HoleSelectionDialog after sidebar selection\n";
        return 6;
    }
    if (!window.findChild<StrategyPanel *>()->isVisible()) {
        std::cerr << "FAIL G73 command did not open parameters from sidebar selection\n";
        return 7;
    }
    saveWorkflowScreenshot(window, window.findChild<StrategyPanel *>()->window(),
                           outputDir + QStringLiteral("/sidebar-g73-parameters.png"));
    std::cerr << "STAGE parameters-open" << std::endl;

    StrategyPanel *panel = window.findChild<StrategyPanel *>();
    QComboBox *strategyCombo = panel ? panel->findChild<QComboBox *>() : nullptr;
    if (!panel || !panel->isVisible() || !strategyCombo ||
        strategyCombo->currentData().toString() != QStringLiteral("hole_g73")) {
        std::cerr << "FAIL real hole selection did not enter G73 parameters\n";
        return 9;
    }
    StrategyParams draft = panel->parameterDraft();
    draft.set(QStringLiteral("depth"), 5.0);
    draft.set(QStringLiteral("peckDepth"), 2.0);
    draft.set(QStringLiteral("safeHeight"), 25.0);
    draft.set(QStringLiteral("feedHeight"), 3.0);
    draft.set(QStringLiteral("spindleSpeed"), 600.0);
    draft.set(QStringLiteral("feedRate"), 40.0);
    panel->restoreDraft(draft, drill.id);
    settle();
    QPushButton *confirmButton = nullptr;
    for (QPushButton *button : panel->findChildren<QPushButton *>()) {
        if (button->text().contains(QStringLiteral("确认并加入工序"))) {
            confirmButton = button;
            break;
        }
    }
    if (!confirmButton || !confirmButton->isEnabled()) {
        std::cerr << "FAIL G73 parameter panel did not enable operation confirmation\n";
        return 10;
    }
    QTimer::singleShot(500, [] {
        if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            if (auto *yes = message->button(QMessageBox::Yes)) {
                yes->click();
            } else {
                message->accept();
            }
        }
    });
    confirmButton->click();
    settle();
    if (!project || project->operations().size() != 1) {
        std::cerr << "FAIL real G73 confirmation did not create one operation\n";
        return 11;
    }
    const MachiningOperation confirmed = project->operations().first();
    if (confirmed.strategyId != QStringLiteral("hole_g73") ||
        std::abs(confirmed.params.get(QStringLiteral("depth")) - 5.0) > 1.0e-6) {
        std::cerr << "FAIL confirmed real operation did not preserve G73 depth 5\n";
        return 12;
    }

    ProgramGenerationService generator(
        [](const QString &id) { return StrategyFactory::instance().strategy(id); },
        [](int id) { return ToolLibrary::instance().tool(id); });
    PostProcessorOptions postOptions;
    postOptions.programNumber = QStringLiteral("O7302");
    postOptions.workOffset = QStringLiteral("G54");
    postOptions.safeStartBlocks = QStringList{QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
                                              QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    ProgramGenerationSnapshotOptions snapshotOptions;
    snapshotOptions.name = QStringLiteral("G73 real hole UI");
    snapshotOptions.machineProfile.id = QStringLiteral("fanuc-0i");
    snapshotOptions.sourceFingerprint = project->sourceFileFingerprint();
    snapshotOptions.setupFingerprint = project->setupFingerprint();
    FanucPostProcessor fanuc;
    const ProgramGenerationResult generated = generator.generate(
        project->operations(), fanuc, postOptions, snapshotOptions);
    QFile finalNc(outputDir + QStringLiteral("/g73-real-hole-final.nc"));
    if (finalNc.open(QIODevice::WriteOnly | QIODevice::Text)) {
        finalNc.write(generated.snapshot.gcodeText.toUtf8());
    }
    const QRegularExpression cyclePattern(QStringLiteral("G73 Z(-?[0-9.]+) R(-?[0-9.]+)"));
    const QRegularExpressionMatch cycleMatch = cyclePattern.match(generated.snapshot.gcodeText);
    const double generatedBottom = cycleMatch.hasMatch() ? cycleMatch.captured(1).toDouble() : 0.0;
    const double generatedRetract = cycleMatch.hasMatch() ? cycleMatch.captured(2).toDouble() : 0.0;
    const bool explicitDepthReached = cycleMatch.hasMatch() &&
        std::abs(generatedBottom - (generatedRetract - 3.0 - 5.0)) < 1.0e-3;
    if (!generated.ok || !explicitDepthReached) {
        std::cerr << generated.snapshot.gcodeText.toStdString() << std::endl;
        std::cerr << "FAIL real G73 operation did not generate the requested 5 mm cut depth\n";
        return 13;
    }

    SimulationController simulation;
    QVector<QVector3D> path;
    QVector<bool> rapid;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&path, &rapid](const QVector<QVector3D> &points,
                                     const QVector<bool> &rapidMoves) {
        path = points;
        rapid = rapidMoves;
    });
    simulation.loadGCode(generated.snapshot.gcodeText);
    bool reachesUserDepth = false;
    for (int i = 0; i < rapid.size(); ++i) {
        if (!rapid.at(i) && std::abs(path.at(i + 1).z() - generatedBottom) < 1.0e-3f) {
            reachesUserDepth = true;
        }
    }
    if (!reachesUserDepth) {
        std::cerr << "FAIL real G73 final NC simulation did not reach the requested bottom\n";
        return 14;
    }

    QTemporaryDir projectDir;
    if (!projectDir.isValid()) {
        std::cerr << "FAIL temporary project directory unavailable\n";
        return 15;
    }
    project->setPrograms({generated.snapshot});
    const QString projectPath = QDir(projectDir.path()).filePath(QStringLiteral("g73-real.json"));
    if (!project->saveToFile(projectPath)) {
        std::cerr << "FAIL real G73 project save failed\n";
        return 16;
    }
    ProjectManager reopened;
    if (!reopened.loadFromFile(projectPath) || reopened.operations().size() != 1 ||
        reopened.operations().first().strategyId != QStringLiteral("hole_g73") ||
        std::abs(reopened.operations().first().params.get(QStringLiteral("depth")) - 5.0) > 1.0e-6) {
        std::cerr << "FAIL real G73 save/reopen did not preserve operation and depth\n";
        return 17;
    }
    const ProgramGenerationResult reopenedProgram = generator.generate(
        reopened.operations(), fanuc, postOptions, snapshotOptions);
    if (!reopenedProgram.ok || !reopenedProgram.snapshot.gcodeText.contains(
            QStringLiteral("G73 Z%1").arg(generatedBottom, 0, 'f', 3))) {
        std::cerr << "FAIL reopened real G73 operation did not regenerate final NC\n";
        return 18;
    }
    panel->grab().save(outputDir + QStringLiteral("/g73-real-hole-parameters.png"));

    QToolButton *g81Button = nullptr;
    QToolButton *g83Button = nullptr;
    QToolButton *g76Button = nullptr;
    QToolButton *g86Button = nullptr;
    QToolButton *spotButton = nullptr;
    QToolButton *chamferButton = nullptr;
    QToolButton *tapButton = nullptr;
    for (QToolButton *button : window.findChildren<QToolButton *>()) {
        if (button->text() == QStringLiteral("G81")) {
            g81Button = button;
        } else if (button->text() == QStringLiteral("G83")) {
            g83Button = button;
        } else if (button->text() == QStringLiteral("G76")) {
            g76Button = button;
        } else if (button->text() == QStringLiteral("G86")) {
            g86Button = button;
        } else if (button->text() == QStringLiteral("打点")) {
            spotButton = button;
        } else if (button->text() == QStringLiteral("孔倒角")) {
            chamferButton = button;
        } else if (button->text() == QStringLiteral("攻牙")) {
            tapButton = button;
        }
    }
    OperationListPanel *operationPanel = window.findChild<OperationListPanel *>();
    if (!g81Button || !g83Button || !g76Button || !g86Button || !spotButton ||
        !chamferButton || !tapButton || !operationPanel) {
        std::cerr << "FAIL hole-cycle command or operation panel is unavailable\n";
        return 19;
    }
    QVector<int> compatibleHoleIndices;
    for (int i = 0; i < project->features().size(); ++i) {
        const auto &feature = project->features().at(i);
        if ((feature.kind == FeatureKind::Hole || feature.kind == FeatureKind::Thread) &&
            feature.depth > 0.0 && feature.region != FaceRegion::Side) {
            compatibleHoleIndices.append(i);
            if (compatibleHoleIndices.size() == 2) break;
        }
    }
    if (compatibleHoleIndices.size() < 2) {
        std::cerr << "FAIL real STEP does not provide two usable holes for G81\n";
        return 20;
    }
    QVector<int> sameGroupHoleIndices;
    for (int i = 0; i < project->features().size() && sameGroupHoleIndices.isEmpty(); ++i) {
        const MachiningFeature &first = project->features().at(i);
        const QString groupId = holeFeatureGroupId(first);
        if (groupId.isEmpty() || first.depth <= 0.0 || first.region == FaceRegion::Side) continue;
        for (int j = i + 1; j < project->features().size(); ++j) {
            const MachiningFeature &second = project->features().at(j);
            if (second.depth > 0.0 && second.region != FaceRegion::Side &&
                holeFeatureGroupId(second) == groupId) {
                sameGroupHoleIndices = {i, j};
                break;
            }
        }
    }
    if (sameGroupHoleIndices.size() != 2) {
        std::cerr << "FAIL real STEP does not provide two same-group holes for G76/G86\n";
        return 20;
    }
    const MachiningFeature &completionFeature =
        project->features().at(sameGroupHoleIndices.first());
    const double completionDiameter = completionFeature.radius * 2.0;
    if (completionDiameter <= 0.0) {
        std::cerr << "FAIL same-group completion holes have no valid diameter\n";
        return 20;
    }
    ToolEntry reamer;
    reamer.id = 985;
    reamer.type = QStringLiteral("reamer");
    reamer.name = QStringLiteral("Completion fixture reamer");
    reamer.diameter = completionDiameter;
    reamer.fluteLen = 80.0;
    reamer.totalLen = 120.0;
    ToolLibrary::instance().addTool(reamer);
    ToolEntry spotDrill;
    spotDrill.id = 983;
    spotDrill.type = QStringLiteral("spot_drill");
    spotDrill.name = QStringLiteral("Completion fixture spot drill");
    spotDrill.diameter = completionDiameter * 0.5;
    spotDrill.fluteLen = 80.0;
    spotDrill.totalLen = 120.0;
    ToolLibrary::instance().addTool(spotDrill);
    ToolEntry tap;
    tap.id = 984;
    tap.type = QStringLiteral("tap");
    tap.name = QStringLiteral("Completion fixture tap");
    tap.diameter = completionDiameter;
    tap.fluteLen = 80.0;
    tap.totalLen = 120.0;
    tap.pitch = completionFeature.pitch > 0.0 ? completionFeature.pitch : 1.0;
    ToolLibrary::instance().addTool(tap);
    ToolEntry chamferMill;
    chamferMill.id = 986;
    chamferMill.type = QStringLiteral("chamfer_mill");
    chamferMill.name = QStringLiteral("Completion fixture chamfer mill");
    chamferMill.diameter = completionDiameter * 0.5;
    chamferMill.fluteLen = 80.0;
    chamferMill.totalLen = 120.0;
    chamferMill.pointAngle = 90.0;
    chamferMill.extra.insert(QStringLiteral("angle"), 90.0);
    ToolLibrary::instance().addTool(chamferMill);
    ToolEntry endMill;
    endMill.id = 987;
    endMill.type = QStringLiteral("end_mill");
    endMill.name = QStringLiteral("Completion fixture circular-hole end mill");
    endMill.diameter = completionDiameter * 0.5;
    endMill.fluteLen = 80.0;
    endMill.totalLen = 120.0;
    ToolLibrary::instance().addTool(endMill);
    int nonHoleIndex = -1;
    for (int i = 0; i < project->features().size(); ++i) {
        const auto &feature = project->features().at(i);
        if (feature.kind != FeatureKind::Hole && feature.kind != FeatureKind::Thread) {
            nonHoleIndex = i;
            break;
        }
    }
    if (nonHoleIndex < 0) {
        std::cerr << "FAIL real STEP does not provide a non-hole feature for mixed-selection coverage\n";
        return 20;
    }

    clearFeatureChecks(featureTree);
    if (!setFeatureChecked(featureTree, compatibleHoleIndices.first(), true)) {
        std::cerr << "FAIL could not prepare spot-button separation check\n";
        return 20;
    }
    QTimer::singleShot(500, [] {
        if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            if (auto *yes = message->button(QMessageBox::Yes)) {
                yes->click();
            } else {
                message->accept();
            }
        }
    });
    spotButton->click();
    settle();
    QComboBox *spotCombo = panel->findChild<QComboBox *>();
    if (!spotCombo || spotCombo->currentData().toString() != QStringLiteral("hole_spot")) {
        std::cerr << "FAIL spot button did not open hole_spot\n";
        return 20;
    }
    for (QPushButton *button : panel->window()->findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("取消")) {
            button->click();
            break;
        }
    }
    settle();
    std::cerr << "STAGE button-separation spot=hole_spot g81=hole_g81" << std::endl;

    auto verifyG81Selection = [&](const QVector<int> &checkedIndices,
                                  const QVector<int> &expectedHoleIndices,
                                  int expectedCount,
                                  const QString &screenshotName,
                                  int injectedNonHoleIndex) {
        project->setOperations({});
        project->setPrograms({});
        operationPanel->setOperations({});
        clearFeatureChecks(featureTree);
        for (int index : checkedIndices) {
            if (!setFeatureChecked(featureTree, index, true)) {
                std::cerr << "G81 CHECK FAIL index=" << index << std::endl;
                return false;
            }
        }
        if (injectedNonHoleIndex >= 0) {
            auto *mixedItem = new QTreeWidgetItem;
            mixedItem->setText(0, QStringLiteral("测试混合勾选 · 非孔特征"));
            mixedItem->setData(0, Qt::UserRole, injectedNonHoleIndex);
            mixedItem->setFlags(mixedItem->flags() | Qt::ItemIsUserCheckable);
            mixedItem->setCheckState(0, Qt::Unchecked);
            featureTree->addTopLevelItem(mixedItem);
            mixedItem->setCheckState(0, Qt::Checked);
        }
        settle();
        QString workflowError;
        QTimer entryTimer;
        QObject::connect(&entryTimer, &QTimer::timeout, [&workflowError] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (auto *yes = message->button(QMessageBox::Yes)) {
                    yes->click();
                } else {
                    workflowError = message->text();
                    message->accept();
                }
            }
        });
        entryTimer.start(100);
        g81Button->click();
        settle();
        entryTimer.stop();
        if (!workflowError.isEmpty()) {
            std::cerr << "G81 WORKFLOW ERROR " << workflowError.toStdString() << std::endl;
            return false;
        }
        for (QDialog *dialog : window.findChildren<QDialog *>()) {
            if (auto *holes = dynamic_cast<HoleSelectionDialog *>(dialog)) {
                if (holes->isVisible()) {
                    std::cerr << "G81 SECONDARY DIALOG\n";
                    return false;
                }
            }
        }
        if (injectedNonHoleIndex >= 0) {
            bool ignoredStatusSeen = false;
            for (QLabel *label : window.findChildren<QLabel *>()) {
                if (label->text().contains(QStringLiteral("已忽略 1 个非孔勾选项"))) {
                    ignoredStatusSeen = true;
                    break;
                }
            }
            if (!ignoredStatusSeen) {
                std::cerr << "G81 MIXED FILTER STATUS FAIL\n";
                return false;
            }
        }
        QComboBox *combo = panel->findChild<QComboBox *>();
        if (!panel->isVisible() || !combo ||
            combo->currentData().toString() != QStringLiteral("hole_g81")) {
            std::cerr << "G81 PARAM FAIL visible=" << panel->isVisible()
                      << " strategy=" << (combo ? combo->currentData().toString().toStdString() : "null") << std::endl;
            return false;
        }
        std::cerr << "STAGE g81-strategy=hole_g81" << std::endl;
        saveWorkflowScreenshot(window, panel->window(),
                               outputDir + QLatin1Char('/') + screenshotName);
        StrategyParams spotDraft = panel->parameterDraft();
        spotDraft.set(QStringLiteral("depth"), 5.0);
        spotDraft.set(QStringLiteral("safeHeight"), 25.0);
        spotDraft.set(QStringLiteral("feedHeight"), 3.0);
        spotDraft.set(QStringLiteral("spindleSpeed"), 600.0);
        spotDraft.set(QStringLiteral("feedRate"), 40.0);
        spotDraft.set(QStringLiteral("dwellTime"), 0.0);
        panel->restoreDraft(spotDraft, drill.id);
        QPushButton *addButton = nullptr;
        for (QPushButton *button : panel->findChildren<QPushButton *>()) {
            if (button->text().contains(QStringLiteral("确认并加入工序"))) {
                addButton = button;
                break;
            }
        }
        if (!addButton || !addButton->isEnabled()) {
            std::cerr << "G81 CONFIRM DISABLED\n";
            return false;
        }
        QString confirmationError;
        auto *compatibilityTimer = new QTimer(&window);
        QObject::connect(compatibilityTimer, &QTimer::timeout, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (auto *yes = message->button(QMessageBox::Yes)) {
                    yes->click();
                } else {
                    confirmationError = message->text();
                    message->accept();
                }
            }
        });
        compatibilityTimer->start(100);
        addButton->click();
        settle();
        compatibilityTimer->stop();
        compatibilityTimer->deleteLater();
        if (!confirmationError.isEmpty()) {
            std::cerr << "G81 CONFIRM ERROR " << confirmationError.toStdString() << std::endl;
            return false;
        }
        if (project->operations().size() != expectedCount) {
            std::cerr << "G81 OP COUNT actual=" << project->operations().size()
                      << " expected=" << expectedCount << std::endl;
            return false;
        }
        QSet<QString> expectedRefs;
        for (int index : expectedHoleIndices)
            expectedRefs.insert(stableFeatureId(project->features().at(index)));
        QSet<QString> actualRefs;
        for (const MachiningOperation &operation : project->operations()) {
            if (operation.strategyId != QStringLiteral("hole_g81") ||
                std::abs(operation.params.get(QStringLiteral("depth")) - 5.0) > 1.0e-6 ||
                operation.geometryRefs.size() != 1) {
                std::cerr << "G81 OP ID/REF FAIL\n";
                return false;
            }
            actualRefs.insert(operation.geometryRefs.first());
        }
        if (actualRefs != expectedRefs) {
            std::cerr << "G81 REFS FAIL actual=" << actualRefs.values().join('|').toStdString()
                      << " expected=" << expectedRefs.values().join('|').toStdString() << std::endl;
            return false;
        }
        const ProgramGenerationResult program = generator.generate(
            project->operations(), fanuc, postOptions, snapshotOptions);
        const int g81Count = program.snapshot.gcodeText.count(QStringLiteral("G81 Z"));
        if (!program.ok || g81Count != expectedCount) {
            std::cerr << "G81 NC FAIL ok=" << program.ok << " count=" << g81Count
                      << " expected=" << expectedCount << std::endl;
            return false;
        }
        for (int index : expectedHoleIndices) {
            const QVector3D center = project->features().at(index).center;
            const QString xy = QStringLiteral("X%1 Y%2")
                .arg(center.x(), 0, 'f', 3)
                .arg(center.y(), 0, 'f', 3);
            if (!program.snapshot.gcodeText.contains(xy)) {
                std::cerr << "G81 NC POSITION FAIL index=" << index
                          << " expected=" << xy.toStdString() << std::endl;
                return false;
            }
        }
        return true;
    };

    if (!verifyG81Selection({compatibleHoleIndices.first()},
                            {compatibleHoleIndices.first()}, 1,
                            QStringLiteral("sidebar-g81-one-hole.png"), -1)) {
        std::cerr << "FAIL sidebar single-hole G81 workflow changed the selected target set\n";
        return 21;
    }
    if (!verifyG81Selection(compatibleHoleIndices, compatibleHoleIndices, 2,
                            QStringLiteral("sidebar-g81-two-holes.png"), -1)) {
        std::cerr << "FAIL sidebar two-hole G81 workflow changed the selected target set\n";
        return 22;
    }
    if (!verifyG81Selection({compatibleHoleIndices.first()},
                            {compatibleHoleIndices.first()}, 1,
                            QStringLiteral("sidebar-g81-mixed.png"), nonHoleIndex)) {
        std::cerr << "FAIL mixed sidebar selection did not filter and report the non-hole item\n";
        return 23;
    }
    std::cerr << "STAGE mixed-filtered=1 holes=1" << std::endl;

    auto verifyBoringSelection = [&](QToolButton *cycleButton,
                                     const QString &strategyId,
                                     const QString &cycleCode,
                                     const QVector<int> &holeIndices,
                                     const QString &artifactStem) {
        project->setOperations({});
        project->setPrograms({});
        operationPanel->setOperations({});
        clearFeatureChecks(featureTree);
        for (int index : holeIndices) {
            if (!setFeatureChecked(featureTree, index, true)) {
                std::cerr << cycleCode.toStdString() << " CHECK FAIL index=" << index << std::endl;
                return false;
            }
        }

        QString entryError;
        QTimer entryTimer;
        QObject::connect(&entryTimer, &QTimer::timeout, [&entryError] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                entryError = message->text();
                message->accept();
            }
        });
        entryTimer.start(100);
        cycleButton->click();
        settle();
        entryTimer.stop();
        if (!entryError.isEmpty()) {
            std::cerr << cycleCode.toStdString() << " ENTRY ERROR "
                      << entryError.toStdString() << std::endl;
            return false;
        }
        QComboBox *combo = panel->findChild<QComboBox *>();
        if (!panel->isVisible() || !combo || combo->currentData().toString() != strategyId) {
            std::cerr << cycleCode.toStdString() << " PARAM FAIL\n";
            return false;
        }

        StrategyParams boringDraft = panel->parameterDraft();
        boringDraft.set(QStringLiteral("depth"), 5.0);
        boringDraft.set(QStringLiteral("safeHeight"), 25.0);
        boringDraft.set(QStringLiteral("feedHeight"), 3.0);
        boringDraft.set(QStringLiteral("spindleSpeed"), 400.0);
        boringDraft.set(QStringLiteral("feedRate"), 40.0);
        boringDraft.set(QStringLiteral("coolantMode"), 1.0);
        if (cycleCode == QStringLiteral("G76")) {
            boringDraft.set(QStringLiteral("boringOffset"), 0.1);
        }
        panel->restoreDraft(boringDraft, boringBar.id);
        if (holeIndices.size() == 2) {
            saveWorkflowScreenshot(window, panel->window(),
                                   outputDir + QLatin1Char('/') + artifactStem + QStringLiteral("-parameters.png"));
        }

        QPushButton *addButton = nullptr;
        for (QPushButton *button : panel->findChildren<QPushButton *>()) {
            if (button->text().contains(QStringLiteral("确认并加入工序"))) {
                addButton = button;
                break;
            }
        }
        if (!addButton || !addButton->isEnabled()) {
            std::cerr << cycleCode.toStdString() << " CONFIRM DISABLED\n";
            return false;
        }
        QString confirmationError;
        QTimer confirmationTimer;
        QObject::connect(&confirmationTimer, &QTimer::timeout, [&confirmationError] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (auto *yes = message->button(QMessageBox::Yes)) {
                    yes->click();
                } else {
                    confirmationError = message->text();
                    message->accept();
                }
            }
        });
        confirmationTimer.start(100);
        addButton->click();
        settle();
        confirmationTimer.stop();
        if (!confirmationError.isEmpty()) {
            std::cerr << cycleCode.toStdString() << " CONFIRM ERROR "
                      << confirmationError.toStdString() << std::endl;
            return false;
        }
        if (project->operations().size() != holeIndices.size()) {
            std::cerr << cycleCode.toStdString() << " OP COUNT actual="
                      << project->operations().size() << " expected=" << holeIndices.size() << std::endl;
            return false;
        }

        QSet<QString> expectedRefs;
        for (int index : holeIndices) {
            expectedRefs.insert(stableFeatureId(project->features().at(index)));
        }
        QSet<QString> actualRefs;
        for (const MachiningOperation &operation : project->operations()) {
            if (operation.strategyId != strategyId || operation.toolId != boringBar.id ||
                std::abs(operation.params.get(QStringLiteral("depth")) - 5.0) > 1.0e-6 ||
                operation.geometryRefs.size() != 1) {
                std::cerr << cycleCode.toStdString() << " OP CONTRACT FAIL\n";
                return false;
            }
            actualRefs.insert(operation.geometryRefs.first());
        }
        if (actualRefs != expectedRefs) {
            std::cerr << cycleCode.toStdString() << " REFS FAIL\n";
            return false;
        }

        ProgramGenerationSnapshotOptions boringSnapshotOptions = snapshotOptions;
        boringSnapshotOptions.name = cycleCode + QStringLiteral(" real hole UI");
        const ProgramGenerationResult program = generator.generate(
            project->operations(), fanuc, postOptions, boringSnapshotOptions);
        QString cycleLine;
        for (const QString &line : program.snapshot.gcodeText.split(QLatin1Char('\n'))) {
            if (line.startsWith(QStringLiteral("G98 ") + cycleCode + QStringLiteral(" Z"))) {
                cycleLine = line;
                break;
            }
        }
        const QRegularExpression bottomPattern(
            QStringLiteral("G98 %1 Z(-?[0-9.]+) R(-?[0-9.]+)").arg(cycleCode));
        const QRegularExpressionMatch bottomMatch = bottomPattern.match(cycleLine);
        const double bottom = bottomMatch.hasMatch() ? bottomMatch.captured(1).toDouble() : 0.0;
        if (!program.ok || cycleLine.isEmpty() ||
            program.snapshot.gcodeText.count(QStringLiteral("G98 ") + cycleCode) != 1 ||
            !program.snapshot.gcodeText.contains(QStringLiteral("M8")) ||
            !program.snapshot.gcodeText.contains(QStringLiteral("G80\nG0 Z25.000")) ||
            (cycleCode == QStringLiteral("G76") && !cycleLine.contains(QStringLiteral("Q0.100"))) ||
            (cycleCode == QStringLiteral("G86") && cycleLine.contains(QLatin1Char('Q')))) {
            std::cerr << cycleCode.toStdString() << " FINAL NC FAIL\n"
                      << program.snapshot.gcodeText.toStdString() << std::endl;
            return false;
        }
        int lastHolePosition = -1;
        for (int index : holeIndices) {
            const QVector3D center = project->features().at(index).center;
            const QString xy = QStringLiteral("X%1 Y%2")
                .arg(center.x(), 0, 'f', 3)
                .arg(center.y(), 0, 'f', 3);
            const int position = program.snapshot.gcodeText.indexOf(xy);
            if (position < 0) {
                std::cerr << cycleCode.toStdString() << " NC POSITION FAIL "
                          << xy.toStdString() << std::endl;
                return false;
            }
            lastHolePosition = std::max(lastHolePosition, position);
        }
        if (cycleCode == QStringLiteral("G86") && holeIndices.size() == 2 &&
            program.snapshot.gcodeText.indexOf(QStringLiteral("M5"), lastHolePosition) < lastHolePosition) {
            std::cerr << "G86 stopped the spindle before completing the two-hole cycle\n";
            return false;
        }

        SimulationController boringSimulation;
        QVector<QVector3D> boringPath;
        QVector<bool> boringRapid;
        QObject::connect(&boringSimulation, &SimulationController::toolPathReady,
                         [&boringPath, &boringRapid](const QVector<QVector3D> &points,
                                                     const QVector<bool> &rapidMoves) {
            boringPath = points;
            boringRapid = rapidMoves;
        });
        boringSimulation.loadGCode(program.snapshot.gcodeText);
        if ((cycleCode == QStringLiteral("G76") &&
             !boringSimulation.hasUnverifiedG76LateralShift()) ||
            (cycleCode == QStringLiteral("G86") &&
             !boringSimulation.hasUnverifiedG86SpindleStopRetract())) {
            std::cerr << cycleCode.toStdString() << " SIMULATION LIMIT FLAG FAIL\n";
            return false;
        }
        for (int index : holeIndices) {
            const QVector3D center = project->features().at(index).center;
            bool simulatedBottom = false;
            for (int move = 0; move < boringRapid.size(); ++move) {
                const QVector3D end = boringPath.at(move + 1);
                if (!boringRapid.at(move) &&
                    std::abs(end.x() - center.x()) < 1.0e-3f &&
                    std::abs(end.y() - center.y()) < 1.0e-3f &&
                    std::abs(end.z() - bottom) < 1.0e-3f) {
                    simulatedBottom = true;
                    break;
                }
            }
            if (!simulatedBottom) {
                std::cerr << cycleCode.toStdString() << " SIMULATION POSITION/DEPTH FAIL index="
                          << index << std::endl;
                return false;
            }
        }

        if (holeIndices.size() == 2) {
            QFile ncFile(outputDir + QLatin1Char('/') + artifactStem + QStringLiteral("-final.nc"));
            if (!ncFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
                ncFile.write(program.snapshot.gcodeText.toUtf8()) <= 0) {
                std::cerr << cycleCode.toStdString() << " NC ARTIFACT WRITE FAIL\n";
                return false;
            }
            project->setPrograms({program.snapshot});
            const QString savedPath = QDir(projectDir.path()).filePath(artifactStem + QStringLiteral(".json"));
            if (!project->saveToFile(savedPath)) {
                std::cerr << cycleCode.toStdString() << " SAVE FAIL\n";
                return false;
            }
            ProjectManager reopenedBoring;
            if (!reopenedBoring.loadFromFile(savedPath) ||
                reopenedBoring.operations().size() != holeIndices.size()) {
                std::cerr << cycleCode.toStdString() << " REOPEN FAIL\n";
                return false;
            }
            for (const MachiningOperation &operation : reopenedBoring.operations()) {
                if (operation.strategyId != strategyId || operation.toolId != boringBar.id ||
                    std::abs(operation.params.get(QStringLiteral("depth")) - 5.0) > 1.0e-6) {
                    std::cerr << cycleCode.toStdString() << " REOPEN CONTRACT FAIL\n";
                    return false;
                }
            }
            const ProgramGenerationResult reopenedProgram = generator.generate(
                reopenedBoring.operations(), fanuc, postOptions, boringSnapshotOptions);
            if (!reopenedProgram.ok ||
                reopenedProgram.snapshot.gcodeText.count(QStringLiteral("G98 ") + cycleCode) != 1) {
                std::cerr << cycleCode.toStdString() << " REOPEN REGENERATION FAIL\n";
                return false;
            }
        }
        std::cerr << "STAGE " << cycleCode.toStdString()
                  << " real-sidebar holes=" << holeIndices.size()
                  << " bottom=" << bottom << std::endl;
        return true;
    };

    if (!verifyBoringSelection(g76Button, QStringLiteral("hole_g76"), QStringLiteral("G76"),
                               {sameGroupHoleIndices.first()}, QStringLiteral("fanuc-g76-one-hole")) ||
        !verifyBoringSelection(g76Button, QStringLiteral("hole_g76"), QStringLiteral("G76"),
                               sameGroupHoleIndices, QStringLiteral("fanuc-g76-two-hole"))) {
        std::cerr << "FAIL real sidebar G76 single/two-hole workflow\n";
        return 24;
    }
    if (!verifyBoringSelection(g86Button, QStringLiteral("hole_g86"), QStringLiteral("G86"),
                               {sameGroupHoleIndices.first()}, QStringLiteral("fanuc-g86-one-hole")) ||
        !verifyBoringSelection(g86Button, QStringLiteral("hole_g86"), QStringLiteral("G86"),
                               sameGroupHoleIndices, QStringLiteral("fanuc-g86-two-hole"))) {
        std::cerr << "FAIL real sidebar G86 single/two-hole workflow\n";
        return 25;
    }

    struct CompletionCase {
        QToolButton *entryButton = nullptr;
        QString strategyId;
        QString cycleCode;
        int toolId = 0;
        QString artifactStem;
        double expectedDrop = 5.0;
        bool hasDepthParam = true;
        int chamferMode = -1;
    };
    const QVector<CompletionCase> completionCases{
        {spotButton, QStringLiteral("hole_spot"), QStringLiteral("G81"), spotDrill.id,
         QStringLiteral("fanuc-spot-g81"), 5.0, true},
        {g81Button, QStringLiteral("hole_g81"), QStringLiteral("G81"), drill.id,
         QStringLiteral("fanuc-g81-drilling"), 5.0, true},
        {g73Button, QStringLiteral("hole_g73"), QStringLiteral("G73"), drill.id,
         QStringLiteral("fanuc-g73-chip-break"), 5.0, true},
        {g83Button, QStringLiteral("hole_peck"), QStringLiteral("G83"), drill.id,
         QStringLiteral("fanuc-g83-peck"), 5.0, true},
        {g83Button, QStringLiteral("hole_deephole"), QStringLiteral("G83"), drill.id,
         QStringLiteral("fanuc-g83-deep-hole"), 5.0, true},
        {g83Button, QStringLiteral("hole_reaming"), QStringLiteral("G85"), reamer.id,
         QStringLiteral("fanuc-g85-reaming"), 5.0, true},
        {tapButton, QStringLiteral("hole_tapping"), QStringLiteral("G84"), tap.id,
         QStringLiteral("fanuc-g84-tapping"), 5.0, true},
        {chamferButton, QStringLiteral("hole_chamfer"), QString(), chamferMill.id,
         QStringLiteral("fanuc-circumferential-hole-chamfer"), 1.0, false, 0},
        {chamferButton, QStringLiteral("hole_chamfer"), QStringLiteral("G81"), chamferMill.id,
         QStringLiteral("fanuc-axial-g81-countersink"), 2.0, false, 1},
        {g83Button, QStringLiteral("hole_circular_mill"), QString(), endMill.id,
         QStringLiteral("fanuc-hole-circular-mill"), 5.0, true},
    };

    auto verifyCompletionSelection = [&](const CompletionCase &item,
                                         const QVector<int> &holeIndices,
                                         const QString &countName) {
        project->setOperations({});
        project->setPrograms({});
        operationPanel->setOperations({});
        clearFeatureChecks(featureTree);
        for (int index : holeIndices) {
            if (!setFeatureChecked(featureTree, index, true)) {
                std::cerr << item.strategyId.toStdString() << " CHECK FAIL index=" << index << std::endl;
                return false;
            }
        }

        QString entryError;
        QTimer entryTimer;
        QObject::connect(&entryTimer, &QTimer::timeout, [&entryError] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (auto *yes = message->button(QMessageBox::Yes)) {
                    yes->click();
                } else {
                    entryError = message->text();
                    message->accept();
                }
            }
        });
        entryTimer.start(100);
        item.entryButton->click();
        settle();
        entryTimer.stop();
        if (!entryError.isEmpty() || !panel->selectStrategy(item.strategyId)) {
            std::cerr << item.strategyId.toStdString() << " ENTRY/SELECT FAIL "
                      << entryError.toStdString() << std::endl;
            return false;
        }
        settle();
        QComboBox *combo = panel->findChild<QComboBox *>();
        if (!panel->isVisible() || !combo || combo->currentData().toString() != item.strategyId) {
            std::cerr << item.strategyId.toStdString() << " PARAM FAIL\n";
            return false;
        }

        StrategyParams draft = panel->parameterDraft();
        if (item.hasDepthParam) draft.set(QStringLiteral("depth"), 5.0);
        draft.set(QStringLiteral("safeHeight"), 25.0);
        draft.set(QStringLiteral("feedHeight"), 3.0);
        draft.set(QStringLiteral("spindleSpeed"), 600.0);
        draft.set(QStringLiteral("feedRate"), 40.0);
        draft.set(QStringLiteral("coolantMode"), 1.0);
        if (item.strategyId == QStringLiteral("hole_g73") ||
            item.strategyId == QStringLiteral("hole_peck") ||
            item.strategyId == QStringLiteral("hole_deephole")) {
            draft.set(QStringLiteral("peckDepth"), 2.0);
            draft.set(QStringLiteral("dwellTime"), 0.0);
        } else if (item.strategyId == QStringLiteral("hole_tapping")) {
            draft.set(QStringLiteral("dwellTime"), 0.0);
        } else if (item.strategyId == QStringLiteral("hole_reaming")) {
            draft.set(QStringLiteral("dwellTime"), 0.0);
        } else if (item.strategyId == QStringLiteral("hole_chamfer")) {
            draft.set(QStringLiteral("chamferMode"), item.chamferMode);
            draft.set(QStringLiteral("axialDepth"), item.expectedDrop);
            draft.set(QStringLiteral("chamferWidth"), 1.0);
            draft.set(QStringLiteral("chamferAngle"), 45.0);
            draft.set(QStringLiteral("leadLength"), 1.0);
        } else if (item.strategyId == QStringLiteral("hole_circular_mill")) {
            draft.set(QStringLiteral("stepDown"), 5.0);
            draft.set(QStringLiteral("plungeRate"), 40.0);
            draft.set(QStringLiteral("stockToLeave"), 0.0);
            draft.set(QStringLiteral("bottomAllowance"), 0.0);
            draft.set(QStringLiteral("springPasses"), 1.0);
        }
        panel->restoreDraft(draft, item.toolId);
        if (holeIndices.size() == 2) {
            saveWorkflowScreenshot(
                window, panel->window(),
                outputDir + QLatin1Char('/') + item.artifactStem + QStringLiteral("-two-hole-parameters.png"));
        }

        QPushButton *addButton = panel->findChild<QPushButton *>(QStringLiteral("strategyPrimaryAction"));
        if (!addButton || !addButton->isEnabled()) {
            std::cerr << item.strategyId.toStdString() << " CONFIRM DISABLED\n";
            return false;
        }
        QString confirmationError;
        QTimer confirmationTimer;
        QObject::connect(&confirmationTimer, &QTimer::timeout, [&confirmationError] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (auto *yes = message->button(QMessageBox::Yes)) {
                    yes->click();
                } else {
                    confirmationError = message->text();
                    message->accept();
                }
            }
        });
        confirmationTimer.start(100);
        addButton->click();
        settle();
        confirmationTimer.stop();
        if (!confirmationError.isEmpty() || project->operations().size() != holeIndices.size()) {
            std::cerr << item.strategyId.toStdString() << " CONFIRM/COUNT FAIL "
                      << confirmationError.toStdString() << " operations="
                      << project->operations().size() << std::endl;
            return false;
        }

        QSet<QString> expectedRefs;
        for (int index : holeIndices) expectedRefs.insert(stableFeatureId(project->features().at(index)));
        QSet<QString> actualRefs;
        for (const MachiningOperation &operation : project->operations()) {
            if (operation.strategyId != item.strategyId || operation.toolId != item.toolId ||
                operation.geometryRefs.size() != 1 ||
                (item.hasDepthParam &&
                 std::abs(operation.params.get(QStringLiteral("depth")) - 5.0) > 1.0e-6) ||
                (item.chamferMode >= 0 &&
                 (std::abs(operation.params.get(QStringLiteral("chamferMode")) - item.chamferMode) > 1.0e-6 ||
                  std::abs(operation.params.get(QStringLiteral("axialDepth")) - item.expectedDrop) > 1.0e-6))) {
                std::cerr << item.strategyId.toStdString() << " OP CONTRACT FAIL\n";
                return false;
            }
            actualRefs.insert(operation.geometryRefs.first());
        }
        if (actualRefs != expectedRefs) {
            std::cerr << item.strategyId.toStdString() << " FEATURE REFS FAIL\n";
            return false;
        }

        ProgramGenerationSnapshotOptions completionSnapshotOptions = snapshotOptions;
        completionSnapshotOptions.name = item.strategyId + QStringLiteral(" real hole UI");
        const ProgramGenerationResult program = generator.generate(
            project->operations(), fanuc, postOptions, completionSnapshotOptions);
        const QString &code = program.snapshot.gcodeText;
        const QString toolChange = QStringLiteral("T%1 M6").arg(item.toolId);
        if (!program.ok || code.count(toolChange) != 1 ||
            !code.contains(QStringLiteral("S600 M3")) || !code.contains(QStringLiteral("M8")) ||
            !code.contains(QStringLiteral("M5")) || !code.contains(QStringLiteral("M9")) ||
            !code.contains(QStringLiteral("M30")) || !code.contains(QStringLiteral("G0 Z25.000"))) {
            std::cerr << item.strategyId.toStdString() << " FINAL NC ENVELOPE FAIL\n"
                      << code.toStdString() << std::endl;
            return false;
        }

        double cycleBottom = 0.0;
        if (!item.cycleCode.isEmpty()) {
            QString cycleLine;
            for (const QString &line : code.split(QLatin1Char('\n'))) {
                if (line.startsWith(QStringLiteral("G98 ") + item.cycleCode + QStringLiteral(" Z"))) {
                    cycleLine = line;
                    break;
                }
            }
            const QRegularExpression cyclePattern(
                QStringLiteral("G98 %1 Z(-?[0-9.]+) R(-?[0-9.]+) .*F([0-9.]+)")
                    .arg(item.cycleCode));
            const QRegularExpressionMatch match = cyclePattern.match(cycleLine);
            cycleBottom = match.hasMatch() ? match.captured(1).toDouble() : 0.0;
            const double retract = match.hasMatch() ? match.captured(2).toDouble() : 0.0;
            if (!match.hasMatch() || code.count(QStringLiteral("G98 ") + item.cycleCode) != 1 ||
                !code.contains(QStringLiteral("G80\nG0 Z25.000")) ||
                std::abs(cycleBottom - (retract - 3.0 - item.expectedDrop)) > 1.0e-3) {
                std::cerr << item.strategyId.toStdString() << " FIXED CYCLE FAIL\n"
                          << code.toStdString() << std::endl;
                return false;
            }
        } else if (item.strategyId == QStringLiteral("hole_chamfer")) {
            if (code.count(QStringLiteral("; Chamfer hole")) != holeIndices.size() ||
                code.count(QStringLiteral("G2 ")) < holeIndices.size()) {
                std::cerr << "hole_chamfer CIRCUMFERENTIAL PATH FAIL\n";
                return false;
            }
        } else if (code.count(QStringLiteral("; Center-start spiral mill")) != holeIndices.size()) {
            std::cerr << "hole_circular_mill PATH COUNT FAIL\n";
            return false;
        }

        SimulationController completionSimulation;
        QVector<QVector3D> completionPath;
        QVector<bool> completionRapid;
        QObject::connect(&completionSimulation, &SimulationController::toolPathReady,
                         [&completionPath, &completionRapid](const QVector<QVector3D> &points,
                                                             const QVector<bool> &rapidMoves) {
            completionPath = points;
            completionRapid = rapidMoves;
        });
        completionSimulation.loadGCode(code);
        for (int index : holeIndices) {
            const MachiningFeature &feature = project->features().at(index);
            const double entryZ = feature.center.z() + feature.depth * 0.5;
            const double expectedBottom = entryZ - item.expectedDrop;
            bool simulatedCut = false;
            for (int move = 0; move < completionRapid.size(); ++move) {
                if (completionRapid.at(move)) continue;
                const QVector3D end = completionPath.at(move + 1);
                const double xyDistance = std::hypot(double(end.x() - feature.center.x()),
                                                     double(end.y() - feature.center.y()));
                if (std::abs(end.z() - expectedBottom) < 1.0e-3 &&
                    ((!item.cycleCode.isEmpty() && xyDistance < 1.0e-3) ||
                     (item.cycleCode.isEmpty() && xyDistance <= feature.radius + 2.0))) {
                    simulatedCut = true;
                    break;
                }
            }
            if (!simulatedCut || (!item.cycleCode.isEmpty() &&
                                  std::abs(cycleBottom - expectedBottom) > 1.0e-3)) {
                std::cerr << item.strategyId.toStdString()
                          << " SIMULATION POSITION/DEPTH FAIL index=" << index
                          << " expectedBottom=" << expectedBottom << std::endl;
                return false;
            }
        }

        const QString artifactName = item.artifactStem + QLatin1Char('-') + countName;
        QFile ncFile(outputDir + QLatin1Char('/') + artifactName + QStringLiteral("-final.nc"));
        if (!ncFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
            ncFile.write(code.toUtf8()) <= 0) {
            std::cerr << item.strategyId.toStdString() << " NC ARTIFACT WRITE FAIL\n";
            return false;
        }
        project->setPrograms({program.snapshot});
        const QString savedPath = QDir(projectDir.path()).filePath(artifactName + QStringLiteral(".json"));
        if (!project->saveToFile(savedPath)) {
            std::cerr << item.strategyId.toStdString() << " SAVE FAIL\n";
            return false;
        }
        ProjectManager reopenedCompletion;
        if (!reopenedCompletion.loadFromFile(savedPath) ||
            reopenedCompletion.operations().size() != holeIndices.size()) {
            std::cerr << item.strategyId.toStdString() << " REOPEN FAIL\n";
            return false;
        }
        for (const MachiningOperation &operation : reopenedCompletion.operations()) {
            if (operation.strategyId != item.strategyId || operation.toolId != item.toolId ||
                (item.hasDepthParam &&
                 std::abs(operation.params.get(QStringLiteral("depth")) - 5.0) > 1.0e-6) ||
                (item.chamferMode >= 0 &&
                 (std::abs(operation.params.get(QStringLiteral("chamferMode")) - item.chamferMode) > 1.0e-6 ||
                  std::abs(operation.params.get(QStringLiteral("axialDepth")) - item.expectedDrop) > 1.0e-6))) {
                std::cerr << item.strategyId.toStdString() << " REOPEN CONTRACT FAIL\n";
                return false;
            }
        }
        const ProgramGenerationResult reopenedProgram = generator.generate(
            reopenedCompletion.operations(), fanuc, postOptions, completionSnapshotOptions);
        if (!reopenedProgram.ok || reopenedProgram.snapshot.gcodeText.count(toolChange) != 1) {
            std::cerr << item.strategyId.toStdString() << " REOPEN REGENERATION FAIL\n";
            return false;
        }
        std::cerr << "STAGE " << item.strategyId.toStdString() << ' '
                  << countName.toStdString() << " final-nc/simulation/reopen=pass\n";
        return true;
    };

    for (const CompletionCase &item : completionCases) {
        if (!verifyCompletionSelection(item, {sameGroupHoleIndices.first()},
                                       QStringLiteral("one-hole")) ||
            !verifyCompletionSelection(item, sameGroupHoleIndices,
                                       QStringLiteral("two-hole"))) {
            std::cerr << "FAIL completion workflow " << item.strategyId.toStdString() << std::endl;
            return 26;
        }
    }

    QFile modelFile(modelPath);
    QByteArray modelHash;
    if (modelFile.open(QIODevice::ReadOnly)) {
        modelHash = QCryptographicHash::hash(modelFile.readAll(), QCryptographicHash::Sha256).toHex();
    }
    std::cerr << "EVIDENCE model=" << modelPath.toStdString()
              << " sha256=" << modelHash.toStdString()
              << " selected=" << confirmed.geometryRefs.first().toStdString()
              << " depth=5.000 Q=2.000" << std::endl;
    if (argc >= 4) {
        QFile exe(QString::fromLocal8Bit(argv[3]));
        QByteArray exeHash;
        if (exe.open(QIODevice::ReadOnly)) {
            exeHash = QCryptographicHash::hash(exe.readAll(), QCryptographicHash::Sha256).toHex();
        }
        std::cerr << "EVIDENCE exe=" << QFileInfo(exe).absoluteFilePath().toStdString()
                  << " sha256=" << exeHash.toStdString() << std::endl;
    }

    std::cout << "PASS sidebar-only hole entry: empty blocked; G73 one hole; independent G81 one/two/mixed; G76/G86 and completion hole strategies one/two with final NC, simulation and reopen; no secondary dialog ("
              << holeCount << " recognized holes)\n";
    return 0;
}
