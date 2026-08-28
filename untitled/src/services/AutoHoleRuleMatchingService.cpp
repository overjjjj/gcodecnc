#include "AutoHoleRuleMatchingService.h"

#include "RestrictedFormulaEvaluator.h"

#include <QDate>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {

HoleRuleMatchResult Rejected(const QString &code, const QString &field_path,
                             const QString &message)
{
    HoleRuleMatchResult result;
    result.reasons.append({code, field_path, message});
    return result;
}

bool HasCompleteReference(const VersionedCatalogReference &reference)
{
    return !reference.id.trimmed().isEmpty()
        && !reference.catalogId.trimmed().isEmpty()
        && !reference.catalogVersion.trimmed().isEmpty();
}

bool MatchesRange(double value, const HoleRuleLayerCondition &condition)
{
    if (value < condition.lowerMm
        || (value == condition.lowerMm && !condition.lowerClosed)) {
        return false;
    }
    if (value > condition.upperMm
        || (value == condition.upperMm && !condition.upperClosed)) {
        return false;
    }
    return true;
}

bool UsesUnconfirmedSemanticVariable(const QString &expression)
{
    static const QRegularExpression kHnOrAt(
        QStringLiteral("\\b(?:H[1-9][0-9]*|AT)\\b"));
    return kHnOrAt.match(expression).hasMatch();
}

QString LayerSignature(const QList<HoleRuleLayerInput> &layers)
{
    QStringList parts;
    for (const HoleRuleLayerInput &layer : layers) {
        parts.append(QStringLiteral("%1:%2:%3:%4:%5:%6")
                         .arg(layer.ordinal)
                         .arg(layer.kind)
                         .arg(layer.diameterMm, 0, 'g', 16)
                         .arg(layer.startPlaneMm, 0, 'g', 16)
                         .arg(layer.endPlaneMm, 0, 'g', 16)
                         .arg(layer.through ? QStringLiteral("through")
                                            : QStringLiteral("blind")));
    }
    return parts.join(QLatin1Char('|'));
}

bool ValidateFeature(const HoleRuleMatchRequest &request,
                     HoleRuleMatchResult *result)
{
    const HoleRuleFeatureInput &feature = request.feature;
    if (feature.geometryRef.trimmed().isEmpty()) {
        *result = Rejected(QStringLiteral("GEOMETRY_REF_MISSING"),
                           QStringLiteral("feature.geometryRef"),
                           QStringLiteral("A geometry reference is required."));
        return false;
    }
    if (!feature.setupConfirmed) {
        *result = Rejected(QStringLiteral("SETUP_UNCONFIRMED"),
                           QStringLiteral("feature.setupRef"),
                           QStringLiteral("The setup must be confirmed."));
        return false;
    }
    if (feature.region != HoleRuleFeatureRegion::Front
        || !feature.axisMatchesPositiveZ) {
        *result = Rejected(QStringLiteral("GEOMETRY_UNSUPPORTED"),
                           QStringLiteral("feature.axis"),
                           QStringLiteral("Only front positive-Z holes are supported."));
        return false;
    }
    if (feature.layers.isEmpty()) {
        *result = Rejected(QStringLiteral("LAYER_SEQUENCE_INVALID"),
                           QStringLiteral("feature.layers"),
                           QStringLiteral("At least one ordered layer is required."));
        return false;
    }
    for (int index = 0; index < feature.layers.size(); ++index) {
        const HoleRuleLayerInput &layer = feature.layers.at(index);
        const QString path = QStringLiteral("feature.layers[%1]").arg(index);
        if (layer.ordinal != index + 1 || layer.kind.trimmed().isEmpty()
            || !std::isfinite(layer.diameterMm) || layer.diameterMm <= 0.0
            || !std::isfinite(layer.startPlaneMm)
            || !std::isfinite(layer.endPlaneMm)
            || layer.startPlaneMm <= layer.endPlaneMm
            || (layer.through && index != feature.layers.size() - 1)) {
            *result = Rejected(QStringLiteral("LAYER_SEQUENCE_INVALID"), path,
                               QStringLiteral("Layer geometry or order is invalid."));
            return false;
        }
    }
    return true;
}

bool ValidateRequest(const AutomationTemplateDocument &document,
                     const HoleRuleMatchRequest &request,
                     HoleRuleMatchResult *result)
{
    if (request.requestId.trimmed().isEmpty()) {
        *result = Rejected(QStringLiteral("CATALOG_VERSION_MISSING"),
                           QStringLiteral("requestId"),
                           QStringLiteral("A request ID is required."));
        return false;
    }
    if (!QDate::fromString(request.asOfDate, Qt::ISODate).isValid()) {
        *result = Rejected(QStringLiteral("CATALOG_VERSION_MISSING"),
                           QStringLiteral("asOfDate"),
                           QStringLiteral("An ISO as-of date is required."));
        return false;
    }
    if (!HasCompleteReference(request.materialRef)
        || !HasCompleteReference(request.machineProfileRef)
        || !HasCompleteReference(request.toolCatalogRef)
        || !HasCompleteReference(request.ruleCatalogRef)) {
        *result = Rejected(QStringLiteral("CATALOG_VERSION_MISSING"),
                           QStringLiteral("catalogReference"),
                           QStringLiteral("All catalog references must be versioned."));
        return false;
    }
    if (request.ruleCatalogRef.catalogId != document.documentId
        || request.ruleCatalogRef.catalogVersion != document.documentVersion) {
        *result = Rejected(QStringLiteral("CATALOG_VERSION_MISSING"),
                           QStringLiteral("ruleCatalogRef"),
                           QStringLiteral("The referenced rule catalog is unavailable."));
        return false;
    }
    return ValidateFeature(request, result);
}

