#include "src/workspaces/process_design/ui/ToolOperationCompatibility.h"

#include <QCoreApplication>
#include <QTextStream>

#include <cstdlib>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << Qt::endl;
        std::exit(1);
    }
}

ToolEntry tool(const QString &type, double diameter, double fluteLength)
{
    ToolEntry result;
    result.id = 1;
    result.name = QStringLiteral("test tool");
    result.type = type;
    result.diameter = diameter;
    result.fluteLen = fluteLength;
    result.totalLen = 60.0;
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    HoleFeature hole;
    hole.kind = FeatureKind::Hole;
    hole.radius = 5.0;
    hole.depth = 20.0;

    ToolCompatibilityReport wrongType = reviewToolCompatibility(
        QStringLiteral("hole_peck"), tool(QStringLiteral("end_mill"), 10.0, 30.0), hole, false);
    require(wrongType.hasBlockingIssues(),
            "wrong tool type should block operation confirmation");

    ToolCompatibilityReport shortFlute = reviewToolCompatibility(
        QStringLiteral("hole_peck"), tool(QStringLiteral("drill"), 10.0, 12.0), hole, false);
    require(shortFlute.hasBlockingIssues(),
            "cutting depth beyond flute length should block confirmation");

    ToolCompatibilityReport missingFlute = reviewToolCompatibility(
        QStringLiteral("hole_peck"), tool(QStringLiteral("drill"), 10.0, 0.0), hole, false);
    require(!missingFlute.hasBlockingIssues() && missingFlute.hasWarnings(),
            "missing flute length should require operator acknowledgement");

    ToolCompatibilityReport undersizeDrill = reviewToolCompatibility(
        QStringLiteral("hole_peck"), tool(QStringLiteral("drill"), 8.0, 30.0), hole, false);
    require(!undersizeDrill.hasBlockingIssues() && undersizeDrill.hasWarnings(),
            "undersize drill should be allowed only as an acknowledged pilot/rough hole");

    ToolCompatibilityReport oversizeDrill = reviewToolCompatibility(
        QStringLiteral("hole_peck"), tool(QStringLiteral("drill"), 11.0, 30.0), hole, false);
    require(oversizeDrill.hasBlockingIssues(),
            "oversize drill should block the target hole operation");

    ToolCompatibilityReport oversizeG73Drill = reviewToolCompatibility(
        QStringLiteral("hole_g73"), tool(QStringLiteral("drill"), 11.0, 30.0), hole, false);
    require(oversizeG73Drill.hasBlockingIssues(),
            "oversize drill should also block G73 confirmation");

    ToolCompatibilityReport oversizeG81Drill = reviewToolCompatibility(
        QStringLiteral("hole_g81"), tool(QStringLiteral("drill"), 11.0, 30.0), hole, false);
    require(oversizeG81Drill.hasBlockingIssues(),
            "oversize drill should block independent G81 confirmation");

    for (const QString &strategyId : {QStringLiteral("hole_g76"), QStringLiteral("hole_g86")}) {
        ToolCompatibilityReport boringBar = reviewToolCompatibility(
            strategyId, tool(QStringLiteral("boring_bar"), 8.0, 30.0), hole, false, 5.0);
        require(!boringBar.hasBlockingIssues(),
                "G76/G86 must accept a boring bar with sufficient reach");
        ToolCompatibilityReport drillForBoring = reviewToolCompatibility(
            strategyId, tool(QStringLiteral("drill"), 8.0, 30.0), hole, false, 5.0);
        require(drillForBoring.hasBlockingIssues(),
                "G76/G86 must reject a drill in place of a boring bar");
    }

    ContourFeature slot;
    slot.subType = QStringLiteral("blind_slot");
    slot.length = 40.0;
    slot.width = 8.0;
    slot.depth = 5.0;
    ToolCompatibilityReport wideTool = reviewToolCompatibility(
        QStringLiteral("mill_blind_slot"), tool(QStringLiteral("end_mill"), 8.0, 20.0), slot, false);
    require(wideTool.hasBlockingIssues(),
            "slot tool diameter equal to slot width should block confirmation");

    ToolCompatibilityReport inefficientTool = reviewToolCompatibility(
        QStringLiteral("mill_blind_slot"), tool(QStringLiteral("end_mill"), 1.0, 20.0), slot, false);
    require(!inefficientTool.hasBlockingIssues() && inefficientTool.hasWarnings(),
            "very small slot tool should require efficiency acknowledgement");

    HoleFeature thread = hole;
    thread.kind = FeatureKind::Thread;
    thread.pitch = 1.5;
    ToolEntry tap = tool(QStringLiteral("tap"), 10.0, 30.0);
    tap.pitch = 1.25;
    ToolCompatibilityReport wrongPitch = reviewToolCompatibility(
        QStringLiteral("hole_tapping"), tap, thread, false);
    require(wrongPitch.hasBlockingIssues(),
            "tap pitch mismatch should block confirmation");

    QTextStream(stdout) << "PASS tool_operation_compatibility_test" << Qt::endl;
    return 0;
}
