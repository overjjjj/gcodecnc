#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/mill/BlindSlotMillingStrategy.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

static int expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const auto strategy = std::make_shared<BlindSlotMillingStrategy>();
    ProgramGenerationService generator(
        [strategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == strategy->id() ? strategy : std::shared_ptr<StrategyBase>();
        },
        [](int toolId) {
            ToolEntry tool;
            if (toolId == 6) {
                tool.id = 6;
                tool.type = QStringLiteral("end_mill");
                tool.diameter = 6.0;
                tool.fluteLen = 20.0;
                tool.totalLen = 60.0;
            }
            return tool;
        });

    MachiningOperation operation;
    operation.id = QStringLiteral("front-blind-slot");
    operation.opType = OperationType::Contour;
    operation.stage = OperationStage::RoughCut;
    operation.strategyId = strategy->id();
    operation.toolId = 6;
    operation.params = strategy->defaultParams();
    operation.contourFeature.subType = QStringLiteral("blind_slot");
    operation.contourFeature.center = QVector3D(0.0f, 0.0f, 0.0f);
    operation.contourFeature.length = 40.0;
    operation.contourFeature.width = 12.0;
    operation.contourFeature.depth = 4.0;
    operation.contourFeature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    operation.contourFeature.region = FaceRegion::Front;
    operation.markToolpathValid();

    PostProcessorOptions options;
    options.programNumber = QStringLiteral("O2401");
    options.workOffset = QStringLiteral("G54");
    options.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
        QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    ProgramGenerationSnapshotOptions snapshotOptions;
    snapshotOptions.name = QStringLiteral("front blind slot");
    snapshotOptions.machineProfile.id = QStringLiteral("cq8-interface");

    Cq8PostProcessor postProcessor;
    const ProgramGenerationResult generated = generator.generate(
        {operation}, postProcessor, options, snapshotOptions);
    if (expect(generated.ok, "front blind slot must generate final CQ8 G-code") ||
        expect(generated.snapshot.gcodeText.contains(QStringLiteral("G17 G40 G49 G80")) &&
                   generated.snapshot.gcodeText.contains(QStringLiteral("G21")) &&
                   generated.snapshot.gcodeText.contains(QStringLiteral("G90")) &&
                   generated.snapshot.gcodeText.contains(QStringLiteral("G54")) &&
                   generated.snapshot.gcodeText.contains(QStringLiteral("G94")) &&
                   generated.snapshot.gcodeText.contains(QStringLiteral("M5")) &&
                   generated.snapshot.gcodeText.contains(QStringLiteral("M9")) &&
                   generated.snapshot.gcodeText.contains(QStringLiteral("M30")),
               "final slot program must retain safe startup and shutdown blocks")) {
        return 1;
    }

    SimulationController simulation;
    QVector<QVector3D> path;
    QVector<bool> rapidSegments;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&path, &rapidSegments](const QVector<QVector3D> &newPath,
                                             const QVector<bool> &newRapidSegments) {
        path = newPath;
        rapidSegments = newRapidSegments;
    });
    simulation.loadGCode(generated.snapshot.gcodeText);

    bool reachesBottom = false;
    for (int i = 0; i < rapidSegments.size(); ++i) {
        const QVector3D &from = path.at(i);
        const QVector3D &to = path.at(i + 1);
        if (rapidSegments.at(i) &&
            (std::abs(from.x() - to.x()) > 1.0e-3f || std::abs(from.y() - to.y()) > 1.0e-3f) &&
            std::min(from.z(), to.z()) < 3.0f - 1.0e-3f) {
            return expect(false, "slot rapid XY moves must remain at or above the feed start height");
        }
        if (!rapidSegments.at(i) && std::abs(to.z() + 4.0f) < 1.0e-3f) {
            reachesBottom = true;
        }
    }
    return expect(reachesBottom,
                  "final CQ8 code must simulate a feed move to the confirmed slot bottom");
}
