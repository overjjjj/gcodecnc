#include "../src/gcode/GCodeSafetyValidator.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/services/OperationFactory.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/mill/OuterContourChamferStrategy.h"

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

ContourFeature rectangle()
{
    ContourFeature feature;
    feature.subType = QStringLiteral("closed_contour");
    feature.center = QVector3D();
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.points = {
        QVector3D(0.0f, 0.0f, 0.0f), QVector3D(20.0f, 0.0f, 0.0f),
        QVector3D(20.0f, 10.0f, 0.0f), QVector3D(0.0f, 10.0f, 0.0f)};
    return feature;
}

ToolEntry tool()
{
    ToolEntry entry;
    entry.id = 12;
    entry.type = QStringLiteral("chamfer_mill");
    entry.diameter = 10.0;
    entry.fluteLen = 12.0;
    entry.totalLen = 50.0;
    entry.extra.insert(QStringLiteral("includedAngle"), 90.0);
    entry.extra.insert(QStringLiteral("tipRadius"), 0.2);
    return entry;
}

OperationFactoryResult confirmedOperation(
    const OuterContourChamferStrategy &strategy,
    const ContourFeature &feature)
{
    OperationProposal proposal;
    proposal.kind = OperationProposalKind::Contour;
    proposal.strategyId = strategy.id();
    proposal.toolId = 12;
    proposal.contourFeature = feature;
    proposal.params = strategy.defaultParams();
    proposal.params.set(QStringLiteral("safeHeight"), 12.0);
    proposal.params.set(QStringLiteral("plungeHeight"), 3.0);
    proposal.params.set(QStringLiteral("feedHeight"), 3.0);
    proposal.params.set(QStringLiteral("chamferWidth"), 1.0);
    proposal.params.set(QStringLiteral("chamferAngle"), 45.0);
    proposal.params.set(QStringLiteral("toolTipRadius"), 0.2);
    proposal.params.set(QStringLiteral("tipCompensation"), 0.083);
    proposal.params.set(QStringLiteral("sideClearance"), 0.5);
    proposal.params.set(QStringLiteral("stepDown"), 0.5);
    proposal.params.set(QStringLiteral("overcut"), 2.0);
    proposal.selectionChain.id = QStringLiteral("outer-chamfer-chain");
    proposal.selectionChain.closed = true;
    proposal.selectionChain.machiningSide = ChainMachiningSide::Outside;
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

void verifySimulation(const QString &gcode)
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

    bool reachesFinalDepth = false;
    bool tracesBoundary = false;
    for (int index = 0; index < rapid.size(); ++index) {
        const QVector3D &from = path.at(index);
        const QVector3D &to = path.at(index + 1);
        if (rapid.at(index) &&
            (std::abs(from.x() - to.x()) > 1.0e-3f ||
             std::abs(from.y() - to.y()) > 1.0e-3f)) {
            require(std::min(from.z(), to.z()) >= 12.0f - 1.0e-3f,
                    "outer chamfer rapid XY must remain at the safe plane");
        }
        if (!rapid.at(index)) {
            reachesFinalDepth = reachesFinalDepth ||
                std::abs(double(to.z()) + 1.083) < 0.02;
            tracesBoundary = tracesBoundary ||
                (std::abs(double(to.x()) - 20.0) < 0.02 &&
                 std::abs(double(to.y()) - 10.0) < 0.02);
        }
    }
    require(reachesFinalDepth && tracesBoundary,
            "final CQ8 simulation must trace the boundary at reverse-calculated depth");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const auto strategy = std::make_shared<OuterContourChamferStrategy>();
    ProgramGenerationService generator(
        [strategy](const QString &id) {
            return id == strategy->id() ? std::shared_ptr<StrategyBase>(strategy)
                                        : std::shared_ptr<StrategyBase>();
        },
        [](int id) { return id == 12 ? tool() : ToolEntry(); });

    OperationFactoryResult created = confirmedOperation(*strategy, rectangle());
    require(created.ok, "UI-equivalent confirmation must create the chamfer operation");

    PostProcessorOptions options;
    options.programNumber = QStringLiteral("O2404");
    options.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
        QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    Cq8PostProcessor post;
    ProgramGenerationSnapshotOptions snapshot;
    const ProgramGenerationResult generated =
        generator.generate({created.operation}, post, options, snapshot);
    require(generated.ok, "confirmed outer chamfer should generate final CQ8 output");
    require(generated.snapshot.gcodeText.contains(QStringLiteral("OUTER CHAMFER LAYER 3")) &&
                generated.snapshot.gcodeText.contains(QStringLiteral("X2.000 Y0.000")),
            "final CQ8 must preserve depth layers and closed overlap");
    require(GCodeSafetyValidator::validate(generated.snapshot.gcodeText).ok,
            "final outer chamfer CQ8 must pass safety validation");
    verifySimulation(generated.snapshot.gcodeText);

    ContourFeature sideFace = rectangle();
    sideFace.axis = QVector3D(1.0f, 0.0f, 0.0f);
    sideFace.region = FaceRegion::Side;
    created = confirmedOperation(*strategy, sideFace);
    require(created.ok, "side-face proposal may be retained for a future Setup");
    require(!generator.generate({created.operation}, post, options, snapshot).ok,
            "side-face outer chamfer must block without a dedicated Setup");
    return 0;
}
