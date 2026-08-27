#include "../src/gcode/GCodeSafetyValidator.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/postprocessor/FanucPostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/services/AutoHolePlanningService.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/hole/ChamferStrategy.h"
#include "../src/strategies/hole/PeckDrillingStrategy.h"

#include <QCoreApplication>

#include <cmath>
#include <iostream>
#include <memory>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

CompoundHoleFeature makeCountersink()
{
    CompoundHoleFeature feature;
    feature.geometryRef = QStringLiteral("feature:compound-cq8-countersink");
    feature.entryPoint = QVector3D(15.0f, 25.0f, 0.0f);
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.region = FaceRegion::Front;
    feature.confidence = 0.99;
    feature.layers = {
        {HoleLayerKind::ConicalCountersink, 10.0, 0.0, -2.0, false},
        {HoleLayerKind::Cylindrical, 6.0, -2.0, -14.0, false}};
    return feature;
}

CompoundHoleFeature makeThroughHole()
{
    CompoundHoleFeature feature;
    feature.geometryRef = QStringLiteral("feature:compound-cq8-through");
    feature.entryPoint = QVector3D(35.0f, 25.0f, 0.0f);
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.region = FaceRegion::Front;
    feature.confidence = 0.99;
    feature.layers = {
        {HoleLayerKind::Cylindrical, 6.0, 0.0, -12.0, true}};
    return feature;
}

AutoHolePlan countersinkPlan()
{
    AutoHolePlan plan;
    plan.id = QStringLiteral("countersink-cq8");
    plan.version = QStringLiteral("1");
    plan.shape = AutoHoleShape::Countersink;
    plan.formulaVariables = {{QStringLiteral("T1"), 7.0},
                             {QStringLiteral("T2"), 8.0}};
    plan.steps = {
        {QStringLiteral("drill"), QStringLiteral("hole_peck"),
         QStringLiteral("T1"), QStringLiteral("0"), QStringLiteral("H1+H2"), 1, false},
        {QStringLiteral("countersink"), QStringLiteral("hole_chamfer"),
         QStringLiteral("T2"), QStringLiteral("0"), QStringLiteral("H1"), 0, true}};
    plan.layers.moduleRule.set(QStringLiteral("safeHeight"), 20.0);
    plan.layers.moduleRule.set(QStringLiteral("plungeHeight"), 2.0);
    plan.layers.moduleRule.set(QStringLiteral("peckDepth"), 3.0);
    plan.layers.moduleRule.set(QStringLiteral("feedRate"), 90.0);
    plan.layers.moduleRule.set(QStringLiteral("spindleSpeed"), 1200.0);
    return plan;
}

AutoHolePlan throughPlan()
{
    AutoHolePlan plan;
    plan.id = QStringLiteral("through-cq8");
    plan.version = QStringLiteral("1");
    plan.shape = AutoHoleShape::Single;
    plan.formulaVariables = {{QStringLiteral("T1"), 7.0}};
    plan.steps = {{QStringLiteral("through-drill"), QStringLiteral("hole_peck"),
                   QStringLiteral("T1"), QStringLiteral("0"),
                   QStringLiteral("H1+AT"), 0, false}};
    plan.layers.moduleRule.set(QStringLiteral("safeHeight"), 20.0);
    plan.layers.moduleRule.set(QStringLiteral("plungeHeight"), 2.0);
    plan.layers.moduleRule.set(QStringLiteral("peckDepth"), 3.0);
    plan.layers.moduleRule.set(QStringLiteral("feedRate"), 90.0);
    plan.layers.moduleRule.set(QStringLiteral("spindleSpeed"), 1200.0);
    return plan;
}

QMap<int, AutoHoleToolInfo> makeTools()
{
    AutoHoleToolInfo drill;
    drill.id = 7;
    drill.type = QStringLiteral("drill");
    drill.diameter = 6.0;
    drill.fluteLength = 30.0;
    drill.totalLength = 60.0;
    AutoHoleToolInfo chamfer;
    chamfer.id = 8;
    chamfer.type = QStringLiteral("chamfer_mill");
    chamfer.diameter = 6.0;
    chamfer.fluteLength = 8.0;
    chamfer.totalLength = 50.0;
    return {{7, drill}, {8, chamfer}};
}

ProcessContext makeContext()
{
    ProcessContext context;
    context.toolId = 7;
    context.toolTypeId = QStringLiteral("drill");
    context.materialId = QStringLiteral("aluminum");
    context.setup.workOffset = QStringLiteral("G54");
    context.setup.origin.confirmed = true;
    return context;
}

