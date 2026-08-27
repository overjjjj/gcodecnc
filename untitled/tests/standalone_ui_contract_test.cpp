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
    const QString strategyFactorySource = readUtf8(
        QStringLiteral("src/strategies/StrategyFactory.cpp"));
    const QString strategyBaseSource = readUtf8(
        QStringLiteral("src/strategies/StrategyBase.h"));
    const QString parameterEditorSource = readUtf8(
        QStringLiteral("src/ui/ParameterEditorDialog.cpp"));
    const QString contourChoiceDialogSource = readUtf8(
        QStringLiteral("src/ui/ContourMachiningChoiceDialog.cpp"));
    const QString autoHoleDialogSource = readUtf8(
        QStringLiteral("src/ui/AutoHoleCandidateDialog.cpp"));
    const QString processTemplateDialogSource = readUtf8(
        QStringLiteral("src/ui/ProcessTemplateLibraryDialog.cpp"));
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
    require(strategySource.contains(QStringLiteral("setParameterSchema")) &&
                strategySource.contains(QStringLiteral("setTemplateParams")),
            QStringLiteral("the operation editor must use the shared schema and applied template"));
    require(source.contains(QStringLiteral("OperationFactory::CreateConfirmed")),
            QStringLiteral("confirmed UI operations must pass through OperationFactory"));
    require(contourChoiceDialogSource.contains(QStringLiteral("chainGeometrySourceCombo")) &&
                contourChoiceDialogSource.contains(QStringLiteral("chainSelectionModeCombo")) &&
                contourChoiceDialogSource.contains(QStringLiteral("chainMachiningSideCombo")) &&
                contourChoiceDialogSource.contains(QStringLiteral("chainSortStrategyCombo")) &&
                contourChoiceDialogSource.contains(QStringLiteral("chainBranchCombo")),
            QStringLiteral("contour confirmation must expose the complete selection-chain contract"));
    require(source.contains(QStringLiteral("selectionChainForContourChoice")) &&
                source.contains(QStringLiteral("oneProposal.selectionChain")),
            QStringLiteral("contour UI selection chain must enter OperationFactory proposals"));
    require(strategySource.contains(QStringLiteral("plungeHeight")) &&
                strategySource.contains(QStringLiteral("referenceHeight")) &&
                strategySource.contains(QStringLiteral("stepOver")),
            QStringLiteral("legacy strategy fields must be bridged to canonical common parameters"));
    require(strategyFactorySource.contains(QStringLiteral("HighSpeedPeckDrillingStrategy")) &&
                strategyFactorySource.contains(QStringLiteral("BoringG86Strategy")),
            QStringLiteral("G73 and G86 strategies must be registered in the factory"));
    require(strategySource.contains(QStringLiteral("hole_peck_g73")) &&
                strategySource.contains(QStringLiteral("hole_bore_g86")),
            QStringLiteral("G73 and G86 must be selectable from the hole workflow"));
    require(strategyFactorySource.contains(QStringLiteral("ThreadMillingStrategy")) &&
                strategySource.contains(QStringLiteral("hole_thread_mill")),
            QStringLiteral("thread milling must be registered and selectable from the thread workflow"));
    require(strategyFactorySource.contains(QStringLiteral("OuterContourChamferStrategy")) &&
                strategySource.contains(QStringLiteral("mill_outer_chamfer")),
            QStringLiteral("verified 2D outer chamfer must be registered and selectable"));
    require(strategyFactorySource.contains(QStringLiteral("PlanarSlopeMillingStrategy")) &&
                strategySource.contains(QStringLiteral("mill_slope_plane_2d")),
            QStringLiteral("verified 2D slope milling must be registered and selectable"));
    require(source.contains(QStringLiteral(
                "addDesignCommand(contoursMenu, QStringLiteral(\"outerChamfer\"));")) &&
                source.contains(QStringLiteral(
                    "addDesignCommand(contoursMenu, QStringLiteral(\"chamfer3D\"), false);")) &&
                source.contains(QStringLiteral(
                    "addDesignCommand(contoursMenu, QStringLiteral(\"clearInnerCorner\"), false);")) &&
                source.contains(QStringLiteral(
                    "addDesignCommand(contoursMenu, QStringLiteral(\"engrave\"), false);")) &&
                source.contains(QStringLiteral(
                    "addDesignCommand(contoursMenu, QStringLiteral(\"spatialCurve\"), false);")),
            QStringLiteral("2D outer chamfer must be available while unverifiable module-four paths remain blocked"));
    require(source.contains(QStringLiteral(
                "addDesignCommand(surfacesMenu, QStringLiteral(\"slopeMill\"));")) &&
                source.contains(QStringLiteral(
                    "addDesignCommand(surfacesMenu, QStringLiteral(\"slopeMill3D\"), false);")) &&
                source.contains(QStringLiteral(
                    "addDesignCommand(surfacesMenu, QStringLiteral(\"filletMill\"), false);")) &&
                source.contains(QStringLiteral(
                    "addDesignCommand(surfacesMenu, QStringLiteral(\"chamferSlopePlane\"), false);")),
            QStringLiteral("2D slope must be available while unverifiable 3D surface paths remain blocked"));
    require(!strategyFactorySource.contains(QStringLiteral("mill_slope_plane_3d")) &&
                !strategyFactorySource.contains(QStringLiteral("mill_filleted_corner")) &&
                !strategyFactorySource.contains(QStringLiteral("mill_chamfer_3d")) &&
                !strategyFactorySource.contains(QStringLiteral("chamfer_slope_plane")),
            QStringLiteral("unverifiable 3D slope, fillet, 3D chamfer, and slope-chamfer strategies must remain unregistered"));
    require(source.contains(QStringLiteral(
                "addDesignCommand(automationMenu, QStringLiteral(\"autoHole\"));")) &&
                source.contains(QStringLiteral(
                    "addDesignCommand(automationMenu, QStringLiteral(\"processTemplates\"));")) &&
                source.contains(QStringLiteral(
                    "addDesignCommand(automationMenu, QStringLiteral(\"smartChamfer\"), false);")) &&
                source.contains(QStringLiteral(
                    "addDesignCommand(automationMenu, QStringLiteral(\"autoSlotFrame\"), false);")),
            QStringLiteral("verified auto-hole drafts and template data must be available while unsafe automation stays blocked"));
    require(source.contains(QStringLiteral("AutoHoleCandidateDialog")) &&
                source.contains(QStringLiteral("AutoHolePlanningService::Confirm")) &&
                source.contains(QStringLiteral("project->setOperations")),
            QStringLiteral("automatic-hole UI must require candidate review before writing a formal operation"));
    require(autoHoleDialogSource.contains(QStringLiteral("识别结果仅为候选草稿")) &&
                autoHoleDialogSource.contains(QStringLiteral("确认创建工序")),
            QStringLiteral("automatic-hole UI must clearly distinguish drafts from formal operations"));
    require(processTemplateDialogSource.contains(QStringLiteral("addPlan")) &&
                processTemplateDialogSource.contains(QStringLiteral("add")) &&
                processTemplateDialogSource.contains(QStringLiteral("版本不可覆盖")),
            QStringLiteral("template UI must explicitly add immutable parameter and machining-plan versions"));
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
