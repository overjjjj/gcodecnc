#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/hole/PeckDrillingStrategy.h"
#include "../src/strategies/hole/SpotDrillingStrategy.h"

#include <QCoreApplication>
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
    const auto spot = std::make_shared<SpotDrillingStrategy>();
    const auto peck = std::make_shared<PeckDrillingStrategy>();
    ProgramGenerationService generator(
        [spot, peck](const QString &id) {
            if (id == spot->id()) return std::static_pointer_cast<StrategyBase>(spot);
            if (id == peck->id()) return std::static_pointer_cast<StrategyBase>(peck);
            return std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool; tool.id = id;
            if (id == 4) { tool.type = QStringLiteral("spot_drill"); tool.diameter = 4.0; tool.fluteLen = 10.0; tool.totalLen = 50.0; }
            if (id == 10) { tool.type = QStringLiteral("drill"); tool.diameter = 10.0; tool.fluteLen = 30.0; tool.totalLen = 70.0; }
            return tool;
        });
    HoleFeature hole; hole.center = QVector3D(20.0f, 30.0f, -6.0f); hole.radius = 5.0;
    hole.depth = 12.0; hole.axis = QVector3D(0, 0, 1); hole.region = FaceRegion::Front;
    MachiningOperation spotOperation;
    spotOperation.id = QStringLiteral("spot-d10"); spotOperation.opType = OperationType::Hole;
    spotOperation.stage = OperationStage::Setup; spotOperation.strategyId = spot->id();
    spotOperation.toolId = 4; spotOperation.params = spot->defaultParams(); spotOperation.holeFeature = hole;
    MachiningOperation peckOperation = spotOperation;
    peckOperation.id = QStringLiteral("peck-d10"); peckOperation.stage = OperationStage::DeepHole;
    peckOperation.strategyId = peck->id(); peckOperation.toolId = 10;
    peckOperation.params = peck->defaultParams(); peckOperation.holeFeature = hole;
    PostProcessorOptions options; options.programNumber = QStringLiteral("O2501");
    options.safeStartBlocks = QStringList{QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"), QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    Cq8PostProcessor post; ProgramGenerationSnapshotOptions snapshot;
    const ProgramGenerationResult generated = generator.generate({spotOperation, peckOperation}, post, options, snapshot);
    if (!expect(generated.ok, "explicit spot then peck operations must generate one final CQ8 program")) return 1;
    const QString &gcode = generated.snapshot.gcodeText;
    const int spotIndex = gcode.indexOf(QStringLiteral("G98 G81"));
    const int peckIndex = gcode.indexOf(QStringLiteral("G98 G83"));
    const int secondToolIndex = gcode.indexOf(QStringLiteral("T10 M6"));
    if (!expect(spotIndex >= 0 && peckIndex > spotIndex && secondToolIndex > spotIndex,
                "final CQ8 code must keep operator-confirmed spot-before-peck order") ||
        !expect(gcode.mid(spotIndex, secondToolIndex - spotIndex).contains(QStringLiteral("M5")) &&
                    gcode.mid(spotIndex, secondToolIndex - spotIndex).contains(QStringLiteral("M9")),
                "tool change between confirmed hole stages must stop spindle and coolant") ||
        !expect(gcode.contains(QStringLiteral("G80")) && gcode.contains(QStringLiteral("M30")),
                "final hole-chain program must cancel fixed cycles and end safely")) return 1;
    SimulationController simulation; QVector<QVector3D> path; QVector<bool> rapid;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&path, &rapid](const QVector<QVector3D> &p, const QVector<bool> &r) { path = p; rapid = r; });
    simulation.loadGCode(gcode);
    bool spotDepth = false, drillDepth = false;
    for (int i = 0; i < rapid.size(); ++i) {
        if (rapid.at(i)) continue;
        const QVector3D &to = path.at(i + 1);
        spotDepth |= std::abs(to.x() - 20.0f) < 1e-3f && std::abs(to.y() - 30.0f) < 1e-3f && std::abs(to.z() + 2.0f) < 1e-3f;
        drillDepth |= std::abs(to.x() - 20.0f) < 1e-3f && std::abs(to.y() - 30.0f) < 1e-3f && std::abs(to.z() + 12.0f) < 1e-3f;
    }
    return expect(spotDepth && drillDepth,
                  "final CQ8 simulation must include both spot and peck cutting depths") ? 0 : 1;
}
