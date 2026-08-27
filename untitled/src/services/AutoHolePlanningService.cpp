#include "AutoHolePlanningService.h"

#include "../core/FeatureIdentity.h"
#include "../import/CompoundHoleRecognizer.h"
#include "ProcessTemplateService.h"
#include "RestrictedFormulaEvaluator.h"

#include <QMap>
#include <QSet>

#include <cmath>

namespace {

const AutoHolePlan *FindPlan(const AutoHoleCandidate &candidate, const QString &id)
{
    for (const AutoHolePlan &plan : candidate.plans) {
        if (plan.id == id) {
            return &plan;
        }
    }
    return nullptr;
}

void Merge(const StrategyParams &source, StrategyParams *destination)
{
    for (auto it = source.values.cbegin(); it != source.values.cend(); ++it) {
        destination->values.insert(it.key(), it.value());
    }
}

bool IsSupportedDrillingPlan(const QString &strategyId, const QString &toolType)
{
    if (strategyId == QStringLiteral("hole_spot")) {
        return toolType == QStringLiteral("spot_drill") ||
               toolType == QStringLiteral("drill");
    }
    return (strategyId == QStringLiteral("hole_peck") ||
            strategyId == QStringLiteral("hole_peck_g73") ||
            strategyId == QStringLiteral("hole_deephole")) &&
           toolType == QStringLiteral("drill");
}

AutoHoleShape ShapeFromName(const QString &name)
{
    if (name == QStringLiteral("single")) {
        return AutoHoleShape::Single;
    }
    if (name == QStringLiteral("counterbore")) {
        return AutoHoleShape::Counterbore;
    }
    if (name == QStringLiteral("countersink")) {
        return AutoHoleShape::Countersink;
    }
    return AutoHoleShape::Any;
}

AutoHoleShape ShapeForFeature(const CompoundHoleFeature &feature)
{
    for (const HoleLayer &layer : feature.layers) {
        if (layer.kind == HoleLayerKind::ConicalCountersink) {
            return AutoHoleShape::Countersink;
        }
    }
    return feature.layers.size() > 1 ? AutoHoleShape::Counterbore
                                     : AutoHoleShape::Single;
}

bool PlanMatchesFeature(const AutoHolePlan &plan, const CompoundHoleFeature &feature)
{
    return plan.shape == AutoHoleShape::Any ||
           plan.shape == ShapeForFeature(feature);
}

HoleFeature HoleForCompound(const CompoundHoleFeature &feature,
                            double diameter,
                            double depth)
{
    HoleFeature hole;
    hole.kind = FeatureKind::Hole;
    hole.subType = feature.isThrough() ? QStringLiteral("through_hole")
                                       : QStringLiteral("blind_hole");
    hole.radius = diameter * 0.5;
    hole.depth = depth;
    hole.center = QVector3D(feature.entryPoint.x(), feature.entryPoint.y(),
                            float(feature.entryPoint.z() - depth * 0.5));
    hole.axis = feature.axis;
    hole.region = feature.region;
    return hole;
}

QMap<QString, double> LayerVariables(const CompoundHoleFeature &feature,
                                     const AutoHolePlan &plan,
                                     double allowance)
{
    QMap<QString, double> variables = plan.formulaVariables;
    variables.insert(QStringLiteral("AT"), allowance);
    for (int index = 0; index < feature.layers.size(); ++index) {
        const HoleLayer &layer = feature.layers.at(index);
        variables.insert(QStringLiteral("H%1").arg(index + 1), layer.depth());
        variables.insert(QStringLiteral("D%1").arg(index + 1), layer.diameter);
    }
    return variables;
}

bool IsSupportedCompoundStep(const QString &strategy_id, const QString &tool_type)
{
    if (strategy_id == QStringLiteral("hole_chamfer")) {
        return tool_type == QStringLiteral("chamfer_mill");
    }
    return IsSupportedDrillingPlan(strategy_id, tool_type);
}

double LayeredValue(const OperationParameterLayers &layers,
                    const QString &key,
                    double default_value)
{
    double value = default_value;
    const QList<StrategyParams> ordered_layers{
        layers.systemDefaults, layers.materialTemplate, layers.toolTypeTemplate,
        layers.moduleRule, layers.manualOverrides};
    for (const StrategyParams &layer : ordered_layers) {
        if (layer.values.contains(key)) {
            value = layer.get(key);
        }
    }
    return value;
}

struct PreparedCompoundStep {
    AutoHolePlanStep step;
    AutoHoleToolInfo tool;
    HoleFeature hole;
    OperationParameterLayers layers;
};

QSet<QString> PlanIds(const QList<AutoHolePlan> &plans)
{
    QSet<QString> ids;
    for (const AutoHolePlan &plan : plans) {
        ids.insert(plan.id);
    }
    return ids;
}

} // namespace

