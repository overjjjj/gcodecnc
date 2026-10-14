#include "../src/strategies/StrategyFactory.h"
#include "../src/workspaces/process_design/operations/OperationProposal.h"
#include "../src/workspaces/process_design/ui/OperationEditDialog.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/tool/ToolLibrary.h"
#include <QCoreApplication>
#include <QRegularExpression>
#include <iostream>
#include <cmath>
#include <limits>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const QString &context) {
        if (!ok) { ++failures; std::cerr << context.toStdString() << '\n'; }
    };
    struct Case { const char *id; const char *type; double diameter; };
    const Case cases[] = {
        {"hole_spot", "spot_drill", 5}, {"hole_g81", "drill", 10},
        {"hole_deephole", "drill", 10}, {"hole_g73", "drill", 10},
        {"hole_g76", "boring_bar", 3}, {"hole_g86", "boring_bar", 3},
        {"hole_tapping", "tap", 10}, {"hole_chamfer", "chamfer_mill", 5},
        {"hole_circular_mill", "end_mill", 5}, {"hole_peck", "drill", 10},
        {"hole_reaming", "reamer", 10}
    };
    HoleFeature hole; hole.center = QVector3D(20, 30, -10); hole.radius = 5;
    hole.depth = 20; hole.axis = QVector3D(0, 0, 1); hole.region = FaceRegion::Front;
    ToolEntry tool; tool.id = 981; tool.fluteLen = 80; tool.totalLen = 120;
    tool.pitch = 1.25; tool.pointAngle = 90; tool.extra.insert("angle", 90);
    Cq8PostProcessor cq8; SiemensPostProcessor siemens;
    PostProcessorOptions options; options.programNumber = "O2608";
    options.safeStartBlocks = QStringList{"G17 G40 G49 G80", "G21", "G90", "G54", "G94"};
    ProgramGenerationSnapshotOptions snapshot;
    ProgramGenerationService generator(
        [](const QString &id) { return StrategyFactory::instance().strategy(id); },
        [&](int) { return tool; });
    for (const auto &c : cases) {
        const QString id = c.id;
        auto strategy = StrategyFactory::instance().strategy(id);
        if (!strategy) return 2;
        tool.type = c.type; tool.diameter = c.diameter;
        ToolLibrary::instance().removeTool(tool.id); ToolLibrary::instance().addTool(tool);
        MachiningOperation op; op.id = id; op.opType = OperationType::Hole;
        op.strategyId = id; op.toolId = tool.id; op.holeFeature = hole;
        op.params = strategy->defaultParams(); op.params.set("depth", 5);
        op.params.set("stockToLeave", .2);
        check(strategy->generate(hole, tool, op.params).ok, id + " valid baseline");
        for (const QVariant &speed : QList<QVariant>{.25, .5, .999, 0., -1.,
                 std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),
                 double(std::numeric_limits<int>::max()) + 1., QString("invalid")}) {
            auto bad = op; bad.params.values.insert("spindleSpeed", speed);
            const QString label = id + " S=" + speed.toString();
            check(!strategy->generate(hole, tool, bad.params).ok, label + " strategy must reject");
            OperationProposal proposal; proposal.kind = OperationProposalKind::Hole;
            proposal.strategyId = id; proposal.toolId = tool.id;
            proposal.holeFeature = hole; proposal.params = bad.params;
            check(!OperationFactory::confirm(proposal, OperationConfirmationIntent::ExplicitUser, {}).ok,
                  label + " creation must reject");
            check(!operationEditError(op, bad.params, tool.id).isEmpty(), label + " edit must reject");
            check(!generator.generate({bad}, cq8, options, snapshot).ok, label + " service must reject");
        }
        for (double speed : {1., 600., 200.9}) {
            auto valid = op; valid.params.set("spindleSpeed", speed);
            OperationProposal proposal; proposal.kind = OperationProposalKind::Hole;
            proposal.strategyId = id; proposal.toolId = tool.id;
            proposal.holeFeature = hole; proposal.params = valid.params;
            check(OperationFactory::confirm(proposal, OperationConfirmationIntent::ExplicitUser, {}).ok,
                  id + " valid creation S=" + QString::number(speed));
            check(operationEditError(op, valid.params, tool.id).isEmpty(),
                  id + " valid edit S=" + QString::number(speed));
            const auto result = generator.generate({valid}, cq8, options, snapshot);
            check(result.ok && result.snapshot.gcodeText.contains(QString("S%1 M3").arg(int((id == "hole_g81" || id == "hole_g73" || id == "hole_g76" || id == "hole_g86") ? std::round(speed) : speed))),
                  id + " positive integer output");
        }
        if (id == "hole_tapping") {
            const ToolEntry validTool = tool;
            for (int badCase = 0; badCase < 6; ++badCase) {
                tool = validTool;
                if (badCase == 0) tool.diameter *= 2;
                else if (badCase == 5) tool.type = "end_mill";
                else tool.pitch = badCase == 1 ? 0. : badCase == 2 ? -1. :
                    badCase == 3 ? std::numeric_limits<double>::quiet_NaN() : std::numeric_limits<double>::infinity();
                ToolLibrary::instance().updateTool(tool);
                OperationProposal proposal; proposal.kind = OperationProposalKind::Hole;
                proposal.strategyId = id; proposal.toolId = tool.id;
                proposal.holeFeature = hole; proposal.params = op.params;
                check(!strategy->generate(hole, tool, op.params).ok, "invalid tap tool strategy");
                check(!OperationFactory::confirm(proposal, OperationConfirmationIntent::ExplicitUser, {}).ok,
                      "invalid tap tool creation");
                check(!operationEditError(op, op.params, tool.id).isEmpty(), "invalid tap tool edit");
                check(!generator.generate({op}, cq8, options, snapshot).ok, "invalid tap tool service");
            }
            tool = validTool; ToolLibrary::instance().updateTool(tool);
            auto mismatched = op; mismatched.holeFeature.pitch = 1.5;
            OperationProposal pitchProposal; pitchProposal.kind = OperationProposalKind::Hole;
            pitchProposal.strategyId = id; pitchProposal.toolId = tool.id;
            pitchProposal.holeFeature = mismatched.holeFeature; pitchProposal.params = op.params;
            check(!strategy->generate(mismatched.holeFeature, tool, op.params).ok, "recognized pitch mismatch strategy");
            check(!OperationFactory::confirm(pitchProposal, OperationConfirmationIntent::ExplicitUser, {}).ok,
                  "recognized pitch mismatch creation");
            check(!operationEditError(mismatched, op.params, tool.id).isEmpty(), "recognized pitch mismatch edit");
            check(!generator.generate({mismatched}, cq8, options, snapshot).ok, "recognized pitch mismatch service");
            op.params.set("spindleSpeed", 200.9);
            const auto raw = strategy->generate(hole, tool, op.params);
            check(raw.ok && raw.gcode.contains("S200 M3") && raw.gcode.contains("rpm=200.000") &&
                  raw.gcode.contains("f=250.000"), "tap raw S/F/rpm must share emitted integer speed");
            const auto iso = generator.generate({op}, cq8, options, snapshot);
            check(iso.ok && iso.snapshot.gcodeText.contains("S200 M3") &&
                  QRegularExpression("G84[^\\n]*F250\\.000").match(iso.snapshot.gcodeText).hasMatch(),
                  "tap CQ8 final S200 and F250");
            const auto sinumerik = generator.generate({op}, siemens, options, snapshot);
            check(sinumerik.ok && sinumerik.snapshot.gcodeText.contains("S200 M3") &&
                  QRegularExpression("CYCLE84\\([^\\n]*,200\\.000,200\\.000\\)").match(sinumerik.snapshot.gcodeText).hasMatch(),
                  "tap Siemens final S and CYCLE84 RPM agree");
        }
    }
    std::cout << "FAILURES=" << failures << std::endl;
    return failures ? 1 : 0;
}
