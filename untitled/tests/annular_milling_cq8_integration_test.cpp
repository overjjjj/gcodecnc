#include "../src/gcode/GCodeSafetyValidator.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/postprocessor/FanucPostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/mill/AnnularMillingStrategy.h"
#include "../src/strategies/mill/IslandMillingStrategy.h"

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

QVector<QVector3D> circleLoop(double radius)
{
    QVector<QVector3D> points;
    for (int index = 0; index < 16; ++index) {
        const double angle = 2.0 * std::acos(-1.0) * index / 16.0;
        points.append(QVector3D(float(radius * std::cos(angle)),
                               float(radius * std::sin(angle)), 0.0f));
    }
    return points;
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

    bool reachesBottom = false;
    bool hasCut = false;
    for (int index = 0; index < rapid.size(); ++index) {
        const QVector3D &from = path.at(index);
        const QVector3D &to = path.at(index + 1);
        if (rapid.at(index) &&
            (std::abs(from.x() - to.x()) > 1.0e-3f ||
             std::abs(from.y() - to.y()) > 1.0e-3f)) {
            require(std::min(from.z(), to.z()) >= 12.0f - 1.0e-3f,
                    "annular rapid XY must remain on the safe plane");
        }
        if (!rapid.at(index) && std::min(from.z(), to.z()) < -1.0e-3f) {
            hasCut = true;
            const double radius = std::hypot(double(to.x()), double(to.y()));
            require(radius >= 11.0 - 0.02 && radius <= 17.0 + 0.02,
                    "simulated cutter center must remain inside the annular envelope");
            reachesBottom = reachesBottom || std::abs(double(to.z()) + 4.0) < 0.02;
        }
    }
    require(hasCut && reachesBottom,
            "final CQ8 simulation should trace the annulus to final depth");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const auto strategy = std::make_shared<AnnularMillingStrategy>();
    const auto islandStrategy = std::make_shared<IslandMillingStrategy>();
    ProgramGenerationService generator(
        [strategy, islandStrategy](const QString &id) {
            if (id == strategy->id()) return std::shared_ptr<StrategyBase>(strategy);
            if (id == islandStrategy->id()) {
                return std::shared_ptr<StrategyBase>(islandStrategy);
            }
            return std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 7) {
                tool.id = 7;
                tool.type = QStringLiteral("end_mill");
                tool.diameter = 6.0;
                tool.fluteLen = 20.0;
                tool.totalLen = 60.0;
            }
            return tool;
        });

    MachiningOperation operation;
    operation.id = QStringLiteral("annular-cq8");
    operation.opType = OperationType::Roughing;
    operation.stage = OperationStage::RoughCut;
    operation.strategyId = strategy->id();
    operation.toolId = 7;
    operation.params = strategy->defaultParams();
    operation.params.set(QStringLiteral("safeHeight"), 12.0);
    operation.params.set(QStringLiteral("plungeHeight"), 3.0);
    operation.params.set(QStringLiteral("referenceHeight"), 0.0);
    operation.params.set(QStringLiteral("depth"), 4.0);
    operation.params.set(QStringLiteral("stepOver"), 3.0);
    operation.params.set(QStringLiteral("stepDown"), 2.0);
    operation.contourFeature.subType = QStringLiteral("annular_pocket");
    operation.contourFeature.center = QVector3D();
    operation.contourFeature.radius = 20.0;
    operation.contourFeature.depth = 4.0;
    operation.contourFeature.points = circleLoop(20.0);
    operation.contourFeature.islands = {circleLoop(8.0)};
    operation.markToolpathValid();

    PostProcessorOptions options;
    options.programNumber = QStringLiteral("O2403");
    options.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
        QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    Cq8PostProcessor post;
    ProgramGenerationSnapshotOptions snapshot;
    const ProgramGenerationResult generated =
        generator.generate({operation}, post, options, snapshot);
    require(generated.ok, "verified annulus should generate final CQ8 output");
    require(GCodeSafetyValidator::validate(generated.snapshot.gcodeText).ok,
            "final annular CQ8 output should pass the safety validator");

    verifyFinalSimulation(generated.snapshot.gcodeText);

    operation.id = QStringLiteral("island-cq8");
    operation.strategyId = islandStrategy->id();
    const ProgramGenerationResult generatedIsland =
        generator.generate({operation}, post, options, snapshot);
    require(generatedIsland.ok &&
                generatedIsland.snapshot.gcodeText.contains(QStringLiteral("ISLAND LAYER")),
            "verified island should generate distinct final CQ8 output");
    require(GCodeSafetyValidator::validate(generatedIsland.snapshot.gcodeText).ok,
            "final island CQ8 output should pass the safety validator");
    verifyFinalSimulation(generatedIsland.snapshot.gcodeText);

    const FanucPostProcessor fanuc;
    const SiemensPostProcessor siemens;
    const ProgramGenerationResult generatedIslandFanuc =
        generator.generate({operation}, fanuc, options, snapshot);
    const ProgramGenerationResult generatedIslandSiemens =
        generator.generate({operation}, siemens, options, snapshot);
    require(generatedIslandFanuc.ok && generatedIslandSiemens.ok,
            "verified island should post for Fanuc and Siemens");
    require(GCodeSafetyValidator::validate(
                generatedIslandFanuc.snapshot.gcodeText).ok &&
                GCodeSafetyValidator::validate(
                    generatedIslandSiemens.snapshot.gcodeText).ok,
            "final Fanuc and Siemens island output should pass the safety validator");
    return 0;
}