QList<AutoHolePlan> AutoHolePlanningService::BuildPlans(
    const ProcessTemplateLibrary &library,
    QStringList *errors)
{
    QList<AutoHolePlan> result;
    for (const MachiningPlanDefinition &definition : library.plans()) {
        ProcessTemplateSelection selection;
        selection.templateRefs = definition.templateRefs;
        const ProcessTemplateResolution resolved =
            ProcessTemplateService::Resolve(library, selection);
        if (!resolved.ok) {
            if (errors) {
                for (const QString &error : resolved.errors) {
                    errors->append(QStringLiteral("%1: %2")
                                       .arg(definition.ref(), error));
                }
            }
            continue;
        }
        AutoHolePlan plan;
        plan.id = definition.ref();
        plan.version = definition.version;
        plan.strategyId = definition.strategyId;
        plan.toolId = definition.toolId;
        plan.startExpression = definition.startExpression;
        plan.depthExpression = definition.depthExpression;
        plan.layers = resolved.layers;
        plan.shape = ShapeFromName(definition.shape);
        plan.formulaVariables = definition.formulaVariables;
        for (const MachiningPlanStepDefinition &definitionStep : definition.steps) {
            AutoHolePlanStep step;
            step.id = definitionStep.id;
            step.strategyId = definitionStep.strategyId;
            step.toolExpression = definitionStep.toolExpression;
            step.startExpression = definitionStep.startExpression;
            step.depthExpression = definitionStep.depthExpression;
            step.layerIndex = definitionStep.layerIndex;
            step.countersinkStep = definitionStep.countersinkStep;
            plan.steps.append(step);
        }
        if (plan.steps.isEmpty()) {
            plan.steps.append({definition.id, definition.strategyId,
                               QString::number(definition.toolId),
                               definition.startExpression, definition.depthExpression,
                               0, false});
        }
        result.append(plan);
    }
    return result;
}

