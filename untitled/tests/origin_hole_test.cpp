#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/strategies/hole/ChamferStrategy.h"
#include "../src/strategies/hole/PeckDrillingStrategy.h"

#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <memory>
#include <QRegularExpression>

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
    const auto chamfer = std::make_shared<ChamferStrategy>();
    ProgramGenerationService generator(
        [peck, chamfer](const QString &id) {
            if (id == peck->id()) return std::static_pointer_cast<StrategyBase>(peck);
            if (id == chamfer->id()) return std::static_pointer_cast<StrategyBase>(chamfer);
            return std::shared_ptr<StrategyBase>();
        },
        [](int id) { ToolEntry tool; tool.id = id; tool.fluteLen = 30.0; tool.totalLen = 70.0;
                      if (id == 10) { tool.type = QStringLiteral("drill"); tool.diameter = 10.0; }
                      if (id == 16) { tool.type = QStringLiteral("chamfer_mill"); tool.diameter = 6.0; }
                      return tool; });
    HoleFeature hole; hole.center = QVector3D(20.0f, 30.0f, -6.0f); hole.radius = 5.0;
    hole.depth = 12.0; hole.axis = QVector3D(0, 0, 1); hole.region = FaceRegion::Front;
    MachiningOperation peckOperation;
    peckOperation.id = QStringLiteral("peck-d10"); peckOperation.opType = OperationType::Hole;
    peckOperation.stage = OperationStage::DeepHole; peckOperation.strategyId = peck->id();
    peckOperation.toolId = 10; peckOperation.params = peck->defaultParams(); peckOperation.holeFeature = hole;
    MachiningOperation chamferOperation = peckOperation;
    chamferOperation.id = QStringLiteral("chamfer-d10"); chamferOperation.stage = OperationStage::Cleanup;
    chamferOperation.strategyId = chamfer->id(); chamferOperation.toolId = 16;
    chamferOperation.params = chamfer->defaultParams();
    PostProcessorOptions options; options.programNumber = QStringLiteral("O2503");
    options.safeStartBlocks = QStringList{QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"), QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    Cq8PostProcessor post; ProgramGenerationSnapshotOptions snapshot;
    const ProgramGenerationResult generated = generator.generate({peckOperation, chamferOperation}, post, options, snapshot);
    if (!expect(generated.ok, "explicit peck then chamfer operations must generate final CQ8 code")) return 1;
    snapshot.modelOrigin={10,-5,2};
    const auto shifted=generator.generate({peckOperation,chamferOperation},post,options,snapshot);
    if(!expect(shifted.ok,"Nonzero origin holes must generate CQ8"))return 1;
    const auto shiftedNc=shifted.snapshot.expandedGcodeText.isEmpty()?shifted.snapshot.gcodeText:shifted.snapshot.expandedGcodeText;
    SimulationController shiftedSimulation;shiftedSimulation.loadGCode(shiftedNc,false);
    bool translatedBottom=false;
    for(const auto &move:shiftedSimulation.moves())if(!move.isRapid&&move.toKnownAxes==7)
        translatedBottom |= std::abs(move.to.x()-10)<1e-9&&std::abs(move.to.y()-35)<1e-9&&std::abs(move.to.z()+14)<1e-9;
    if(!expect(translatedBottom,"Hole positions and bottom translate by minus origin"))return 1;
    const QRegularExpression ij("[IJ][+-]?[0-9]+(?:\\.[0-9]+)?");
    auto increments=[&](const QString &nc){QStringList values;auto matches=ij.globalMatch(nc);while(matches.hasNext())values<<matches.next().captured();return values;};
    if(!expect(!increments(shiftedNc).isEmpty()&&increments(shiftedNc)==increments(generated.snapshot.gcodeText),"Circular chamfer IJ must remain incremental"))return 1;
    bool shiftedBottom=false,shiftedChamfer=false;
    for(const auto &move:shiftedSimulation.moves())if(!move.isRapid){shiftedBottom|=std::abs(move.to.z()+14)<1e-3;shiftedChamfer|=std::abs(move.to.z()+3)<1e-3;}
    if(!expect(shiftedSimulation.motionError().isEmpty()&&shiftedBottom&&shiftedChamfer,"Final shifted hole cycle and IJ arc parse at translated depths"))return 1;
    const QString &gcode = generated.snapshot.gcodeText;
    const int peckIndex = gcode.indexOf(QStringLiteral("G98 G83"));
    const int chamferIndex = gcode.indexOf(QStringLiteral("Chamfer hole D10.000"));
    const int chamferToolIndex = gcode.indexOf(QStringLiteral("T16 M6"));
    if (!expect(peckIndex >= 0 && chamferIndex > peckIndex && chamferToolIndex > peckIndex,
                "final CQ8 code must keep operator-confirmed peck-before-chamfer order") ||
        !expect(gcode.mid(peckIndex, chamferToolIndex - peckIndex).contains(QStringLiteral("M5")) &&
                    gcode.mid(peckIndex, chamferToolIndex - peckIndex).contains(QStringLiteral("M9")),
                "chamfer tool change must stop spindle and coolant") ||
        !expect(gcode.contains(QStringLiteral("G2")) && gcode.contains(QStringLiteral("M30")),
                "final chamfer program must retain circular cutting and safe program end")) return 1;
    SimulationController simulation; QVector<QVector3D> path; QVector<bool> rapid;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&path, &rapid](const QVector<QVector3D> &p, const QVector<bool> &r) { path = p; rapid = r; });
    simulation.loadGCode(gcode);
    bool drilledBottom = false, chamferDepth = false, chamferArc = false;
    for (int i = 0; i < rapid.size(); ++i) {
        if (rapid.at(i)) continue;
        const QVector3D &from = path.at(i), &to = path.at(i + 1);
        drilledBottom |= std::abs(to.z() + 12.0f) < 1e-3f;
        chamferDepth |= std::abs(to.z() + 1.0f) < 1e-3f;
        chamferArc |= std::abs(from.z() + 1.0f) < 1e-3f && std::abs(to.z() + 1.0f) < 1e-3f &&
                      (from - to).length() > 1.0e-3f;
    }
    return expect(drilledBottom && chamferDepth && chamferArc,
                  "final CQ8 simulation must include drill bottom and shallow circular chamfer cutting") ? 0 : 1;
}
