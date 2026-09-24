#include "../src/workspaces/process_design/operations/SlotOperationState.h"
#include "../src/app/MillingBatchCommit.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/postprocessor/FanucPostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/strategies/mill/BlindSlotMillingStrategy.h"
#include "../src/strategies/mill/TaperedSlotMillingStrategy.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <cmath>
#include <iostream>

static int failures = 0;
static void check(bool value, const QString &message)
{
    if (!value) { ++failures; std::cerr << message.toStdString() << '\n'; }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    auto blind = std::make_shared<BlindSlotMillingStrategy>();
    auto tapered = std::make_shared<TaperedSlotMillingStrategy>();
    ToolEntry tool; tool.id = 6; tool.type = "end_mill"; tool.diameter = 6;
    tool.fluteLen = 20; tool.totalLen = 60;
    ProgramGenerationService generator(
        [=](const QString &id) -> std::shared_ptr<StrategyBase> {
            if (id == blind->id()) return blind;
            if (id == tapered->id()) return tapered;
            return {};
        }, [=](int) { return tool; }, [](const ContourFeature &candidate) {
            ContourFeature base; base.subType = "blind_slot";
            base.length = 40; base.width = 12; base.depth = 4;
            base.center = QVector3D(11, -7, 0); base.axis = QVector3D(0, 0, 1); base.region = FaceRegion::Front;
            QList<ContourFeature> sources;
            for (double angle : {0., 45., 90.}) for (bool slopes : {false, true}) {
                auto source = base; source.angle = angle;
                if (slopes) { source.slopeStartLength = 2; source.slopeEndLength = 1; source.slopeMinWidth = .4; source.slopeMaxWidth = .6; }
                sources << source;
            }
            // Explicit recognized geometries, including infeasible toolpath cases.
            auto source = base; source.slopeMinWidth = 4; source.slopeMaxWidth = 4; sources << source;
            source = base; source.length = 8; sources << source;
            source = base; source.length = 6.21; sources << source;
            source = base; source.slopeMinWidth = 1.5; sources << source;
            source = base; source.slopeStartLength = 2; source.slopeEndLength = 1; sources << source;
            source = base; source.slopeMinWidth = .4; sources << source;
            source = base; source.slopeStartLength = 10; sources << source;
            source.slopeEndLength = 10; sources << source;
            for (const auto &recognized : sources) if (sameSlotGeometry(candidate, recognized)) return true;
            return false;
        });
    MachiningOperation base;
    base.id = "slot-safety"; base.opType = OperationType::Contour;
    base.strategyId = blind->id(); base.toolId = tool.id;
    base.params = blind->defaultParams();
    base.params.set("depth", 4.0);
    // Final generation must discard obsolete geometry parameters.
    base.params.set("slotLength", 99); base.params.set("slotWidth", 88);
    base.params.set("angle", 17);
    base.params.set("helixRadius", 2); base.params.set("helixPitch", .5);
    auto &f = base.contourFeature;
    f.subType = "blind_slot"; f.length = 40; f.width = 12; f.depth = 4;
    f.center = QVector3D(11, -7, 0); f.axis = QVector3D(0, 0, 1); f.region = FaceRegion::Front;
    PostProcessorOptions options;
    options.programNumber = "O2491"; options.workOffset = "G54";
    options.safeStartBlocks = QStringList{"G17 G40 G49 G80", "G21", "G90", "G54", "G94"};
    ProgramGenerationSnapshotOptions snapshot;
    Cq8PostProcessor cq8; FanucPostProcessor fanuc; SiemensPostProcessor siemens;
    for (PostProcessorBase *post : {static_cast<PostProcessorBase *>(&cq8),
                                    static_cast<PostProcessorBase *>(&fanuc),
                                    static_cast<PostProcessorBase *>(&siemens)}) {
        snapshot.machineProfile.id = post->id();
        for (double angle : {0., 45., 90.}) {
            for (bool slopes : {false, true}) {
              for (double entryRadius : {0., 2.}) {
                auto op = base; op.contourFeature.angle = angle;
                op.params.set("helixRadius", entryRadius);
                if (slopes) {
                    op.contourFeature.slopeStartLength = 2;
                    op.contourFeature.slopeEndLength = 1;
                    op.contourFeature.slopeMinWidth = .4;
                    op.contourFeature.slopeMaxWidth = .6;
                }
                const QString name = post->id() + QString(" angle=%1 slopes=%2 helix=%3").arg(angle).arg(slopes).arg(entryRadius);
                const auto result = generator.generate({op}, *post, options, snapshot);
                check(result.ok, "supported slot must generate: " + name + result.errors.join(";"));
                if (!result.ok) continue;
                const auto &code = result.snapshot.gcodeText;
                if (app.arguments().contains("--samples")) {
                    QDir().mkpath("../outputs/slot-batch1/samples");
                    QFile file(QString("../outputs/slot-batch1/samples/%1-%2-%3-%4.nc").arg(post->id()).arg(angle).arg(slopes).arg(entryRadius));
                    check(file.open(QIODevice::WriteOnly), "sample output open"); file.write(code.toUtf8());
                }
                // Check actual arc words independently of the simulation's interpolation.
                double x = 0, y = 0; int arcs = 0; bool arcSafe = true;
                const QRegularExpression word("([A-Z])(-?[0-9]+(?:\\.[0-9]+)?)");
                for (QString line : code.split('\n')) {
                    line = line.section(';', 0, 0).section('(', 0, 0);
                    QMap<QChar, double> words;
                    auto matches = word.globalMatch(line);
                    while (matches.hasNext()) { const auto m = matches.next(); words[m.captured(1).at(0)] = m.captured(2).toDouble(); }
                    if (words.contains('I') || words.contains('J')) {
                        ++arcs;
                        const double cx = x + words.value('I'), cy = y + words.value('J');
                        const double radius = std::hypot(words.value('I'), words.value('J'));
                        arcSafe &= std::hypot(cx - f.center.x(), cy - f.center.y()) < .002;
                        // The deepest boundary is the smallest for these inward linear walls.
                        arcSafe &= radius + tool.diameter / 2 + .1 <= 6 - op.contourFeature.slopeMaxWidth + .002;
                        arcSafe &= radius + tool.diameter / 2 + .1 <= 6 - op.contourFeature.slopeMinWidth + .002;
                    }
                    if (words.contains('X')) x = words['X'];
                    if (words.contains('Y')) y = words['Y'];
                }
                check((entryRadius == 0 ? arcs == 0 : arcs > 0) && arcSafe, "S1 rotated helix center and stock envelope: " + name);
                SimulationController simulation; QVector<QVector3D> path; QVector<bool> rapid;
                QObject::connect(&simulation, &SimulationController::toolPathReady,
                    [&](const QVector<QVector3D> &p, const QVector<bool> &r) { path = p; rapid = r; });
                simulation.loadGCode(code);
                const double a = angle * std::acos(-1.) / 180.;
                bool envelope = !path.isEmpty(), safeRapid = true, bottom = false;
                for (int i = 0; i < rapid.size(); ++i) {
                    const auto from = path.at(i), to = path.at(i + 1);
                    if (rapid.at(i) && std::hypot(from.x()-to.x(), from.y()-to.y()) > .002)
                        safeRapid &= std::min(from.z(), to.z()) >= 3 - .002;
                    // Linear walls define convex half-spaces: segment endpoints suffice;
                    // arc extrema are additionally checked analytically above.
                    for (const auto &p : {from, to}) {
                        if (p.z() > .002 || rapid.at(i)) continue;
                        const double u = (p.x()-f.center.x())*std::cos(a)+(p.y()-f.center.y())*std::sin(a);
                        const double v = -(p.x()-f.center.x())*std::sin(a)+(p.y()-f.center.y())*std::cos(a);
                        const double d = std::max(0., -double(p.z()) / 4.);
                        envelope &= u - 3 >= -20 + op.contourFeature.slopeStartLength*d - .003 &&
                                    u + 3 <= 20 - op.contourFeature.slopeEndLength*d + .003 &&
                                    v - 3 >= -6 + op.contourFeature.slopeMinWidth*d - .003 &&
                                    v + 3 <= 6 - op.contourFeature.slopeMaxWidth*d + .003 && p.z() >= -4.003;
                        bottom |= std::abs(p.z()+4) < .002;
                    }
                }
                check(envelope && safeRapid && bottom, "complete final cutter envelope/rapid/bottom: " + name);
              }
            }
        }
    }
    base.params.values.remove("slotLength"); base.params.values.remove("slotWidth");
    base.params.values.remove("angle");
    auto rejected = [&](MachiningOperation invalid, const QString &name) {
        const auto strategy = invalid.strategyId == blind->id()
            ? std::static_pointer_cast<StrategyBase>(blind) : std::static_pointer_cast<StrategyBase>(tapered);
        const auto raw = strategy->generate(invalid.contourFeature, tool, invalid.params);
        check(!raw.ok && raw.gcode.isEmpty() && !raw.errorMsg.isEmpty(), "reject without partial code: " + name);
        auto valid = base; valid.strategyId = invalid.strategyId;
        if (invalid.strategyId == tapered->id()) valid.params = tapered->defaultParams();
        if (invalid.strategyId == tapered->id()) valid.contourFeature.slopeMinWidth = .4;
        valid.params.set("depth", valid.contourFeature.depth);
        int commits = 0;
        const auto batch = validateAndCommitMillingBatch({valid, invalid}, *strategy, tool,
            [&](const QList<MachiningOperation> &) { ++commits; return QStringList{"unexpected"}; });
        check(!batch.ok && batch.operationIds.isEmpty() && commits == 0, "zero batch commits: " + name);
        const auto final = generator.generate({valid, invalid}, cq8, options, snapshot);
        check(!final.ok && final.snapshot.gcodeText.isEmpty() && final.errors.join(";").contains(raw.errorMsg), "no partial final program: " + name);
    };
    auto invalid = base; invalid.contourFeature.slopeMinWidth = 4; invalid.contourFeature.slopeMaxWidth = 4;
    rejected(invalid, "S2 true bottom narrower than cutter");
    invalid = base; invalid.params.set("helixRadius", 3); rejected(invalid, "S1 helix violates rough stock");
    invalid.params.set("helixRadius", 20); rejected(invalid, "S1 do not clamp requested helix");
    invalid = base; invalid.contourFeature.length = 8; rejected(invalid, "S1 helix violates end walls");
    invalid = base; invalid.contourFeature.slopeMinWidth = 1.5; rejected(invalid, "S1 helix violates real sloped wall");
    invalid = base; invalid.contourFeature.slopeStartLength = 2; invalid.contourFeature.slopeEndLength = 1;
    invalid.params.set("compensation", 1);
    rejected(invalid, "S2 sloped wall controller compensation has an unprotected finish entry");
    invalid.params.set("compensation", -1);
    rejected(invalid, "S2 opposite controller compensation also requires proven entry");
    invalid = base; invalid.params.set("helixRadius", 0); invalid.contourFeature.length = 6.21;
    rejected(invalid, "short slot cannot ramp");
    invalid = base; invalid.strategyId = tapered->id(); invalid.params = tapered->defaultParams();
    invalid.params.set("depth", invalid.contourFeature.depth);
    invalid.params.values.remove("slotLength"); invalid.params.values.remove("slotWidth");
    invalid.contourFeature.slopeStartLength = 10;
    rejected(invalid, "S3 empty reachable layer at Z=-3");
    invalid.contourFeature.slopeEndLength = 10;
    rejected(invalid, "S3 disconnected reachable ends / S4 finish crosses middle kink");
    invalid.contourFeature.slopeEndLength = 0; invalid.params.set("stepDown", 4);
    rejected(invalid, "S4 single-end piecewise floor cannot be endpoint-interpolated");
    std::cout << "slot safety failures=" << failures << '\n';
    return failures ? 1 : 0;
}
