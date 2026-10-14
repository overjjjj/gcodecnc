#include "../src/app/MainWindow.h"
#include "../src/app/AppController.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include "../src/workspaces/process_design/ui/OperationEditDialog.h"
#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/workspaces/process_design/ui/ProcessParameterTemplateStore.h"
#include "../src/tool/ToolLibrary.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QDir>
#include <QTimer>
#include <QEventLoop>
#include <iostream>

static void settle() { QEventLoop loop; QTimer::singleShot(200, &loop, &QEventLoop::quit); loop.exec(); }

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc, argv);
    app.setOrganizationName("CNEXT-UI-Acceptance");
    app.setApplicationName("Phase2-integration");
    ToolEntry drill;
    drill.id = 913; drill.type = "drill"; drill.name = QStringLiteral("测试麻花钻");
    drill.diameter = 6; drill.fluteLen = 40;
    ToolLibrary::instance().addTool(drill);
    MachiningOperation operation;
    operation.id = "phase2-fixture"; operation.featureRef = QStringLiteral("测试工序 · 非试切数据");
    operation.strategyId = "hole_deephole"; operation.toolId = drill.id;
    operation.holeFeature.radius = 3; operation.holeFeature.depth = 12;
    operation.params.set("depth", 12); operation.params.set("peckDepth", 2);
    operation.params.set("feedRate", 150); operation.params.set("spindleSpeed", 1800);
    operation.params.set("safeHeight", 5); operation.params.set("feedHeight", 2);
    operation.markToolpathValid();
    MainWindow window;
    window.show();
    auto *project = AppController::instance().projectManager();
    auto *tree = window.findChild<OperationListPanel *>();
    project->setOperations({operation});
    tree->setOperations({operation});
    emit tree->editOperationRequested(operation.id);
    settle();
    OperationEditDialog *editor = nullptr;
    for (auto *dialog : window.findChildren<QDialog *>())
        if (auto *candidate = dynamic_cast<OperationEditDialog *>(dialog)) editor = candidate;
    if (!editor || !editor->isVisible() || editor->isModal()) return 1;
    editor->findChild<QDoubleSpinBox *>("operationParameter_peckDepth")->setValue(3);
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (!out.isEmpty()) { QDir().mkpath(out); editor->grab().save(out + "/edit-test-fixture.png"); }
    editor->accept(); settle();
    const auto updated = project->operations();
    if (updated.size() != 1 || updated.first().id != operation.id
        || updated.first().params.get("peckDepth") != 3
        || updated.first().toolpathState != ToolpathState::Stale) return 2;

    StrategyPanel panel;
    panel.setFeature(operation.holeFeature);
    if (!panel.selectStrategy(operation.strategyId)) return 3;
    panel.restoreDraft(operation.params, drill.id);
    const auto before = panel.parameterDraft();
    const int toolBefore = panel.currentTool().id;
    ProcessParameterTemplateStore store;
    QString error;
    store.remove("integration-fixture", operation.strategyId, &error);
    if (!store.save("integration-fixture", operation.strategyId,
                    {{"feedRate", 321}, {"depth", 999}, {"toolId", 999}}, &error)) { std::cerr << error.toStdString() << "\n"; return 4; }
    panel.restoreDraft(before, toolBefore);
    auto *combo = panel.findChild<QComboBox *>("processTemplateCombo");
    combo->setCurrentIndex(combo->findData("integration-fixture"));
    panel.findChild<QPushButton *>("applyProcessTemplateButton")->click();
    const auto after = panel.parameterDraft();
    std::cerr << "template index=" << combo->currentIndex() << " feed=" << after.get("feedRate") << " depth=" << before.get("depth") << "/" << after.get("depth") << " tool=" << toolBefore << "/" << panel.currentTool().id << " ops=" << project->operations().size() << "\n";
    store.remove("integration-fixture", operation.strategyId, &error);
    if (after.get("feedRate") != 321 || after.get("depth") != before.get("depth")
        || panel.currentTool().id != toolBefore || project->operations().size() != 1) return 5;
    panel.resize(500, 680); panel.show(); settle();
    if (!out.isEmpty()) panel.grab().save(out + "/template-test-fixture.png");
    std::cout << "PASS MainWindow edits preserve ID and mark stale; template applies cutting data without depth/tool/operation changes\n";
    return 0;
}
