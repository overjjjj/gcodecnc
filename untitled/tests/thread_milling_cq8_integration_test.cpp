#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/hole/ThreadMillingStrategy.h"

#include <QCoreApplication>

#include <cmath>
#include <iostream>
#include <memory>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const auto strategy = std::make_shared<ThreadMillingStrategy>();
    ProgramGenerationService generator(
        [strategy](const QString &id) {
            return id == strategy->id()
                ? std::static_pointer_cast<StrategyBase>(strategy)
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            tool.id = id;
            tool.type = QStringLiteral("thread_mill");
            tool.diameter = 4.0;
            tool.pitch = 1.5;
            tool.fluteLen = 15.0;
            tool.totalLen = 50.0;
            return tool;
        });

    MachiningOperation operation;
    operation.id = QStringLiteral("internal-m10x1.5");
    operation.opType = OperationType::Hole;
    operation.stage = OperationStage::FinishCut;
    operation.strategyId = strategy->id();
    operation.toolId = 31;
    operation.params = strategy->defaultParams();
    operation.params.set(QStringLiteral("depth"), 9.0);
    operation.params.set(QStringLiteral("roughPasses"), 2.0);
    operation.params.set(QStringLiteral("finishAllowance"), 0.2);
    operation.params.set(QStringLiteral("taperAngle"), 2.0);
    operation.params.set(QStringLiteral("finishFeedRate"), 180.0);
    operation.params.set(QStringLiteral("finishSpindleSpeed"), 2200.0);
    operation.holeFeature.center = QVector3D(10.0f, 20.0f, 0.0f);
    operation.holeFeature.radius = 5.0;
    operation.holeFeature.depth = 9.0;
    operation.holeFeature.pitch = 1.5;
    operation.holeFeature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    operation.holeFeature.region = FaceRegion::Front;
    operation.markToolpathValid();

    PostProcessorOptions options;
    options.programNumber = QStringLiteral("O2506");
    options.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
        QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    const ProgramGenerationResult generated = generator.generate(
        {operation}, Cq8PostProcessor(), options, {});
    if (!expect(generated.ok,
                "confirmed internal thread milling must generate final CQ8 code") ||
        !expect(generated.snapshot.gcodeText.contains(QStringLiteral("CNEXT_THREAD_MILL")) &&
                    generated.snapshot.gcodeText.contains(QStringLiteral("pass=rough-2")) &&
                    generated.snapshot.gcodeText.contains(QStringLiteral("pass=finish")) &&
                    generated.snapshot.gcodeText.contains(QStringLiteral("M30")),
                "final CQ8 code must retain thread traceability and safe termination")) {
        return 1;
    }

    SimulationController simulation;
    QVector<QVector3D> path;
    QVector<bool> rapid;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&path, &rapid](const QVector<QVector3D> &newPath,
                                     const QVector<bool> &newRapid) {
                         path = newPath;
                         rapid = newRapid;
                     });
    simulation.loadGCode(generated.snapshot.gcodeText);
    bool reachesBottom = false;
    for (int index = 0; index < rapid.size(); ++index) {
        if (rapid.at(index)) {
            continue;
        }
        const QVector3D &point = path.at(index + 1);
        const double radialDistance = std::hypot(point.x() - 10.0, point.y() - 20.0);
        if (!expect(radialDistance <= 3.001,
                    "thread cutting motion must remain inside the tool-center feature boundary")) {
            return 1;
        }
        reachesBottom = reachesBottom || std::abs(point.z() + 4.0f) < 1.0e-3f;
    }
    return expect(reachesBottom,
                  "final-code preview must reach the confirmed thread depth")
        ? 0
        : 1;
}
