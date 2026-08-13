#include "../src/gcode/GCodeSafetyValidator.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/mill/TaperedSlotMillingStrategy.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

static bool expect(bool condition, const char *message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const auto strategy = std::make_shared<TaperedSlotMillingStrategy>();
    ProgramGenerationService generator(
        [strategy](const QString &id) { return id == strategy->id() ? strategy : std::shared_ptr<StrategyBase>(); },
        [](int id) { ToolEntry tool; if (id == 6) { tool.id = 6; tool.type = QStringLiteral("end_mill"); tool.diameter = 6.0; tool.fluteLen = 20.0; tool.totalLen = 60.0; } return tool; });

    MachiningOperation operation;
    operation.id = QStringLiteral("front-tapered-slot"); operation.opType = OperationType::Contour;
    operation.stage = OperationStage::RoughCut; operation.strategyId = strategy->id(); operation.toolId = 6;
    operation.params = strategy->defaultParams(); operation.contourFeature.subType = QStringLiteral("straight_slot");
    operation.contourFeature.center = QVector3D(); operation.contourFeature.length = 40.0;
    operation.contourFeature.width = 12.0; operation.contourFeature.depth = 4.0;
    operation.contourFeature.axis = QVector3D(0, 0, 1); operation.contourFeature.region = FaceRegion::Front;

    PostProcessorOptions options; options.programNumber = QStringLiteral("O2403");
    options.safeStartBlocks = QStringList{QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"), QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    ProgramGenerationSnapshotOptions snapshot; Cq8PostProcessor post;
    const ProgramGenerationResult generated = generator.generate({operation}, post, options, snapshot);
    if (!expect(generated.ok, "front tapered slot must generate final CQ8 G-code") ||
        !expect(generated.snapshot.gcodeText.contains(QStringLiteral("Tapered slot floor finish")),
                "final CQ8 code must retain the tapered floor-finish stage") ||
        !expect(GCodeSafetyValidator::validate(generated.snapshot.gcodeText).ok,
                "final tapered-slot CQ8 code must pass the safety gate")) return 1;

    SimulationController simulation; QVector<QVector3D> path; QVector<bool> rapid;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&path, &rapid](const QVector<QVector3D> &p, const QVector<bool> &r) { path = p; rapid = r; });
    simulation.loadGCode(generated.snapshot.gcodeText);
    bool reachesTop = false, reachesBottom = false, hasSlopeCut = false;
    for (int i = 0; i < rapid.size(); ++i) {
        const QVector3D &from = path.at(i), &to = path.at(i + 1);
        if (rapid.at(i) && (std::abs(from.x() - to.x()) > 1e-3f || std::abs(from.y() - to.y()) > 1e-3f) &&
            std::min(from.z(), to.z()) < 3.0f - 1e-3f) return expect(false, "tapered-slot rapid XY must stay above feed height") ? 0 : 1;
        if (!rapid.at(i)) {
            reachesTop |= std::abs(to.z()) < 1e-3f;
            reachesBottom |= std::abs(to.z() + 4.0f) < 1e-3f;
            hasSlopeCut |= std::abs(from.z() - to.z()) > 1e-3f &&
                           std::abs(from.x() - to.x()) > 1e-3f;
        }
    }
    return expect(reachesTop && reachesBottom && hasSlopeCut,
                  "final CQ8 simulation must contain the tapered top-to-bottom cutting slope") ? 0 : 1;
}