ToolEntry tool(int id)
{
    ToolEntry value;
    value.id = id;
    value.type = id == 7 ? QStringLiteral("drill") : QStringLiteral("chamfer_mill");
    value.diameter = 6.0;
    value.fluteLen = id == 7 ? 30.0 : 8.0;
    value.totalLen = id == 7 ? 60.0 : 50.0;
    return value;
}

void markValid(QList<MachiningOperation> *operations)
{
    for (MachiningOperation &operation : *operations) {
        operation.markToolpathValid();
    }
}

bool reachesDepth(const QString &gcode, double depth)
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
    for (int index = 0; index < rapid.size(); ++index) {
        if (!rapid.at(index) && std::abs(path.at(index + 1).z() + depth) < 0.02f) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QList<AutoHoleCandidate> candidates = AutoHolePlanningService::RecognizeCompound(
        {makeCountersink()}, {countersinkPlan()}, {});
    candidates.first().selectedPlanId = countersinkPlan().id;
    AutoHoleConfirmationResult confirmed = AutoHolePlanningService::ConfirmCompound(
        candidates.first(), makeTools(), makeContext(), 0.2);
    if (!expect(confirmed.ok && confirmed.operations.size() == 2,
                "confirmed countersink draft must produce two formal operations")) {
        return 1;
    }
    markValid(&confirmed.operations);

    const auto drill = std::make_shared<PeckDrillingStrategy>();
    const auto chamfer = std::make_shared<ChamferStrategy>();
    ProgramGenerationService generator(
        [drill, chamfer](const QString &id) {
            if (id == drill->id()) return std::static_pointer_cast<StrategyBase>(drill);
            if (id == chamfer->id()) return std::static_pointer_cast<StrategyBase>(chamfer);
            return std::shared_ptr<StrategyBase>();
        },
        [](int id) { return tool(id); });
    PostProcessorOptions options;
    options.programNumber = QStringLiteral("O2908");
    options.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
        QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};

    const ProgramGenerationResult cq8 = generator.generate(
        confirmed.operations, Cq8PostProcessor(), options, {});
    if (!cq8.ok) {
        std::cerr << cq8.errors.join(QLatin1Char('\n')).toStdString() << '\n';
    }
    if (!expect(cq8.ok && GCodeSafetyValidator::validate(cq8.snapshot.gcodeText).ok &&
                    cq8.snapshot.gcodeText.contains(QStringLiteral("G98 G83")) &&
                    cq8.snapshot.gcodeText.contains(QStringLiteral("Chamfer hole")) &&
                    reachesDepth(cq8.snapshot.expandedGcodeText.isEmpty()
                                     ? cq8.snapshot.gcodeText : cq8.snapshot.expandedGcodeText, 14.0),
                "CQ8 output must safely drill the core and machine the countersink from final code")) {
        return 1;
    }
    const ProgramGenerationResult fanuc = generator.generate(
        confirmed.operations, FanucPostProcessor(), options, {});
    const ProgramGenerationResult siemens = generator.generate(
        confirmed.operations, SiemensPostProcessor(), options, {});
    if (!expect(fanuc.ok && siemens.ok &&
                    fanuc.snapshot.gcodeText.contains(QStringLiteral("%\nO2908")) &&
                    siemens.snapshot.gcodeText.contains(QStringLiteral("Post-processor: Siemens 840D")) &&
                    GCodeSafetyValidator::validate(fanuc.snapshot.gcodeText).ok &&
                    GCodeSafetyValidator::validate(siemens.snapshot.gcodeText).ok,
                "Fanuc and Siemens final output must preserve controller-specific headers and safety")) {
        return 1;
    }

    QList<AutoHoleCandidate> throughCandidates = AutoHolePlanningService::RecognizeCompound(
        {makeThroughHole()}, {throughPlan()}, {});
    throughCandidates.first().selectedPlanId = throughPlan().id;
    AutoHoleConfirmationResult through = AutoHolePlanningService::ConfirmCompound(
        throughCandidates.first(), makeTools(), makeContext(), 0.2);
    if (!expect(through.ok && through.operations.size() == 1,
                "through-hole allowance must remain a valid controlled draft confirmation")) {
        return 1;
    }
    markValid(&through.operations);
    const ProgramGenerationResult throughCode = generator.generate(
        through.operations, Cq8PostProcessor(), options, {});
    if (!expect(throughCode.ok && reachesDepth(
                    throughCode.snapshot.expandedGcodeText.isEmpty()
                        ? throughCode.snapshot.gcodeText : throughCode.snapshot.expandedGcodeText,
                    12.2),
                "through-hole final code must use the formula-confirmed breakthrough allowance")) {
        return 1;
    }
    return 0;
}
