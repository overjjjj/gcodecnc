#include "../src/services/AutoHolePlanningService.h"

#include <QCoreApplication>

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

HoleFeature sampleHole(double depth = 12.0)
{
    HoleFeature hole;
    hole.kind = FeatureKind::Hole;
    hole.subType = QStringLiteral("blind_hole");
    hole.radius = 3.0;
    hole.depth = depth;
    hole.center = QVector3D(10.0f, 20.0f, 0.0f);
    hole.axis = QVector3D(0.0f, 0.0f, 1.0f);
    hole.region = FaceRegion::Front;
    return hole;
}

ProcessTemplateLibrary sampleLibrary()
{
    ProcessTemplateLibrary library;
    ProcessTemplateDefinition parameters;
    parameters.id = QStringLiteral("auto-hole");
    parameters.version = QStringLiteral("1");
    parameters.name = QStringLiteral("Auto-hole safe defaults");
    parameters.source = ProcessParameterSource::ModuleRule;
    parameters.values.set(QStringLiteral("safeHeight"), 20.0);
    parameters.values.set(QStringLiteral("plungeHeight"), 2.0);
    QString error;
    library.add(parameters, &error);

    MachiningPlanDefinition plan;
    plan.id = QStringLiteral("blind-drill");
    plan.version = QStringLiteral("1");
    plan.name = QStringLiteral("Blind drill");
    plan.strategyId = QStringLiteral("hole_peck");
    plan.toolId = 7;
    plan.startExpression = QStringLiteral("AT");
    plan.depthExpression = QStringLiteral("H1-AT");
    plan.templateRefs = QStringList{QStringLiteral("auto-hole@1")};
    library.addPlan(plan, &error);
    return library;
}

ProcessContext validContext()
{
    ProcessContext context;
    context.toolId = 7;
    context.materialId = QStringLiteral("aluminum");
    context.toolTypeId = QStringLiteral("drill");
    context.setup.workOffset = QStringLiteral("G54");
    context.setup.origin.confirmed = true;
    return context;
}