bool ValidatePlan(const AutomationTemplateDocument &document,
                  const FeatureMatchingRule &rule,
                  const HoleRuleMatchRequest &request,
                  QList<AutomationMachiningPlanStep> *steps,
                  HoleRuleMatchReason *reason)
{
    const HoleRuleDefinition &definition = rule.holeRule;
    if (definition.planStepIds.isEmpty() || definition.planStepIds.size() > 11) {
        *reason = {QStringLiteral("RULE_STEP_INVALID"),
                   QStringLiteral("holeRule.planStepIds"),
                   QStringLiteral("A plan must contain 1..11 steps.")};
        return false;
    }
    QMap<QString, const AutomationMachiningPlanStep *> steps_by_id;
    for (const AutomationMachiningPlanStep &step : document.machiningPlanSteps) {
        if (steps_by_id.contains(step.id)) {
            *reason = {QStringLiteral("RULE_STEP_INVALID"),
                       QStringLiteral("machiningPlanSteps"),
                       QStringLiteral("Plan step IDs must be unique.")};
            return false;
        }
        steps_by_id.insert(step.id, &step);
    }
    QSet<int> source_slots;
    for (int index = 0; index < definition.planStepIds.size(); ++index) {
        const QString path = QStringLiteral("holeRule.planStepIds[%1]").arg(index);
        const AutomationMachiningPlanStep *step =
            steps_by_id.value(definition.planStepIds.at(index));
        if (step == nullptr || !step->hasHolePlanStepDefinition || !step->enabled
            || step->sourceSlot < 1 || step->sourceSlot > 11
            || step->sequence != index + 1 || step->layerOrdinal < 1
            || step->layerOrdinal > request.feature.layers.size()
            || source_slots.contains(step->sourceSlot)) {
            *reason = {QStringLiteral("RULE_STEP_INVALID"), path,
                       QStringLiteral("The normalized plan step is invalid.")};
            return false;
        }
        source_slots.insert(step->sourceSlot);
        if (UsesUnconfirmedSemanticVariable(step->startExpression)
            || UsesUnconfirmedSemanticVariable(step->depthExpression)) {
            *reason = {QStringLiteral("SEMANTIC_VARIABLE_UNCONFIRMED"), path,
                       QStringLiteral("Hn and AT semantics are unconfirmed.")};
            return false;
        }
        const FormulaEvaluationResult start = RestrictedFormulaEvaluator::Evaluate(
            step->startExpression, {});
        const FormulaEvaluationResult depth = RestrictedFormulaEvaluator::Evaluate(
            step->depthExpression, {});
        if (!start.ok || !depth.ok || !std::isfinite(depth.value)
            || depth.value <= 0.0) {
            *reason = {QStringLiteral("FORMULA_REJECTED"), path,
                       QStringLiteral("The plan step formula is invalid.")};
            return false;
        }
        const HoleRuleLayerInput &layer = request.feature.layers.at(
            step->layerOrdinal - 1);
        const double layer_depth = layer.startPlaneMm - layer.endPlaneMm;
        if (depth.value > layer_depth) {
            *reason = {QStringLiteral("DEPTH_OUT_OF_BOUNDS"), path,
                       QStringLiteral("The plan depth exceeds its target layer.")};
            return false;
        }
        steps->append(*step);
    }
    return true;
}

} // namespace