QList<AutoHoleCandidate> AutoHolePlanningService::RecognizeCompound(
    const QList<CompoundHoleFeature> &selectedFeatures,
    const QList<AutoHolePlan> &plans,
    const QList<AutoHoleCandidate> &previousCandidates)
{
    QMap<QString, AutoHoleCandidate> previousByGeometry;
    for (const AutoHoleCandidate &candidate : previousCandidates) {
        previousByGeometry.insert(candidate.geometryRef, candidate);
    }

    QList<AutoHoleCandidate> result;
    QSet<QString> currentGeometryRefs;
    for (const CompoundHoleFeature &feature : selectedFeatures) {
        currentGeometryRefs.insert(feature.geometryRef);
        AutoHoleCandidate candidate = previousByGeometry.value(feature.geometryRef);
        if (candidate.id.isEmpty()) {
            candidate.id = QStringLiteral("auto-compound-hole:") + feature.geometryRef;
            candidate.geometryRef = feature.geometryRef;
        }
        candidate.compoundHole = feature;
        candidate.holeFeature = feature.layers.isEmpty()
            ? HoleFeature() : HoleForCompound(feature,
                feature.layers.last().diameter, feature.totalDepth());
        candidate.plans = plans;
        const QStringList geometryErrors = feature.validateForFrontSetup();
        if (!geometryErrors.isEmpty()) {
            candidate.state = AutoCandidateState::Rejected;
            candidate.reasons = geometryErrors;
        } else {
            bool hasCompatiblePlan = false;
            for (const AutoHolePlan &plan : plans) {
                hasCompatiblePlan = hasCompatiblePlan || PlanMatchesFeature(plan, feature);
            }
            if (!hasCompatiblePlan) {
                candidate.state = AutoCandidateState::Rejected;
                candidate.reasons = QStringList{
                    QStringLiteral("No compatible compound-hole machining plan.")};
            } else if (previousByGeometry.contains(feature.geometryRef) &&
                       PlanIds(previousByGeometry.value(feature.geometryRef).plans) !=
                           PlanIds(plans)) {
                candidate.state = AutoCandidateState::Conflict;
                if (!candidate.reasons.contains(
                        QStringLiteral("Machining-plan versions changed; review is required."))) {
                    candidate.reasons.append(
                        QStringLiteral("Machining-plan versions changed; review is required."));
                }
            } else if (candidate.state != AutoCandidateState::Conflict) {
                candidate.state = AutoCandidateState::Draft;
                candidate.reasons.clear();
            }
        }
        result.append(candidate);
    }

    for (const AutoHoleCandidate &previous : previousCandidates) {
        if (!currentGeometryRefs.contains(previous.geometryRef)) {
            AutoHoleCandidate stale = previous;
            stale.state = AutoCandidateState::Stale;
            if (!stale.reasons.contains(QStringLiteral("Recognized geometry changed or disappeared."))) {
                stale.reasons.append(QStringLiteral("Recognized geometry changed or disappeared."));
            }
            result.append(stale);
        }
    }
    return result;
}

QList<AutoHoleCandidate> AutoHolePlanningService::Recognize(
    const QVector<MachiningFeature> &selectedFeatures,
    const QList<AutoHolePlan> &plans,
    const QList<AutoHoleCandidate> &previousCandidates)
{
    QList<CompoundHoleFeature> compoundFeatures;
    QList<AutoHoleCandidate> rejected;
    for (const MachiningFeature &feature : selectedFeatures) {
        if (feature.kind != FeatureKind::Hole) {
            continue;
        }
        const CompoundHoleRecognitionResult recognized =
            CompoundHoleRecognizer::Recognize(feature);
        if (recognized.ok) {
            compoundFeatures.append(recognized.feature);
        } else {
            AutoHoleCandidate candidate;
            candidate.id = QStringLiteral("auto-compound-hole:") + stableFeatureId(feature);
            candidate.geometryRef = stableFeatureId(feature);
            candidate.holeFeature = feature;
            candidate.plans = plans;
            candidate.state = AutoCandidateState::Rejected;
            candidate.reasons = recognized.reasons;
            rejected.append(candidate);
        }
    }
    QList<AutoHoleCandidate> result = RecognizeCompound(
        compoundFeatures, plans, previousCandidates);
    result.append(rejected);
    return result;
}

