#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QTextStream>

namespace {

void require(bool condition, const QString &message)
{
    if (condition) {
        return;
    }
    QTextStream(stderr) << "FAIL: " << message << Qt::endl;
    ::exit(1);
}

QString readUtf8(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly | QIODevice::Text),
            QStringLiteral("cannot open %1").arg(path));
    return QString::fromUtf8(file.readAll());
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString source = readUtf8(QStringLiteral("src/ui/MainWindow.cpp"));
    const QString strategySource = readUtf8(QStringLiteral("src/ui/StrategyPanel.cpp"));
    const QString strategyBaseSource = readUtf8(
        QStringLiteral("src/strategies/StrategyBase.h"));
    const QString parameterEditorSource = readUtf8(
        QStringLiteral("src/ui/ParameterEditorDialog.cpp"));
    const QString projectManagerSource = readUtf8(
        QStringLiteral("src/core/ProjectManager.h"));
    const QString highlighterSource = readUtf8(
        QStringLiteral("src/ui/GCodeHighlighter.cpp"));

    require(source.contains(QStringLiteral("systemHeader")),
            QStringLiteral("main window must provide a persistent system header"));
    require(source.contains(QStringLiteral("cq8ConnectionBadge")),
            QStringLiteral("system header must expose CQ8 connection state"));
    require(source.contains(QStringLiteral("machineModeBadge")),
            QStringLiteral("system header must expose machine mode"));
    require(source.contains(QStringLiteral("activeTaskBadge")),
            QStringLiteral("system header must expose the active task"));
    require(source.contains(QStringLiteral("safetyStateBadge")),
            QStringLiteral("system header must expose safety state"));
    require(source.contains(QStringLiteral("机床运行")),
            QStringLiteral("navigation must name the standalone machine workspace"));
    require(!source.contains(QStringLiteral("机床控制（预留）")),
            QStringLiteral("navigation must not describe machine control as an external placeholder"));
    require(!source.contains(QStringLiteral("可直接接入现有控制系统")),
            QStringLiteral("standalone UI must not promise embedding another control system"));
    require(!source.contains(QStringLiteral("Existing CNC control UI can be embedded here later")),
            QStringLiteral("standalone UI must not contain external-control placeholder copy"));
    require(source.contains(QStringLiteral("programValidationStrip")),
            QStringLiteral("program page must expose the validation sequence"));
    require(source.contains(QStringLiteral("programSnapshotBadge")),
            QStringLiteral("program page must expose snapshot state"));
    require(source.contains(QStringLiteral("programSafetyBadge")),
            QStringLiteral("program page must expose safety-check state"));
    require(source.contains(QStringLiteral("simulationStateBadge")),
            QStringLiteral("program page must expose simulation state"));
    require(source.contains(QStringLiteral("outputReadinessBadge")),
            QStringLiteral("program page must expose output readiness"));
    require(source.contains(QStringLiteral("simulationPanel")),
            QStringLiteral("program page must label the final-code simulation surface"));
    require(source.contains(QStringLiteral("finalProgramPanel")),
            QStringLiteral("program page must label the final CQ8 program surface"));
    require(source.contains(QStringLiteral("macroLibraryEditor")),
            QStringLiteral("program page must expose the CQ8 macro library for operator review"));
    require(highlighterSource.contains(QStringLiteral("M(?:98|99)")) &&
                highlighterSource.contains(QStringLiteral("O\\\\d+")) &&
                highlighterSource.contains(QStringLiteral("#\\\\d+")),
            QStringLiteral("G-code review must highlight CQ8 macro calls, routines, and variables"));
    require(source.contains(QStringLiteral("designWorkflowStrip")),
            QStringLiteral("design page must expose the operator workflow"));
    require(source.contains(QStringLiteral("designCommandStrip")),
            QStringLiteral("design page must expose the chapter-seven command navigation"));
    require(source.contains(QStringLiteral("designHolesMenu")) &&
                source.contains(QStringLiteral("designSlotsMenu")) &&
                source.contains(QStringLiteral("designAssistMenu")),
            QStringLiteral("design navigation must group hole, slot, and assist commands"));
    require(source.contains(QStringLiteral("孔类")) &&
                source.contains(QStringLiteral("牙类")) &&
                source.contains(QStringLiteral("平面")) &&
                source.contains(QStringLiteral("槽/型腔")) &&
                source.contains(QStringLiteral("辅助/便捷")),
            QStringLiteral("design navigation must expose the chapter-seven machining categories"));
    require(source.contains(QStringLiteral("规划中")) &&
                source.contains(QStringLiteral("setEnabled(false)")),
            QStringLiteral("unimplemented chapter-seven commands must be visibly unavailable"));
    require(source.contains(QStringLiteral("designModelStage")),
            QStringLiteral("design workflow must expose model readiness"));
    require(source.contains(QStringLiteral("designSetupStage")),
            QStringLiteral("design workflow must expose Setup readiness"));
    require(source.contains(QStringLiteral("designFeatureStage")),
            QStringLiteral("design workflow must expose recognized features"));
    require(source.contains(QStringLiteral("designOperationStage")),
            QStringLiteral("design workflow must expose confirmed operations"));
    require(source.contains(QStringLiteral("designProgramStage")),
            QStringLiteral("design workflow must expose generated programs"));
    require(source.contains(QStringLiteral("m_actStockDefinition")),
            QStringLiteral("design UI must expose part and stock properties"));
    require(source.contains(QStringLiteral("stockDefinition().confirmed")),
            QStringLiteral("design workflow must require explicit stock confirmation"));
    require(source.contains(QStringLiteral("m_actOriginFromHole")),
            QStringLiteral("Setup workflow must expose an explicit hole-center origin action"));
    require(source.contains(QStringLiteral("holeFeaturesShareGroup")),
            QStringLiteral("batch hole confirmation must reject mixed non-spot groups"));
    require(strategySource.contains(QStringLiteral("feature.boundaryPoints.size() >= 3")),
            QStringLiteral("recognized irregular pockets must expose the validated roughing strategy"));
    require(strategySource.contains(QStringLiteral(
                "Irregular pockets currently support confirmed vertical entry only.")),
            QStringLiteral("irregular-pocket entry restrictions must be stated directly to the operator"));
    require(strategySource.contains(QStringLiteral("every generated segment entry")),
            QStringLiteral("irregular-pocket confirmation must disclose its repeated plunge condition"));
    require(strategySource.contains(QStringLiteral("applyManualProcessParameters")),
            QStringLiteral("strategy parameters must include the manual's common process settings"));
    require(strategySource.contains(QStringLiteral("工件坐标系")) &&
                strategySource.contains(QStringLiteral("冷却方式")) &&
                strategySource.contains(QStringLiteral("深度模式")),
            QStringLiteral("process settings must expose work offset, coolant, and depth mode"));
    require(strategySource.contains(QStringLiteral("深度模式（仅支持 0绝对）")),
            QStringLiteral("depth-mode label must not advertise unsupported incremental mode"));
    require(strategySource.contains(QStringLiteral("螺纹旋向")) &&
                strategySource.contains(QStringLiteral("倒角刀尖半径")) &&
                strategySource.contains(QStringLiteral("平面走刀方向")) &&
                strategySource.contains(QStringLiteral("型腔路径方式")) &&
                strategySource.contains(QStringLiteral("轮廓过切量")),
            QStringLiteral("strategy-specific parameters from the chapter-seven manual must be visible"));
    require(parameterEditorSource.contains(QStringLiteral("key == QStringLiteral(\"workOffset\")")) &&
                parameterEditorSource.contains(QStringLiteral("G54-G59")),
            QStringLiteral("process settings must limit work offsets to G54-G59 before saving"));
    require(parameterEditorSource.contains(QStringLiteral("key == QStringLiteral(\"depthMode\")")) &&
                parameterEditorSource.contains(QStringLiteral("absolute depth mode")),
            QStringLiteral("process settings must reject unsupported incremental depth mode"));
    require(strategyBaseSource.contains(
                QStringLiteral("ParametricToolpathProgram parametricProgram")),
            QStringLiteral("toolpath results must expose controller-neutral routine metadata"));
    require(projectManagerSource.contains(
                QStringLiteral("parametricPrograms")),
            QStringLiteral("program snapshots must persist routine metadata outside machine package files"));
    require(source.contains(QStringLiteral("program.parametricPrograms.clear()")),
            QStringLiteral("manual final-G-code edits must invalidate routine metadata"));
    require(source.contains(QStringLiteral("program.macroText.clear()")) &&
                source.contains(QStringLiteral("program.expandedGcodeText.clear()")),
            QStringLiteral("manual CQ8 main-program edits must invalidate macro and simulation artifacts"));

    QTextStream(stdout) << "PASS standalone_ui_contract_test" << Qt::endl;
    return 0;
}
