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
    const QString projectManagerSource = readUtf8(
        QStringLiteral("src/core/ProjectManager.h"));

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
    require(source.contains(QStringLiteral("designWorkflowStrip")),
            QStringLiteral("design page must expose the operator workflow"));
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