AutoHoleConfirmationResult AutoHolePlanningService::Confirm(
    const AutoHoleCandidate &candidate,
    const QMap<int, AutoHoleToolInfo> &availableTools,
    const ProcessContext &context,
    double allowance)
{
    AutoHoleConfirmationResult result;
    if (candidate.state != AutoCandidateState::Draft) {
        result.errors.append(QStringLiteral("Only current draft candidates can be confirmed."));
        return result;
    }
    if (candidate.selectedPlanId.trimmed().isEmpty()) {
        result.errors.append(QStringLiteral("Explicit machining-plan selection is required."));
        return result;
    }
    const AutoHolePlan *plan = FindPlan(candidate, candidate.selectedPlanId);
    if (!plan) {
        result.errors.append(QStringLiteral("The selected machining plan is unavailable."));
        return result;
    }
    const AutoHoleToolInfo tool = availableTools.value(plan->toolId);
    if (tool.id != plan->toolId || context.toolId != plan->toolId) {
        result.errors.append(QStringLiteral("The selected tool is unavailable or mismatched."));
        return result;
    }
    if (!IsSupportedDrillingPlan(plan->strategyId, tool.type) ||
        context.toolTypeId != tool.type || tool.diameter <= 0.0 ||
        tool.diameter > candidate.holeFeature.radius * 2.0 + 1.0e-6) {
        result.errors.append(
            QStringLiteral("The selected tool type or diameter is unsafe for this hole plan."));
        return result;
    }
    if (!std::isfinite(allowance) || allowance <= 0.0) {
        result.errors.append(QStringLiteral("AT must be a positive finite allowance."));
        return result;
    }

    const QMap<QString, double> variables{
        {QStringLiteral("H1"), candidate.holeFeature.depth},
        {QStringLiteral("H2"), candidate.holeFeature.secondaryRadius},
        {QStringLiteral("AT"), allowance}
    };
    const FormulaEvaluationResult start = RestrictedFormulaEvaluator::Evaluate(
        plan->startExpression, variables);
    const FormulaEvaluationResult depth = RestrictedFormulaEvaluator::Evaluate(
        plan->depthExpression, variables);
    if (!start.ok) {
        result.errors.append(QStringLiteral("Start formula: %1").arg(start.error));
    }
    if (!depth.ok) {
        result.errors.append(QStringLiteral("Depth formula: %1").arg(depth.error));
    }
    if (!result.errors.isEmpty()) {
        return result;
    }

    const double effectiveReference = candidate.manualOverrides.values.contains(
        QStringLiteral("referenceHeight"))
        ? candidate.manualOverrides.get(QStringLiteral("referenceHeight"))
        : start.value;
    const double effectiveDepth = candidate.manualOverrides.values.contains(
        QStringLiteral("depth"))
        ? candidate.manualOverrides.get(QStringLiteral("depth"))
        : depth.value;
    if (!std::isfinite(effectiveReference) || effectiveReference < 0.0 ||
        !std::isfinite(effectiveDepth) || effectiveDepth <= 0.0 ||
        effectiveReference + effectiveDepth > candidate.holeFeature.depth + 1.0e-6) {
        result.errors.append(
            QStringLiteral("Resolved start and depth exceed the recognized hole geometry."));
        return result;
    }

    OperationProposal proposal;
    proposal.kind = OperationProposalKind::Hole;
    proposal.strategyId = plan->strategyId;
    proposal.toolId = plan->toolId;
    proposal.holeFeature = candidate.holeFeature;

    OperationParameterLayers layers = plan->layers;
    layers.moduleRule.set(QStringLiteral("referenceHeight"), start.value);
    layers.moduleRule.set(QStringLiteral("depth"), depth.value);
    layers.moduleRule.set(QStringLiteral("enforceConfirmedDepth"), 1.0);
    Merge(candidate.manualOverrides, &layers.manualOverrides);
    const OperationFactoryResult created =
        OperationFactory::CreateConfirmed(proposal, layers, context);
    if (!created.ok) {
        result.errors = created.errors;
        return result;
    }
    result.ok = true;
    result.operation = created.operation;
    result.operations = {created.operation};
    return result;
}

