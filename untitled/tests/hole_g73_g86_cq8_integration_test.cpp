#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/hole/BoringG86Strategy.h"
#include "../src/strategies/hole/HighSpeedPeckDrillingStrategy.h"

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

MachiningOperation makeHoleOperation(const QString &id,
                                     const QString &strategyId,
                                     int toolId,
                                     const StrategyParams &params)
{
    MachiningOperation operation;
    operation.id = id;
    operation.opType = OperationType::Hole;
    operation.strategyId = strategyId;
    operation.toolId = toolId;
    operation.params = params;
    operation.holeFeature.center = QVector3D(20.0f, 30.0f, 0.0f);
    operation.holeFeature.radius = 6.0;
    operation.holeFeature.depth = 12.0;
    operation.holeFeature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    operation.holeFeature.region = FaceRegion::Front;
    operation.markToolpathValid();
    return operation;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const auto peck = std::make_shared<HighSpeedPeckDrillingStrategy>();
    const auto bore = std::make_shared<BoringG86Strategy>();
    ProgramGenerationService generator(
        [peck, bore](const QString &id) {
            if (id == peck->id()) {
                return std::static_pointer_cast<StrategyBase>(peck);
            }
            if (id == bore->id()) {
                return std::static_pointer_cast<StrategyBase>(bore);
            }
            return std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            tool.id = id;
            tool.fluteLen = 40.0;
            tool.totalLen = 80.0;
            if (id == 7) {
                tool.type = QStringLiteral("drill");
                tool.diameter = 10.0;
            } else if (id == 8) {
                tool.type = QStringLiteral("boring_bar");
                tool.diameter = 8.0;
            }
            return tool;
        });

    MachiningOperation peckOperation = makeHoleOperation(
        QStringLiteral("g73-d12"), peck->id(), 7, peck->defaultParams());
    peckOperation.stage = OperationStage::DeepHole;
    peckOperation.params.set(QStringLiteral("depth"), 12.0);
    MachiningOperation boreOperation = makeHoleOperation(
        QStringLiteral("g86-d12"), bore->id(), 8, bore->defaultParams());
    boreOperation.stage = OperationStage::FinishCut;
    boreOperation.params.set(QStringLiteral("depth"), 12.0);
    boreOperation.params.set(QStringLiteral("dwellTime"), 500.0);

    PostProcessorOptions options;
    options.programNumber = QStringLiteral("O2505");
    options.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
        QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    Cq8PostProcessor post;
    ProgramGenerationSnapshotOptions snapshot;
    const ProgramGenerationResult generated = generator.generate(
        {peckOperation, boreOperation}, post, options, snapshot);
    if (!expect(generated.ok,
                "confirmed G73 then G86 operations must generate final CQ8 code")) {
        for (const QString &error : generated.errors) {
            std::cerr << error.toStdString() << '\n';
        }
        return 1;
    }

    const QString &gcode = generated.snapshot.gcodeText;
    const int peckIndex = gcode.indexOf(QStringLiteral("G98 G73"));
    const int boreIndex = gcode.indexOf(QStringLiteral("G98 G86"));
    if (!expect(peckIndex >= 0 && boreIndex > peckIndex,
                "final CQ8 code must preserve confirmed G73-before-G86 order") ||
        !expect(gcode.contains(QStringLiteral("Q3.000")) &&
                    gcode.contains(QStringLiteral("P500")),
                "final fixed cycles must retain their dedicated parameters") ||
        !expect(gcode.contains(QStringLiteral("G80")) &&
                    gcode.contains(QStringLiteral("M30")),
                "final fixed-cycle program must cancel cycles and end safely")) {
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
    simulation.loadGCode(gcode);
    int feedMovesAtBottom = 0;
    for (int index = 0; index < rapid.size(); ++index) {
        if (!rapid.at(index) &&
            std::abs(path.at(index + 1).z() + 6.0f) < 1e-3f) {
            ++feedMovesAtBottom;
        }
    }
    if (!expect(feedMovesAtBottom >= 2,
                "final-code preview must reach both confirmed cycle depths")) {
        std::cerr << gcode.toStdString() << '\n';
        for (int index = 0; index < rapid.size(); ++index) {
            if (!rapid.at(index)) {
                const QVector3D &point = path.at(index + 1);
                std::cerr << "feed " << point.x() << ',' << point.y() << ','
                          << point.z() << '\n';
            }
        }
        return 1;
    }
    return 0;
}
