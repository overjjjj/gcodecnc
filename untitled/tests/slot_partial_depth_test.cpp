#include "../src/workspaces/process_design/operations/SlotOperationState.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/strategies/mill/BlindSlotMillingStrategy.h"
#include "../src/strategies/mill/SlotMillingStrategy.h"
#include "../src/strategies/mill/TaperedSlotMillingStrategy.h"
#include <QCoreApplication>
#include <QFile>
#include <cmath>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    auto strategy = std::make_shared<BlindSlotMillingStrategy>();
    auto open = std::make_shared<SlotMillingStrategy>();
    auto tapered = std::make_shared<TaperedSlotMillingStrategy>();
    ToolEntry tool; tool.id = 6; tool.type = "end_mill"; tool.diameter = 6; tool.fluteLen = 20;
    ProgramGenerationService generator([=](const QString &id) -> std::shared_ptr<StrategyBase> {
        if (id == open->id()) return open;
        if (id == tapered->id()) return tapered;
        return strategy;
    }, [=](int) { return tool; }, [](const ContourFeature &candidate) {
        ContourFeature source; source.length = 40; source.width = 12; source.depth = 4;
        source.region = FaceRegion::Front;
        for (const auto &subtype : {"open_slot", "straight_slot"}) {
            source.subType = subtype;
            if (sameSlotGeometry(candidate, source)) return true;
        }
        source.subType = "blind_slot"; source.slopeStartLength = 3; source.slopeEndLength = 1;
        source.slopeMinWidth = .8; source.slopeMaxWidth = .4;
        for (double angle : {45., 90.}) {
            source.angle = angle;
            if (sameSlotGeometry(candidate, source)) return true;
        }
        return false;
    });
    Cq8PostProcessor post;
    PostProcessorOptions options;
    options.safeStartBlocks = QStringList{"G17 G40 G49 G80", "G21", "G90", "G54", "G94"};
    int failures = 0;
    for (double angle : {45., 90.}) for (double radius : {0., 2.}) {
        MachiningOperation op;
        op.id = "partial-slot"; op.opType = OperationType::Contour;
        op.strategyId = strategy->id(); op.toolId = tool.id;
        op.params = strategy->defaultParams(); op.params.set("depth", 2);
        op.params.set("helixRadius", radius);
        auto &f = op.contourFeature;
        f.subType = "blind_slot"; f.length = 40; f.width = 12; f.depth = 4;
        f.angle = angle; f.region = FaceRegion::Front;
        f.slopeStartLength = 3; f.slopeEndLength = 1;
        f.slopeMinWidth = .8; f.slopeMaxWidth = .4;
        const auto result = generator.generate({op}, post, options, {});
        if (!result.ok) { ++failures; std::cerr << result.errors.join(";").toStdString(); continue; }
        QFile sample(QString("outputs/slot-batch2-verification/integrated-fixtures/partial-blind-%1-%2.nc").arg(angle).arg(radius));
        if (sample.open(QIODevice::WriteOnly)) sample.write(result.snapshot.gcodeText.toUtf8());
        SimulationController simulation;
        QVector<QVector3D> path; QVector<bool> rapid;
        QObject::connect(&simulation, &SimulationController::toolPathReady,
            [&](const QVector<QVector3D> &p, const QVector<bool> &r) { path = p; rapid = r; });
        simulation.loadGCode(result.snapshot.gcodeText);
        bool safe = true, reached = false;
        const double a = angle * std::acos(-1.) / 180.;
        for (int i = 0; i < rapid.size(); ++i) {
            if (rapid[i]) continue;
            for (const auto &p : {path[i], path[i+1]}) {
                if (p.z() > 0) continue;
                const double u = p.x()*std::cos(a)+p.y()*std::sin(a);
                const double v = -p.x()*std::sin(a)+p.y()*std::cos(a);
                const double ratio = -p.z()/4.;
                safe &= p.z() >= -2.003 && u-3 >= -20+3*ratio-.003 &&
                        u+3 <= 20-ratio+.003 && v-3 >= -6+.8*ratio-.003 && v+3 <= 6-.4*ratio+.003;
                reached |= std::abs(p.z()+2) < .003;
            }
        }
        if (!safe || !reached || f.depth != 4) {
            ++failures; std::cerr << "partial depth must stop at 2 using true depth 4 wall ratios, angle=" << angle << " radius=" << radius << '\n';
        }
    }
    for (const auto &s : {std::static_pointer_cast<StrategyBase>(open), std::static_pointer_cast<StrategyBase>(tapered)}) {
        MachiningOperation op; op.id = "partial-other"; op.opType = OperationType::Contour;
        op.strategyId = s->id(); op.toolId = tool.id; op.params = s->defaultParams(); op.params.set("depth", 2);
        auto &f = op.contourFeature; f.subType = s == open ? "open_slot" : "straight_slot";
        f.length = 40; f.width = 12; f.depth = 4; f.region = FaceRegion::Front;
        auto result = generator.generate({op}, post, options, {});
        if (s == tapered) {
            if (result.ok || !result.snapshot.gcodeText.isEmpty() || !result.errors.join(";").contains("cutter", Qt::CaseInsensitive)) {
                ++failures; std::cerr << "partial tapered floor must reject without a proven cutter-envelope profile\n";
            }
            continue;
        }
        SimulationController simulation; QVector<QVector3D> path;
        QObject::connect(&simulation, &SimulationController::toolPathReady,
            [&](const QVector<QVector3D> &p, const QVector<bool> &) { path = p; });
        simulation.loadGCode(result.snapshot.gcodeText);
        double minZ = 0;
        for (const auto &p : path) minZ = std::min(minZ, double(p.z()));
        if (!result.ok || std::abs(minZ+2) > .003 || f.depth != 4) {
            ++failures; std::cerr << s->id().toStdString() << " must honor a legal target depth without rewriting real geometry\n";
        }
        op.params.set("depth", 8);
        result = generator.generate({op}, post, options, {});
        if (result.ok || !result.snapshot.gcodeText.isEmpty() || !result.errors.join(";").contains("target depth")) ++failures;
    }
    return failures ? 1 : 0;
}
