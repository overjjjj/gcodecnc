#include "../src/gcode/GCodeSafetyValidator.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/services/AutoHolePlanningService.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
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
    }
    return condition;
}

ProcessTemplateLibrary makeLibrary()
{
    ProcessTemplateLibrary library;
    ProcessTemplateDefinition parameters;
    parameters.id = QStringLiteral("aluminum-drill");
    parameters.version = QStringLiteral("1");
    parameters.name = QStringLiteral("Aluminum drill defaults");
    parameters.source = ProcessParameterSource::MaterialTemplate;
    parameters.values.set(QStringLiteral("safeHeight"), 20.0);
    parameters.values.set(QStringLiteral("plungeHeight"), 2.0);
    parameters.values.set(QStringLiteral("peckDepth"), 3.0);
    parameters.values.set(QStringLiteral("spindleSpeed"), 1200.0);
    parameters.values.set(QStringLiteral("feedRate"), 90.0);
    QString error;
    library.add(parameters, &error);

    MachiningPlanDefinition plan;
    plan.id = QStringLiteral("blind-hole-drill");
    plan.version = QStringLiteral("1");
    plan.name = QStringLiteral("Blind hole drilling");
    plan.strategyId = QStringLiteral("hole_peck");
    plan.toolId = 7;
    plan.startExpression = QStringLiteral("AT");
    plan.depthExpression = QStringLiteral("H1-AT");
    plan.templateRefs = QStringList{QStringLiteral("aluminum-drill@1")};
    library.addPlan(plan, &error);
    return library;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    HoleFeature hole;
    hole.kind = FeatureKind::Hole;
    hole.subType = QStringLiteral("blind_hole");
    hole.center = QVector3D(10.0f, 20.0f, 0.0f);
    hole.axis = QVector3D(0.0f, 0.0f, 1.0f);
    hole.radius = 3.0;
    hole.depth = 12.0;
    hole.region = FaceRegion::Front;

    const QList<AutoHolePlan> plans =
        AutoHolePlanningService::BuildPlans(makeLibrary());
    QList<AutoHoleCandidate> candidates = AutoHolePlanningService::Recognize(
        QVector<MachiningFeature>{hole}, plans, {});
    candidates.first().selectedPlanId = plans.first().id;

    AutoHoleToolInfo toolInfo;
    toolInfo.id = 7;
    toolInfo.type = QStringLiteral("drill");
    toolInfo.diameter = 6.0;
    ProcessContext context;
    context.toolId = 7;
    context.toolTypeId = toolInfo.type;
    context.materialId = QStringLiteral("aluminum");
    context.setup.workOffset = QStringLiteral("G54");
    context.setup.origin.confirmed = true;
    AutoHoleConfirmationResult confirmed =
        AutoHolePlanningService::Confirm(
            candidates.first(), {{7, toolInfo}}, context, 0.2);
    if (!expect(confirmed.ok,
                "explicitly confirmed auto-hole draft must create a formal operation")) {
        return 1;
    }
    confirmed.operation.markToolpathValid();

    const auto strategy = std::make_shared<PeckDrillingStrategy>();
    ProgramGenerationService generator(
        [strategy](const QString &id) {
            return id == strategy->id()
                ? std::static_pointer_cast<StrategyBase>(strategy)
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            tool.id = id;
            tool.type = QStringLiteral("drill");
            tool.diameter = 6.0;
            tool.fluteLen = 30.0;
            tool.totalLen = 60.0;
            return tool;
        });

    PostProcessorOptions options;
    options.programNumber = QStringLiteral("O2606");
    options.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
        QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    const ProgramGenerationResult generated = generator.generate(
        {confirmed.operation}, Cq8PostProcessor(), options, {});
    if (!expect(generated.ok,
                "confirmed automatic-hole operation must generate final CQ8 code") ||
        !expect(generated.snapshot.sourceOperationIds ==
                    QStringList{confirmed.operation.id},
                "final program must trace back to the confirmed operation") ||
        !expect(generated.snapshot.gcodeText.contains(QStringLiteral("G98 G83")) &&
                    generated.snapshot.gcodeText.contains(QStringLiteral("M30")),
                "final CQ8 program must contain the drilling cycle and safe end") ||
        !expect(GCodeSafetyValidator::validate(
                    generated.snapshot.gcodeText).ok,
                "final CQ8 program must pass the output safety boundary")) {
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
    bool reachesConfirmedDepth = false;
    const double expectedBottomZ = hole.center.z() + hole.depth * 0.5 - 11.8;
    for (int index = 0; index < rapid.size(); ++index) {
        if (!rapid.at(index) &&
            std::abs(path.at(index + 1).z() - expectedBottomZ) < 1.0e-3f) {
            reachesConfirmedDepth = true;
        }
    }
    if (!expect(reachesConfirmedDepth,
                "final-code simulation must reach the formula-resolved depth")) {
        std::cerr << generated.snapshot.gcodeText.toStdString() << '\n';
        for (const QVector3D &point : path) {
            std::cerr << point.x() << ',' << point.y() << ',' << point.z() << '\n';
        }
        return 1;
    }
    return 0;
}
