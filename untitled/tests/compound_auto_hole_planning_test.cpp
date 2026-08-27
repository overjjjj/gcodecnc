#include "../src/core/CompoundHoleFeature.h"
#include "../src/services/AutoHolePlanningService.h"

#include <QCoreApplication>

#include <cmath>
#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

CompoundHoleFeature countersunkHole()
{
    CompoundHoleFeature feature;
    feature.geometryRef = QStringLiteral("feature:compound-countersink-1");
    feature.entryPoint = QVector3D(10.0f, 20.0f, 0.0f);
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.region = FaceRegion::Front;
    feature.confidence = 0.98;
    feature.layers = {
        {HoleLayerKind::ConicalCountersink, 10.0, 0.0, -2.0, false},
        {HoleLayerKind::Cylindrical, 6.0, -2.0, -14.0, false}};
    return feature;
}

AutoHolePlan countersinkPlan()
{
    AutoHolePlan plan;
    plan.id = QStringLiteral("countersink-drill");
    plan.version = QStringLiteral("1");
    plan.shape = AutoHoleShape::Countersink;
    plan.formulaVariables = {{QStringLiteral("T1"), 7.0},
                             {QStringLiteral("T2"), 8.0}};
    plan.steps = {
        {QStringLiteral("core-drill"), QStringLiteral("hole_peck"),
         QStringLiteral("T1"), QStringLiteral("0"), QStringLiteral("H1+H2"),
         1, false},
        {QStringLiteral("countersink"), QStringLiteral("hole_chamfer"),
         QStringLiteral("T2"), QStringLiteral("0"), QStringLiteral("H1"),
         0, true}};
    plan.layers.moduleRule.set(QStringLiteral("safeHeight"), 20.0);
    plan.layers.moduleRule.set(QStringLiteral("plungeHeight"), 2.0);
    plan.layers.moduleRule.set(QStringLiteral("peckDepth"), 3.0);
    plan.layers.moduleRule.set(QStringLiteral("feedRate"), 90.0);
    plan.layers.moduleRule.set(QStringLiteral("spindleSpeed"), 1200.0);
    return plan;
}

QMap<int, AutoHoleToolInfo> tools()
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