HoleRuleMatchResult AutoHoleRuleMatchingService::Match(
    const AutomationTemplateDocument &document,
    const HoleRuleMatchRequest &request)
{
    HoleRuleMatchResult result;
    if (!ValidateRequest(document, request, &result)) {
        return result;
    }

    QList<const FeatureMatchingRule *> matches;
    HoleRuleMatchReason blocked_reason;
    for (int rule_index = 0;
         rule_index < document.featureMatchingRules.size(); ++rule_index) {
        const FeatureMatchingRule &rule =
            document.featureMatchingRules.at(rule_index);
        if (!rule.hasHoleRuleDefinition || rule.featureKind != QStringLiteral("hole")) {
            continue;
        }
        result.explanation.consideredRuleIds.append(rule.id);
        const HoleRuleDefinition &definition = rule.holeRule;
        const bool effective = request.asOfDate >= definition.effectiveFrom
            && (definition.effectiveTo.isEmpty()
                || request.asOfDate <= definition.effectiveTo);
        if (!definition.enabled) {
            result.explanation.filteredRuleIds.append(rule.id);
            result.explanation.filterReasons.append(
                {QStringLiteral("RULE_DISABLED"),
                 QStringLiteral("featureMatchingRules[%1].holeRule.enabled")
                     .arg(rule_index),
                 QStringLiteral("The rule is disabled.")});
            continue;
        }
        if (!effective) {
            result.explanation.filteredRuleIds.append(rule.id);
            result.explanation.filterReasons.append(
                {QStringLiteral("RULE_NOT_EFFECTIVE"),
                 QStringLiteral("featureMatchingRules[%1].holeRule")
                     .arg(rule_index),
                 QStringLiteral("The rule is outside its effective date range.")});
            continue;
        }
        const bool eligible = definition.materials.contains(request.materialRef.id)
            && definition.machineProfiles.contains(request.machineProfileRef.id)
            && rule.category == request.feature.category
            && definition.layerConditions.size() == request.feature.layers.size();
        if (!eligible) {
            result.explanation.filteredRuleIds.append(rule.id);
            result.explanation.filterReasons.append(
                {QStringLiteral("RULE_CONDITION_NOT_MET"),
                 QStringLiteral("featureMatchingRules[%1].holeRule")
                     .arg(rule_index),
                 QStringLiteral("The rule conditions do not match the request.")});
            continue;
        }
        bool layers_match = true;
        for (int index = 0; index < request.feature.layers.size(); ++index) {
            const HoleRuleLayerInput &layer = request.feature.layers.at(index);
            const HoleRuleLayerCondition &condition =
                definition.layerConditions.at(index);
            if (condition.ordinal != layer.ordinal
                || !condition.acceptedKinds.contains(layer.kind)
                || !MatchesRange(layer.diameterMm, condition)) {
                layers_match = false;
                break;
            }
        }
        if (!layers_match) {
            result.explanation.filteredRuleIds.append(rule.id);
            result.explanation.filterReasons.append(
                {QStringLiteral("RULE_CONDITION_NOT_MET"),
                 QStringLiteral("featureMatchingRules[%1].holeRule.layerConditions")
                     .arg(rule_index),
                 QStringLiteral("The rule layer conditions do not match.")});
            continue;
        }
        QList<AutomationMachiningPlanStep> steps;
        HoleRuleMatchReason plan_reason;
        if (!ValidatePlan(document, rule, request, &steps, &plan_reason)) {
            if (blocked_reason.code.isEmpty()) {
                blocked_reason = plan_reason;
            }
            result.explanation.filteredRuleIds.append(rule.id);
            result.explanation.filterReasons.append(plan_reason);
            continue;
        }
        matches.append(&rule);
    }

    if (matches.isEmpty()) {
        if (!blocked_reason.code.isEmpty()) {
            result.reasons.append(blocked_reason);
            return result;
        }
        result.reasons.append(
            {QStringLiteral("RULE_NOT_FOUND"),
             QStringLiteral("featureMatchingRules"),
             QStringLiteral("No eligible hole rule matched the feature.")});
        return result;
    }
    int highest_priority = matches.first()->holeRule.priority;
    for (const FeatureMatchingRule *rule : matches) {
        highest_priority = std::max(highest_priority, rule->holeRule.priority);
    }
    QList<const FeatureMatchingRule *> top_matches;
    for (const FeatureMatchingRule *rule : matches) {
        if (rule->holeRule.priority == highest_priority) {
            top_matches.append(rule);
            result.explanation.topRuleIds.append(rule->id);
        }
    }
    if (top_matches.size() != 1) {
        result.state = HoleRuleMatchState::Conflict;
        result.reasons.append({QStringLiteral("RULE_AMBIGUOUS"),
                               QStringLiteral("featureMatchingRules"),
                               QStringLiteral("Multiple highest-priority rules match.")});
        return result;
    }

    const FeatureMatchingRule &rule = *top_matches.first();
    QList<AutomationMachiningPlanStep> steps;
    HoleRuleMatchReason ignored_reason;
    ValidatePlan(document, rule, request, &steps, &ignored_reason);
    result.state = HoleRuleMatchState::Draft;
    result.candidate.geometryRef = request.feature.geometryRef;
    result.candidate.geometryRevision = request.feature.geometryRevision;
    result.candidate.layerSignature = LayerSignature(request.feature.layers);
    result.candidate.ruleId = rule.id;
    result.candidate.ruleVersion = rule.holeRule.ruleVersion;
    result.candidate.planId = rule.holeRule.planId;
    result.candidate.planVersion = rule.holeRule.planVersion;
    result.candidate.materialRef = request.materialRef;
    result.candidate.machineProfileRef = request.machineProfileRef;
    result.candidate.toolCatalogRef = request.toolCatalogRef;
    result.candidate.ruleCatalogRef = request.ruleCatalogRef;
    result.candidate.steps = steps;
    for (const HoleRuleLayerInput &layer : request.feature.layers) {
        result.explanation.layerMatches.append(
            QStringLiteral("layer %1 matched").arg(layer.ordinal));
    }
    return result;
}
