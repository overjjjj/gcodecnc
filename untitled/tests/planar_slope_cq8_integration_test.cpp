#include "../src/gcode/GCodeSafetyValidator.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/services/OperationFactory.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/mill/PlanarSlopeMillingStrategy.h"

#include <QCoreApplication>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

ContourFeature slopeFeature()
{
    ContourFeature feature;
    feature.subType = QStringLiteral("planar_slope_2d");
    feature.center = QVector3D();
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.length = 32.0;
    feature.width = 16.0;
    feature.depth = 24.0 * std::tan(10.0 * std::acos(-1.0) / 180.0);
    feature.points = {
        QVector3D(-16.0f, -8.0f, 0.0f), QVector3D(16.0f, -8.0f, 0.0f),
        QVector3D(16.0f, 8.0f, 0.0f), QVector3D(-16.0f, 8.0f, 0.0f)};
    return feature;
}

ToolEntry ballTool()
{
    ToolEntry tool;
    tool.id = 21;
    tool.type = QStringLiteral("ball_end_mill");
    tool.diameter = 6.0;
    tool.fluteLen = 16.0;
    tool.totalLen = 60.0;
    return tool;
}

OperationFactoryResult confirmedOperation(
    const PlanarSlopeMillingStrategy &strategy,
    const ContourFeature &feature)
{
    OperationProposal proposal;
    proposal.kind = OperationProposalKind::Contour;
    proposal.strategyId = strategy.id();
    proposal.toolId = 21;
    proposal.contourFeature = feature;
    proposal.params = strategy.defaultParams();
    proposal.params.set(QStringLiteral("safeHeight"), 15.0);
    proposal.params.set(QStringLiteral("plungeHeight"), 3.0);
    proposal.params.set(QStringLiteral("slopeAngle"), 10.0);
    proposal.params.set(QStringLiteral("slopeLength"), 24.0);
    proposal.params.set(QStringLiteral("xyWidth"), 8.0);
    proposal.params.set(QStringLiteral("slopeDirection"), 0.0);
    proposal.params.set(QStringLiteral("edgeMargin"), 4.0);
    proposal.params.set(QStringLiteral("keepEdges"), 1.0);
    proposal.params.set(QStringLiteral("compensation"), 0.0);
    proposal.params.set(QStringLiteral("layerAllowance"), 2.0);
    proposal.params.set(QStringLiteral("stepDown"), 1.0);
    proposal.params.set(QStringLiteral("stepOver"), 2.0);
    proposal.selectionChain.id = QStringLiteral("slope-chain");
    proposal.selectionChain.closed = true;
    proposal.selectionChain.orderedGeometryIds = QStringList{
        QStringLiteral("edge-1"), QStringLiteral("edge-2"),
        QStringLiteral("edge-3"), QStringLiteral("edge-4")};

    OperationParameterLayers layers;
    OperationFactoryResult result = OperationFactory::CreateConfirmed(proposal, layers);
    if (result.ok) {
        result.operation.markToolpathValid();
    }
    return result;
}

void verifyFinalSimulation(const QString &gcode)
{
    SimulationController simulation;
    QVector<QVector3D> path;
    QVector<bool> rapid;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&path, &rapid](const QVector<QVector3D> &points,
                                     const QVector<bool> &rapidSegments) {
                         path = points;
                         rapid = rapidSegments;
                     });
    simulation.loadGCode(gcode);

    bool reachesCompensatedLowEdge = false;
    bool tracesFinalSlope = false;
    for (int index = 0; index < rapid.size(); ++index) {
        const QVector3D &from = path.at(index);
        const QVector3D &to = path.at(index + 1);
        if (rapid.at(index) &&
            (std::abs(from.x() - to.x()) > 1.0e-3f ||
             std::abs(from.y() - to.y()) > 1.0e-3f)) {
            require(std::min(from.z(), to.z()) >= 15.0f - 1.0e-3f,
                    "slope rapid XY must remain on the safe plane");
        }
        if (!rapid.at(index)) {
            require(to.x() >= -16.0f - 1.0e-3f && to.x() <= 16.0f + 1.0e-3f &&
                        to.y() >= -8.0f - 1.0e-3f && to.y() <= 8.0f + 1.0e-3f,
                    "simulated programmed path must remain inside the projected boundary");
            reachesCompensatedLowEdge = reachesCompensatedLowEdge ||
                (std::abs(double(to.x()) - 12.521) < 0.02 &&
                 std::abs(double(to.z()) + 4.277) < 0.02);
            tracesFinalSlope = tracesFinalSlope ||
                (std::abs(double(from.x()) + 11.479) < 0.02 &&
                 std::abs(double(from.z()) + 0.046) < 0.02 &&
                 std::abs(double(to.x()) - 12.521) < 0.02);
        }
    }
    require(reachesCompensatedLowEdge && tracesFinalSlope,
            "final CQ8 simulation must trace the compensated final slope plane");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const auto strategy = std::make_shared<PlanarSlopeMillingStrategy>();
    ProgramGenerationService generator(
        [strategy](const QString &id) {
            return id == strategy->id() ? std::shared_ptr<StrategyBase>(strategy)
                                        : std::shared_ptr<StrategyBase>();
        },
        [](int id) { return id == 21 ? ballTool() : ToolEntry(); });

    OperationFactoryResult created = confirmedOperation(*strategy, slopeFeature());
    require(created.ok, "UI-equivalent confirmation must create the 2D slope operation");

    PostProcessorOptions options;
    options.programNumber = QStringLiteral("O2501");
    options.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
        QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    Cq8PostProcessor post;
    ProgramGenerationSnapshotOptions snapshot;
    const ProgramGenerationResult generated =
        generator.generate({created.operation}, post, options, snapshot);
    require(generated.ok, "confirmed 2D slope should generate final CQ8 output");
    require(generated.snapshot.gcodeText.contains(QStringLiteral("PLANAR SLOPE LAYER 3")) &&
                generated.snapshot.gcodeText.contains(QStringLiteral("X12.521")) &&
                generated.snapshot.gcodeText.contains(QStringLiteral("Z-4.277")),
            "final CQ8 must preserve layers and compensated final slope coordinates");
    require(GCodeSafetyValidator::validate(generated.snapshot.gcodeText).ok,
            "final 2D slope CQ8 must pass safety validation");
    verifyFinalSimulation(generated.snapshot.gcodeText);

    ContourFeature sideFace = slopeFeature();
    sideFace.axis = QVector3D(1.0f, 0.0f, 0.0f);
    sideFace.region = FaceRegion::Side;
    created = confirmedOperation(*strategy, sideFace);
    require(created.ok, "side-face proposal may be retained for a future Setup");
    require(!generator.generate({created.operation}, post, options, snapshot).ok,
            "side-face slope must block without a dedicated Setup");
    return 0;
}