AutoHoleConfirmationResult AutoHolePlanningService::ConfirmCompound(
    const AutoHoleCandidate &candidate,
    const QMap<int, AutoHoleToolInfo> &availableTools,
    const ProcessContext &context,
    double allowance)
{
    AutoHoleConfirmationResult result;
    if (candidate.state != AutoCandidateState::Draft) {
        result.errors.append(QStringLiteral("Only current draft candidates can be confirmed."));
        return result;
    }
    if (candidate.selectedPlanId.trimmed().isEmpty()) {
        result.errors.append(QStringLiteral("Explicit machining-plan selection is required."));
        return result;
    }
    const AutoHolePlan *plan = FindPlan(candidate, candidate.selectedPlanId);
    if (!plan || !PlanMatchesFeature(*plan, candidate.compoundHole)) {
        result.errors.append(QStringLiteral("The selected plan is not compatible with this compound hole."));
        return result;
    }
    if (!std::isfinite(allowance) || allowance <= 0.0) {
        result.errors.append(QStringLiteral("AT must be a positive finite allowance."));
        return result;
    }
    const QStringList geometryErrors = candidate.compoundHole.validateForFrontSetup();
    if (!geometryErrors.isEmpty()) {
        result.errors = geometryErrors;
        return result;
    }
    if (plan->steps.isEmpty()) {
        result.errors.append(QStringLiteral("The compound-hole plan has no ordered steps."));
        return result;
    }

    const QMap<QString, double> variables = LayerVariables(
        candidate.compoundHole, *plan, allowance);
    QList<PreparedCompoundStep> prepared;
    for (const AutoHolePlanStep &step : plan->steps) {
        if (candidate.cancelCountersink && step.countersinkStep) {
            continue;
        }
        if (step.id.trimmed().isEmpty() || step.strategyId.trimmed().isEmpty() ||
            step.toolExpression.trimmed().isEmpty() ||
            step.startExpression.trimmed().isEmpty() ||
            step.depthExpression.trimmed().isEmpty() ||
            step.layerIndex < 0 || step.layerIndex >= candidate.compoundHole.layers.size()) {
            result.errors.append(QStringLiteral("Compound-hole plan contains an invalid ordered step."));
            return result;
        }
        const FormulaEvaluationResult toolValue = RestrictedFormulaEvaluator::Evaluate(
            step.toolExpression, variables);
        const FormulaEvaluationResult startValue = RestrictedFormulaEvaluator::Evaluate(
            step.startExpression, variables);
        const FormulaEvaluationResult depthValue = RestrictedFormulaEvaluator::Evaluate(
            step.depthExpression, variables);
        if (!toolValue.ok || !startValue.ok || !depthValue.ok) {
            result.errors.append(QStringLiteral("Compound-hole step %1 formula is invalid.").arg(step.id));
            return result;
        }
        const int toolId = int(std::round(toolValue.value));
        if (std::abs(toolValue.value - toolId) > 1.0e-6 || toolId <= 0 ||
            !availableTools.contains(toolId)) {
            result.errors.append(QStringLiteral("Compound-hole step %1 references an unavailable tool.")
                                 .arg(step.id));
            return result;
        }
        const HoleLayer &layer = candidate.compoundHole.layers.at(step.layerIndex);
        const double totalDepth = candidate.compoundHole.totalDepth();
        const double maximumDepth = totalDepth +
            (candidate.compoundHole.isThrough() ? allowance : 0.0);
        if (startValue.value < -1.0e-6 || depthValue.value <= 0.0 ||
            startValue.value > 1.0e-6 ||
            startValue.value + depthValue.value > maximumDepth + 1.0e-6) {
            result.errors.append(QStringLiteral("Compound-hole step %1 exceeds verified layer bounds.")
                                 .arg(step.id));
            return result;
        }
        if (step.countersinkStep &&
            (layer.kind != HoleLayerKind::ConicalCountersink ||
             std::abs(depthValue.value - layer.depth()) > 1.0e-4)) {
            result.errors.append(QStringLiteral("Countersink step must match one verified conical layer."));
            return result;
        }
        const AutoHoleToolInfo tool = availableTools.value(toolId);
        if (!IsSupportedCompoundStep(step.strategyId, tool.type) ||
            tool.diameter <= 0.0 || tool.fluteLength <= 0.0 || tool.totalLength <= 0.0 ||
            tool.fluteLength + 1.0e-6 < startValue.value + depthValue.value ||
            tool.totalLength + 1.0e-6 < startValue.value + depthValue.value) {
            result.errors.append(QStringLiteral("Compound-hole step %1 has an unsafe tool or reach.")
                                 .arg(step.id));
            return result;
        }
        const double coreDiameter = candidate.compoundHole.layers.last().diameter;
        if (step.strategyId == QStringLiteral("hole_chamfer")) {
            if (tool.diameter > layer.diameter + 1.0e-6 ||
                layer.diameter <= coreDiameter + 1.0e-6) {
                result.errors.append(QStringLiteral("Countersink tool cannot fit the verified conical layer."));
                return result;
            }
        } else if (tool.diameter > layer.diameter + 1.0e-6) {
            result.errors.append(QStringLiteral("Drilling tool exceeds the verified target diameter."));
            return result;
        }

        PreparedCompoundStep item;
        item.step = step;
        item.tool = tool;
        const double geometryDepth = candidate.compoundHole.isThrough() &&
                !step.countersinkStep ? totalDepth : depthValue.value;
        item.hole = HoleForCompound(
            candidate.compoundHole, coreDiameter, geometryDepth);
        item.layers = plan->layers;
        item.layers.moduleRule.set(QStringLiteral("referenceHeight"), startValue.value);
        item.layers.moduleRule.set(QStringLiteral("depth"), depthValue.value);
        item.layers.moduleRule.set(QStringLiteral("enforceConfirmedDepth"), 1.0);
        item.layers.moduleRule.set(QStringLiteral("feedHeight"), LayeredValue(
            plan->layers, QStringLiteral("plungeHeight"), 3.0));
        item.layers.moduleRule.set(QStringLiteral("throughAllowance"),
            candidate.compoundHole.isThrough() ? allowance : 0.0);
        if (step.countersinkStep) {
            const double width = (layer.diameter - coreDiameter) * 0.5;
            item.layers.moduleRule.set(QStringLiteral("chamferWidth"), width);
            item.layers.moduleRule.set(QStringLiteral("chamferAngle"),
                std::atan(width / layer.depth()) * 180.0 / std::acos(-1.0));
        }
        prepared.append(item);
    }
    if (prepared.isEmpty()) {
        result.errors.append(QStringLiteral("Countersink cancellation removed every compound-hole step."));
        return result;
    }

    QList<MachiningOperation> operations;
    for (const PreparedCompoundStep &step : prepared) {
        const double safeHeight = LayeredValue(step.layers, QStringLiteral("safeHeight"), 50.0);
        const double referenceHeight = LayeredValue(
            step.layers, QStringLiteral("referenceHeight"), 0.0);
        const double feedHeight = LayeredValue(step.layers, QStringLiteral("feedHeight"), 3.0);
        const double depth = LayeredValue(step.layers, QStringLiteral("depth"), 0.0);
        const double peckDepth = LayeredValue(step.layers, QStringLiteral("peckDepth"), depth);
        if (safeHeight < feedHeight || feedHeight < referenceHeight ||
            (step.step.strategyId != QStringLiteral("hole_chamfer") &&
             (peckDepth <= 0.0 || peckDepth > depth + 1.0e-6))) {
            result.errors.append(QStringLiteral("Compound-hole step %1 has unsafe R plane or Q depth.")
                                 .arg(step.step.id));
            return result;
        }
        ProcessContext stepContext = context;
        stepContext.toolId = step.tool.id;
        stepContext.toolTypeId = step.tool.type;
        OperationProposal proposal;
        proposal.kind = OperationProposalKind::Hole;
        proposal.strategyId = step.step.strategyId;
        proposal.toolId = step.tool.id;
        proposal.holeFeature = step.hole;
        const OperationFactoryResult created = OperationFactory::CreateConfirmed(
            proposal, step.layers, stepContext);
        if (!created.ok) {
            result.errors = created.errors;
            return result;
        }
        MachiningOperation operation = created.operation;
        operation.geometryRefs = QStringList{candidate.geometryRef};
        operation.featureRef = candidate.geometryRef + QLatin1Char(':') + step.step.id;
        if (!operations.isEmpty()) {
            operation.dependencyOperationIds = QStringList{operations.last().id};
        }
        operations.append(operation);
    }
    result.ok = true;
    result.operations = operations;
    result.operation = operations.first();
    return result;
}
