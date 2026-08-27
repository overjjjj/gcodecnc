#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/hole/PeckDrillingStrategy.h"
#include "../src/strategies/hole/ReamingStrategy.h"

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
    const auto peck = std::make_shared<PeckDrillingStrategy>();
    const auto ream = std::make_shared<ReamingStrategy>();
    ProgramGenerationService generator(
        [peck, ream](const QString &id) {
            if (id == peck->id()) return std::static_pointer_cast<StrategyBase>(peck);
            if (id == ream->id()) return std::static_pointer_cast<StrategyBase>(ream);
            return std::shared_ptr<StrategyBase>();
        },
        [](int id) { ToolEntry tool; tool.id = id; tool.fluteLen = 30.0; tool.totalLen = 70.0;
                      if (id == 9) { tool.type = QStringLiteral("drill"); tool.diameter = 9.5; }
                      if (id == 10) { tool.type = QStringLiteral("reamer"); tool.diameter = 10.0; }
                      return tool; });
    HoleFeature hole; hole.center = QVector3D(20.0f, 30.0f, -6.0f); hole.radius = 5.0;
    hole.depth = 12.0; hole.axis = QVector3D(0, 0, 1); hole.region = FaceRegion::Front;
    MachiningOperation peckOperation;
    peckOperation.id = QStringLiteral("peck-d10"); peckOperation.opType = OperationType::Hole;
    peckOperation.stage = OperationStage::DeepHole; peckOperation.strategyId = peck->id();
    peckOperation.toolId = 9; peckOperation.params = peck->defaultParams(); peckOperation.holeFeature = hole;
    MachiningOperation reamOperation = peckOperation;
    reamOperation.id = QStringLiteral("ream-d10"); reamOperation.stage = OperationStage::FinishCut;
    reamOperation.strategyId = ream->id(); reamOperation.toolId = 10;
    reamOperation.params = ream->defaultParams();
    peckOperation.markToolpathValid(); reamOperation.markToolpathValid();
    PostProcessorOptions options; options.programNumber = QStringLiteral("O2502");
    options.safeStartBlocks = QStringList{QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"), QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    Cq8PostProcessor post; ProgramGenerationSnapshotOptions snapshot;
    const ProgramGenerationResult generated = generator.generate({peckOperation, reamOperation}, post, options, snapshot);
    if (!expect(generated.ok, "explicit peck then ream operations must generate final CQ8 code")) return 1;
    const QString &gcode = generated.snapshot.gcodeText;
    const int peckIndex = gcode.indexOf(QStringLiteral("G98 G83"));
    const int reamIndex = gcode.indexOf(QStringLiteral("G98 G85"));
    const int reamToolIndex = gcode.indexOf(QStringLiteral("T10 M6"));
    if (!expect(peckIndex >= 0 && reamIndex > peckIndex && reamToolIndex > peckIndex,
                "final CQ8 code must keep operator-confirmed peck-before-ream order") ||
        !expect(gcode.mid(peckIndex, reamToolIndex - peckIndex).contains(QStringLiteral("M5")) &&
                    gcode.mid(peckIndex, reamToolIndex - peckIndex).contains(QStringLiteral("M9")),
                "reamer tool change must stop spindle and coolant") ||
        !expect(gcode.contains(QStringLiteral("G80")) && gcode.contains(QStringLiteral("M30")),
                "final peck-ream program must cancel cycles and end safely")) return 1;
    SimulationController simulation; QVector<QVector3D> path; QVector<bool> rapid;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&path, &rapid](const QVector<QVector3D> &p, const QVector<bool> &r) { path = p; rapid = r; });
    simulation.loadGCode(gcode);
    int feedMovesAtBottom = 0;
    for (int i = 0; i < rapid.size(); ++i) {
        if (!rapid.at(i) && std::abs(path.at(i + 1).z() + 12.0f) < 1e-3f) ++feedMovesAtBottom;
    }
    return expect(feedMovesAtBottom >= 2,
                  "final CQ8 simulation must cut the confirmed bottom for both peck and ream stages") ? 0 : 1;
}
