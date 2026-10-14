#include "../src/strategies/mill/SlotMillingStrategy.h"
#include "../src/strategies/mill/BlindSlotMillingStrategy.h"
#include "../src/strategies/mill/TaperedSlotMillingStrategy.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include <QCoreApplication>
#include <iostream>
#include <limits>
#include <cmath>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    auto check = [&](bool pass, const char *message) {
        if (!pass) { std::cerr << message << '\n'; ok = false; }
    };
    ToolEntry tool;
    tool.id = 6; tool.type = "end_mill"; tool.diameter = 6; tool.fluteLen = 20; tool.totalLen = 60;
    const QList<std::shared_ptr<StrategyBase>> strategies = {
        std::make_shared<SlotMillingStrategy>(), std::make_shared<BlindSlotMillingStrategy>(),
        std::make_shared<TaperedSlotMillingStrategy>()};
    Cq8PostProcessor post;
    PostProcessorOptions options;
    options.workOffset = "G54";
    options.safeStartBlocks = QStringList{"G17 G40 G49 G80", "G21", "G90", "G54", "G94"};
    for (const auto &strategy : strategies) {
        ContourFeature feature;
        feature.subType = strategy->id() == "mill_slot" ? "open_slot" : "blind_slot";
        feature.length = 40; feature.width = 12; feature.depth = 4; feature.region = FaceRegion::Front;
        auto params = strategy->defaultParams();
        for (const QString &key : {QString("slotLength"), QString("slotWidth"), QString("angle")}) params.values.remove(key);
        ProgramGenerationService service([&](const QString &) { return strategy; }, [&](int) { return tool; });
        auto generate = [&](const ContourFeature &f) {
            MachiningOperation op;
            op.id = "axis-contract"; op.opType = OperationType::Contour; op.strategyId = strategy->id();
            op.toolId = tool.id; op.params = params; op.contourFeature = f;
            return service.generate({op}, post, options, {});
        };
        const float nan = std::numeric_limits<float>::quiet_NaN();
        for (const QVector3D &axis : {QVector3D(0.6f, 0, 0.8f), QVector3D(), QVector3D(nan,0,1),
                                    QVector3D(0,0,std::numeric_limits<float>::infinity()), QVector3D(1e-4f,0,1)}) {
            feature.axis = axis;
            const auto direct = strategy->generate(feature, tool, params);
            const auto final = generate(feature);
            if (direct.ok || final.ok) std::cerr << strategy->id().toStdString() << " accepted axis " << axis.x() << ',' << axis.y() << ',' << axis.z() << '\n';
            check(!direct.ok && direct.gcode.isEmpty(), "S1 direct/recalculation must reject invalid or tilted axis");
            check(!final.ok && final.snapshot.gcodeText.isEmpty(), "S1 final generation must reject invalid or tilted axis");
        }
        for (const QVector3D &axis : {QVector3D(0,0,1), QVector3D(0,0,-1), QVector3D(0,0,2), QVector3D(1e-7f,0,1)}) {
            feature.axis = axis;
            check(strategy->generate(feature, tool, params).ok && generate(feature).ok,
                  "S1 plane normal sign and magnitude must not change valid Z-plane machining");
        }
        feature.axis = QVector3D(0,0,1);
        feature.region = FaceRegion::Side;
        MachiningOperation accessible;
        accessible.id="accessible-side"; accessible.opType=OperationType::Contour;
        accessible.strategyId=strategy->id(); accessible.toolId=tool.id; accessible.params=params;
        accessible.contourFeature=feature;
        auto &e=accessible.selectionEvidence;
        e.sourceFingerprint="source"; e.setupFingerprint="setup"; e.coordinateSystemId="G54";
        e.orderedGeometryIds=QStringList{"face:1"}; e.selectedSurfaceNormal={0,0,1}; e.toolAxis={0,0,1};
        e.closed=true; e.outerLoopPointCount=4; e.explicitUserSelection=true;
        ProgramGenerationSnapshotOptions snapshot;
        snapshot.sourceFingerprint="source"; snapshot.setupFingerprint="setup";
        const auto side=service.generate({accessible},post,options,snapshot);
        check(side.ok,"S1 explicitly accessible Z-facing Side selection remains supported");
    }

    BlindSlotMillingStrategy blind;
    ContourFeature slope;
    slope.subType = "blind_slot"; slope.length = 40; slope.width = 12; slope.depth = 4;
    slope.region = FaceRegion::Front; slope.slopeMinWidth = 4; slope.slopeMaxWidth = 4;
    auto params = blind.defaultParams();
    params.values.remove("slotLength"); params.values.remove("slotWidth");
    const auto full = blind.generate(slope, tool, params);
    check(!full.ok && full.gcode.isEmpty(), "S2 original full-depth slope cannot fit tool; must not shrink slopes");
    auto finalFor = [&](std::shared_ptr<StrategyBase> strategy, const ContourFeature &feature, const StrategyParams &p) {
        ProgramGenerationService service([=](const QString &) { return strategy; }, [&](int) { return tool; });
        MachiningOperation op; op.id="slope-reject"; op.opType=OperationType::Contour;
        op.strategyId=strategy->id();op.toolId=tool.id;op.params=p;op.contourFeature=feature;
        return service.generate({op},post,options,{});
    };
    const auto fullFinal=finalFor(std::make_shared<BlindSlotMillingStrategy>(),slope,params);
    check(!fullFinal.ok && fullFinal.snapshot.gcodeText.isEmpty(),"S2 rejected geometry cannot create final snapshot");
    params.set("depth", 1);
    const auto shallow = blind.generate(slope, tool, params);
    check(shallow.ok, "S2 partial-depth slope remains machinable");
    SimulationController simulation;
    QVector<QVector3D> path; QVector<bool> rapid;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&](const QVector<QVector3D> &p, const QVector<bool> &r) { path=p; rapid=r; });
    simulation.loadGCode(shallow.gcode);
    bool reachesBottomEdge = false;
    for (int i=0; i<rapid.size(); ++i) {
        if (rapid[i]) continue;
        const auto &p=path[i+1];
        check(p.z() >= -1.001, "S2 partial-depth motion must not exceed requested depth");
        reachesBottomEdge |= std::abs(p.z()+1)<0.001 && std::abs(std::abs(p.y())-2)<0.001;
    }
    check(reachesBottomEdge, "S2 shallow finishing must preserve original slope cross-section (center Y=+/-2)");
    params.set("helixRadius",2.5);
    const auto helix=blind.generate(slope,tool,params);
    check(!helix.ok && helix.gcode.isEmpty(), "S2 helix must fit original target-depth rough cross-section");
    params.set("helixRadius",0);
    auto asymmetric=slope;
    asymmetric.slopeMinWidth=9; asymmetric.slopeMaxWidth=0;
    params.set("depth",2);
    const auto offCenter=blind.generate(asymmetric,tool,params);
    check(!offCenter.ok && offCenter.gcode.isEmpty(), "S2 existing center-based wall entry must fit asymmetric original slopes");

    TaperedSlotMillingStrategy tapered;
    slope.slopeMinWidth=0; slope.slopeMaxWidth=0; slope.slopeStartLength=10; slope.slopeEndLength=10;
    params=tapered.defaultParams(); params.values.remove("slotLength"); params.values.remove("slotWidth");
    const auto separated=tapered.generate(slope,tool,params);
    check(!separated.ok && separated.gcode.isEmpty(), "S3 separated double-end slope must not bridge retained middle material");
    const auto separatedFinal=finalFor(std::make_shared<TaperedSlotMillingStrategy>(),slope,params);
    check(!separatedFinal.ok && separatedFinal.snapshot.gcodeText.isEmpty(),"S3 rejected regions cannot create final snapshot");
    slope.slopeStartLength=0; slope.slopeEndLength=0;
    check(tapered.generate(slope,tool,params).ok, "S3 existing directional slope must remain supported");
    if(ok) std::cout << "PASS slot boundary contracts S1/S2/S3\n";
    return ok?0:1;
}