ProcessContext context()
{
    ProcessContext value;
    value.toolId = 7;
    value.toolTypeId = QStringLiteral("drill");
    value.materialId = QStringLiteral("aluminum");
    value.setup.workOffset = QStringLiteral("G54");
    value.setup.origin.confirmed = true;
    return value;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const CompoundHoleFeature feature = countersunkHole();
    const AutoHolePlan plan = countersinkPlan();
    const QList<AutoHoleCandidate> candidates =
        AutoHolePlanningService::RecognizeCompound(
            {feature}, {plan}, {});
    if (!expect(candidates.size() == 1 &&
                    candidates.first().state == AutoCandidateState::Draft &&
                    candidates.first().compoundHole.layers.size() == 2,
                "a front-facing closed two-layer countersink must become a draft")) {
        return 1;
    }

    AutoHoleCandidate selected = candidates.first();
    selected.selectedPlanId = plan.id;
    const AutoHoleConfirmationResult confirmed =
        AutoHolePlanningService::ConfirmCompound(
            selected, tools(), context(), 0.2);
    if (!confirmed.ok) {
        std::cerr << confirmed.errors.join(QLatin1Char('\n')).toStdString() << '\n';
    }
    if (!expect(confirmed.ok && confirmed.operations.size() == 2 &&
                    confirmed.operations.at(0).strategyId == QStringLiteral("hole_peck") &&
                    confirmed.operations.at(1).strategyId == QStringLiteral("hole_chamfer") &&
                    confirmed.operations.at(0).geometryRefs ==
                        QStringList{feature.geometryRef} &&
                    std::abs(confirmed.operations.at(0).params.get(
                        QStringLiteral("depth")) - 14.0) < 1.0e-9,
                "explicit confirmation must create an ordered same-geometry operation chain")) {
        return 1;
    }

    AutoHoleCandidate cancelledCountersink = selected;
    cancelledCountersink.cancelCountersink = true;
    const AutoHoleConfirmationResult coreOnly =
        AutoHolePlanningService::ConfirmCompound(
            cancelledCountersink, tools(), context(), 0.2);
    if (!expect(coreOnly.ok && coreOnly.operations.size() == 1 &&
                    coreOnly.operations.first().strategyId == QStringLiteral("hole_peck"),
                "cancelling countersink processing must retain only the core-hole operation")) {
        return 1;
    }

    AutoHoleCandidate edited = selected;
    edited.manuallyEdited = true;
    edited.manualOverrides.set(QStringLiteral("feedRate"), 73.0);
    const QList<AutoHoleCandidate> rerecognized =
        AutoHolePlanningService::RecognizeCompound({feature}, {plan}, {edited});
    if (!expect(rerecognized.size() == 1 && rerecognized.first().manuallyEdited &&
                    std::abs(rerecognized.first().manualOverrides.get(
                        QStringLiteral("feedRate")) - 73.0) < 1.0e-9,
                "stable compound geometry must retain manual overrides after re-recognition")) {
        return 1;
    }

    CompoundHoleFeature counterbore = feature;
    counterbore.geometryRef = QStringLiteral("feature:compound-counterbore-1");
    counterbore.layers = {
        {HoleLayerKind::Cylindrical, 10.0, 0.0, -2.0, false},
        {HoleLayerKind::Cylindrical, 6.0, -2.0, -14.0, false}};
    AutoHolePlan counterborePlan = plan;
    counterborePlan.id = QStringLiteral("counterbore-drill");
    counterborePlan.shape = AutoHoleShape::Counterbore;
    counterborePlan.layers.moduleRule.set(QStringLiteral("peckDepth"), 1.0);
    counterborePlan.steps = {
        {QStringLiteral("counterbore"), QStringLiteral("hole_peck"),
         QStringLiteral("T1"), QStringLiteral("0"), QStringLiteral("H1"),
         0, false},
        {QStringLiteral("core-drill"), QStringLiteral("hole_peck"),
         QStringLiteral("T1"), QStringLiteral("0"), QStringLiteral("H1+H2"),
         1, false}};
    AutoHoleCandidate counterboreCandidate =
        AutoHolePlanningService::RecognizeCompound({counterbore}, {counterborePlan}, {})
            .first();
    counterboreCandidate.selectedPlanId = counterborePlan.id;
    const AutoHoleConfirmationResult counterboreConfirmed =
        AutoHolePlanningService::ConfirmCompound(
            counterboreCandidate, tools(), context(), 0.2);
    if (!expect(counterboreConfirmed.ok && counterboreConfirmed.operations.size() == 2 &&
                    std::abs(counterboreConfirmed.operations.first().params.get(
                        QStringLiteral("depth")) - 2.0) < 1.0e-9,
                "coaxial counterbore layers must create ordered depth-bounded operations")) {
        return 1;
    }

    CompoundHoleFeature sideHole = feature;
    sideHole.geometryRef = QStringLiteral("feature:side-hole");
    sideHole.region = FaceRegion::Side;
    const QList<AutoHoleCandidate> rejected =
        AutoHolePlanningService::RecognizeCompound({sideHole}, {plan}, {});
    if (!expect(rejected.size() == 1 &&
                    rejected.first().state == AutoCandidateState::Rejected &&
                    !rejected.first().reasons.isEmpty(),
                "side-face compound holes must be retained as rejected candidates")) {
        return 1;
    }

    CompoundHoleFeature overlapping = feature;
    overlapping.geometryRef = QStringLiteral("feature:overlap-hole");
    overlapping.layers[1].startZ = -1.0;
    const QList<AutoHoleCandidate> invalid =
        AutoHolePlanningService::RecognizeCompound({overlapping}, {plan}, {});
    if (!expect(invalid.size() == 1 &&
                    invalid.first().state == AutoCandidateState::Rejected,
                "overlapping or out-of-order layers must never enter a draft")) {
        return 1;
    }

    AutoHoleCandidate formulaError = selected;
    formulaError.plans.first().steps.first().depthExpression = QStringLiteral("H3/0");
    const AutoHoleConfirmationResult failedFormula =
        AutoHolePlanningService::ConfirmCompound(
            formulaError, tools(), context(), 0.2);
    if (!expect(!failedFormula.ok && failedFormula.operations.isEmpty(),
                "invalid layer formulas must leave no partial formal operations")) {
        return 1;
    }

    QMap<int, AutoHoleToolInfo> missingTool = tools();
    missingTool.remove(8);
    const AutoHoleConfirmationResult failedTool =
        AutoHolePlanningService::ConfirmCompound(
            selected, missingTool, context(), 0.2);
    if (!expect(!failedTool.ok && failedTool.operations.isEmpty(),
                "a missing later-step tool must atomically reject the whole chain")) {
        return 1;
    }

    return 0;
}