QMap<int, AutoHoleToolInfo> validTools()
{
    AutoHoleToolInfo tool;
    tool.id = 7;
    tool.type = QStringLiteral("drill");
    tool.diameter = 6.0;
    return {{tool.id, tool}};
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QStringList planErrors;
    const QList<AutoHolePlan> libraryPlans =
        AutoHolePlanningService::BuildPlans(sampleLibrary(), &planErrors);
    if (!expect(planErrors.isEmpty() && libraryPlans.size() == 1 &&
                    libraryPlans.first().layers.moduleRule.get(
                        QStringLiteral("safeHeight")) == 20.0,
                "persisted definitions should resolve into auditable runtime plans")) {
        return 1;
    }
    const AutoHolePlan plan = libraryPlans.first();
    const QList<AutoHoleCandidate> initial = AutoHolePlanningService::Recognize(
        QVector<MachiningFeature>{sampleHole()}, QList<AutoHolePlan>{plan}, {});
    if (!expect(initial.size() == 1 &&
                    initial.first().state == AutoCandidateState::Draft &&
                    initial.first().compoundHole.layers.size() == 1 &&
                    initial.first().geometryRef.startsWith(QStringLiteral("feature:")),
                "recognition should create a stable geometry-linked compound-hole draft")) {
        return 1;
    }

    const AutoHoleConfirmationResult unconfirmed = AutoHolePlanningService::Confirm(
        initial.first(), validTools(), validContext(), 0.2);
    if (!expect(!unconfirmed.ok,
                "a draft without an explicitly selected plan must not create an operation")) {
        return 1;
    }

    AutoHoleCandidate edited = initial.first();
    edited.selectedPlanId = plan.id;
    edited.manualOverrides.set(QStringLiteral("feedRate"), 275.0);
    edited.manuallyEdited = true;
    const QList<AutoHoleCandidate> repeated = AutoHolePlanningService::Recognize(
        QVector<MachiningFeature>{sampleHole()}, QList<AutoHolePlan>{plan}, {edited});
    if (!expect(repeated.size() == 1 && repeated.first().manuallyEdited &&
                    repeated.first().selectedPlanId == plan.id &&
                    repeated.first().manualOverrides.get(QStringLiteral("feedRate")) == 275.0,
                "repeat recognition must retain manual plan and parameter edits")) {
        return 1;
    }

    AutoHolePlan revisedPlan = plan;
    revisedPlan.id = QStringLiteral("blind-drill@2");
    revisedPlan.version = QStringLiteral("2");
    const QList<AutoHoleCandidate> conflicted = AutoHolePlanningService::Recognize(
        QVector<MachiningFeature>{sampleHole()}, {revisedPlan}, {edited});
    if (!expect(conflicted.size() == 1 &&
                    conflicted.first().state == AutoCandidateState::Conflict &&
                    conflicted.first().selectedPlanId == plan.id &&
                    conflicted.first().manualOverrides.get(
                        QStringLiteral("feedRate")) == 275.0,
                "plan-version changes must retain edits and mark an explicit conflict")) {
        return 1;
    }

    const QList<AutoHoleCandidate> changed = AutoHolePlanningService::Recognize(
        QVector<MachiningFeature>{sampleHole(10.0)}, QList<AutoHolePlan>{plan}, {edited});
    bool stalePreviousFound = false;
    for (const AutoHoleCandidate &candidate : changed) {
        if (candidate.geometryRef == edited.geometryRef &&
            candidate.state == AutoCandidateState::Stale) {
            stalePreviousFound = true;
        }
    }
    if (!expect(stalePreviousFound,
                "changed geometry must retain the edited candidate as explicitly stale")) {
        return 1;
    }

    const AutoHoleConfirmationResult missingTool = AutoHolePlanningService::Confirm(
        edited, {}, validContext(), 0.2);
    if (!expect(!missingTool.ok, "unknown tools must block confirmation")) {
        return 1;
    }

    QMap<int, AutoHoleToolInfo> oversizedTools = validTools();
    oversizedTools[7].diameter = 8.0;
    if (!expect(!AutoHolePlanningService::Confirm(
                    edited, oversizedTools, validContext(), 0.2).ok,
                "tools larger than the recognized hole must block confirmation")) {
        return 1;
    }
    QMap<int, AutoHoleToolInfo> wrongTypeTools = validTools();
    wrongTypeTools[7].type = QStringLiteral("end_mill");
    if (!expect(!AutoHolePlanningService::Confirm(
                    edited, wrongTypeTools, validContext(), 0.2).ok,
                "non-drilling tools must block drilling plans")) {
        return 1;
    }

    AutoHoleCandidate invalidDepth = edited;
    invalidDepth.plans.first().depthExpression = QStringLiteral("H1+1");
    if (!expect(!AutoHolePlanningService::Confirm(
                    invalidDepth, validTools(), validContext(), 0.2).ok,
                "formula depth beyond recognized geometry must block confirmation")) {
        return 1;
    }

    const AutoHoleConfirmationResult confirmed = AutoHolePlanningService::Confirm(
        edited, validTools(), validContext(), 0.2);
    if (!expect(confirmed.ok &&
                    confirmed.operation.geometryRefs == QStringList{edited.geometryRef} &&
                    confirmed.operation.params.get(QStringLiteral("depth")) == 11.8 &&
                    confirmed.operation.params.get(
                        QStringLiteral("enforceConfirmedDepth")) == 1.0 &&
                    confirmed.operation.params.get(QStringLiteral("feedRate")) == 275.0,
                "explicit valid selection should create one formal operation through OperationFactory")) {
        return 1;
    }

    AutoHoleCandidate stale = edited;
    stale.state = AutoCandidateState::Stale;
    if (!expect(!AutoHolePlanningService::Confirm(
                    stale, validTools(), validContext(), 0.2).ok,
                "stale candidates must never create formal operations")) {
        return 1;
    }

    return 0;
}
