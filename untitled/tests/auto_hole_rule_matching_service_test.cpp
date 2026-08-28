#include "../src/services/AutoHoleRuleMatchingService.h"

#include <QCoreApplication>

#include <iostream>

namespace {

bool Expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

bool HasReason(const HoleRuleMatchResult &result, const QString &code,
               const QString &field_path)
{
    for (const HoleRuleMatchReason &reason : result.reasons) {
        if (reason.code == code && reason.fieldPath == field_path) {
            return true;
        }
    }
    return false;
}

VersionedCatalogReference MakeReference(const QString &id,
                                        const QString &catalog_id,
                                        const QString &catalog_version)
{
    VersionedCatalogReference reference;
    reference.id = id;
    reference.catalogId = catalog_id;
    reference.catalogVersion = catalog_version;
    return reference;
}

AutomationMachiningPlanStep MakeStep(const QString &id, int source_slot,
                                     int sequence, int layer_ordinal,
                                     const QString &start_expression,
                                     const QString &depth_expression)
{
    AutomationMachiningPlanStep step;
    step.id = id;
    step.strategy = QStringLiteral("hole_peck");
    step.toolSelector = QStringLiteral("type:drill");
    step.startExpression = start_expression;
    step.depthExpression = depth_expression;
    step.coolant = QStringLiteral("external");
    step.enabled = true;
    step.hasHolePlanStepDefinition = true;
    step.sourceSlot = source_slot;
    step.sequence = sequence;
    step.layerOrdinal = layer_ordinal;
    step.coolantCompat = QStringLiteral("M8");
    return step;
}

FeatureMatchingRule MakeRule(const QString &id, int priority, double lower,
                             bool lower_closed, double upper, bool upper_closed,
                             const QStringList &step_ids)
{
    FeatureMatchingRule rule;
    rule.id = id;
    rule.featureKind = QStringLiteral("hole");
    rule.category = QStringLiteral("single");
    rule.minimumSizeMm = lower;
    rule.maximumSizeMm = upper;
    rule.closed = false;
    rule.through = false;
    rule.hasHoleRuleDefinition = true;
    rule.holeRule.ruleVersion = QStringLiteral("1");
    rule.holeRule.effectiveFrom = QStringLiteral("2026-08-01");
    rule.holeRule.effectiveTo = QStringLiteral("2026-08-31");
    rule.holeRule.priority = priority;
    rule.holeRule.enabled = true;
    rule.holeRule.materials = QStringList{QStringLiteral("aluminum-6061")};
    rule.holeRule.machineProfiles = QStringList{QStringLiteral("front-z-3axis")};
    HoleRuleLayerCondition condition;
    condition.ordinal = 1;
    condition.acceptedKinds = QStringList{QStringLiteral("cylindrical")};
    condition.lowerMm = lower;
    condition.upperMm = upper;
    condition.lowerClosed = lower_closed;
    condition.upperClosed = upper_closed;
    rule.holeRule.layerConditions = {condition};
    rule.holeRule.planId = id + QStringLiteral("-plan");
    rule.holeRule.planVersion = QStringLiteral("1");
    rule.holeRule.planStepIds = step_ids;
    rule.holeRule.sourceRef = QStringLiteral("cleanroom:test");
    return rule;
}

AutomationTemplateDocument MakeDocument()
{
    AutomationTemplateDocument document;
    document.documentId = QStringLiteral("hole-rule-catalog");
    document.documentVersion = QStringLiteral("2026.08.1");
    document.machiningPlanSteps = {
        MakeStep(QStringLiteral("drill-core"), 1, 1, 1,
                 QStringLiteral("0"), QStringLiteral("8"))
    };
    return document;
}

HoleRuleMatchRequest MakeRequest()
{
    HoleRuleMatchRequest request;
    request.requestId = QStringLiteral("request-1");
    request.asOfDate = QStringLiteral("2026-08-28");
    request.materialRef = MakeReference(QStringLiteral("aluminum-6061"),
                                         QStringLiteral("materials"),
                                         QStringLiteral("2026.08"));
    request.machineProfileRef = MakeReference(QStringLiteral("front-z-3axis"),
                                               QStringLiteral("machines"),
                                               QStringLiteral("2026.08"));
    request.toolCatalogRef = MakeReference(QStringLiteral("tool-catalog"),
                                            QStringLiteral("tools"),
                                            QStringLiteral("2026.08"));
    request.ruleCatalogRef = MakeReference(QStringLiteral("hole-rule-catalog"),
                                            QStringLiteral("hole-rule-catalog"),
                                            QStringLiteral("2026.08.1"));
    request.feature.geometryRef = QStringLiteral("face:42/hole:7");
    request.feature.geometryRevision = QStringLiteral("sha256:feature-a");
    request.feature.setupRef = QStringLiteral("setup-front-v1");
    request.feature.setupConfirmed = true;
    request.feature.region = HoleRuleFeatureRegion::Front;
    request.feature.axisMatchesPositiveZ = true;
    request.feature.category = QStringLiteral("single");
    HoleRuleLayerInput layer;
    layer.ordinal = 1;
    layer.kind = QStringLiteral("cylindrical");
    layer.diameterMm = 5.0;
    layer.startPlaneMm = 0.0;
    layer.endPlaneMm = -10.0;
    layer.through = false;
    layer.sourceToleranceRef = QStringLiteral("recognizer-v1");
    request.feature.layers = {layer};
    return request;
}

bool TestExplicitEndpointsAndEligibility()
{
    AutomationTemplateDocument document = MakeDocument();
    document.featureMatchingRules = {
        MakeRule(QStringLiteral("closed"), 10, 5.0, true, 6.0, true,
                 {QStringLiteral("drill-core")})
    };
    HoleRuleMatchRequest request = MakeRequest();
    HoleRuleMatchResult result =
        AutoHoleRuleMatchingService::Match(document, request);
    if (!Expect(result.state == HoleRuleMatchState::Draft
                    && result.candidate.ruleId == QStringLiteral("closed"),
                "a closed lower endpoint should produce a draft")) {
        return false;
    }
    document.featureMatchingRules.first().holeRule.layerConditions.first()
        .lowerClosed = false;
    result = AutoHoleRuleMatchingService::Match(document, request);
    if (!Expect(result.state == HoleRuleMatchState::Rejected
                    && result.reasons.first().code == QStringLiteral("RULE_NOT_FOUND"),
                "an open lower endpoint should not match")) {
        return false;
    }
    document.featureMatchingRules.first().holeRule.layerConditions.first()
        .lowerClosed = true;
    document.featureMatchingRules.first().holeRule.layerConditions.first()
        .upperMm = 5.0;
    document.featureMatchingRules.first().holeRule.layerConditions.first()
        .upperClosed = false;
    result = AutoHoleRuleMatchingService::Match(document, request);
    if (!Expect(result.state == HoleRuleMatchState::Rejected,
                "an open upper endpoint should not match")) {
        return false;
    }
    document.featureMatchingRules.first().holeRule.layerConditions.first()
        .upperClosed = true;
    request.asOfDate = QStringLiteral("2026-08-31");
    result = AutoHoleRuleMatchingService::Match(document, request);
    if (!Expect(result.state == HoleRuleMatchState::Draft,
                "the effective-date upper endpoint should be closed")) {
        return false;
    }
    request.materialRef.id = QStringLiteral("steel");
    if (!Expect(AutoHoleRuleMatchingService::Match(document, request).state
                    == HoleRuleMatchState::Rejected,
                "material eligibility should use an exact condition")) {
        return false;
    }
    request = MakeRequest();
    request.asOfDate = QStringLiteral("2026-09-01");
    result = AutoHoleRuleMatchingService::Match(document, request);
    return Expect(result.state == HoleRuleMatchState::Rejected
                      && result.explanation.filterReasons.size() == 1
                      && result.explanation.filterReasons.first().code
                          == QStringLiteral("RULE_NOT_EFFECTIVE")
                      && result.explanation.filterReasons.first().fieldPath
                          == QStringLiteral("featureMatchingRules[0].holeRule"),
                  "ineligible dates should retain a stable filtering reason");
}

bool TestPriorityAndLayerMatching()
{
    AutomationTemplateDocument document = MakeDocument();
    document.featureMatchingRules = {
        MakeRule(QStringLiteral("low"), 10, 3.0, true, 8.0, true,
                 {QStringLiteral("drill-core")}),
        MakeRule(QStringLiteral("high"), 20, 3.0, true, 8.0, true,
                 {QStringLiteral("drill-core")})
    };
    HoleRuleMatchResult result = AutoHoleRuleMatchingService::Match(
        document, MakeRequest());
    if (!Expect(result.state == HoleRuleMatchState::Draft
                    && result.candidate.ruleId == QStringLiteral("high")
                    && result.explanation.topRuleIds
                        == QStringList{QStringLiteral("high")},
                "the unique highest explicit priority should win")) {
        return false;
    }
    document.featureMatchingRules[0].holeRule.priority = 20;
    result = AutoHoleRuleMatchingService::Match(document, MakeRequest());
    if (!Expect(result.state == HoleRuleMatchState::Conflict
                    && result.reasons.first().code
                        == QStringLiteral("RULE_AMBIGUOUS")
                    && result.explanation.topRuleIds.size() == 2,
                "equal highest priorities should block as a conflict")) {
        return false;
    }

    AutomationTemplateDocument layered = MakeDocument();
    FeatureMatchingRule rule = MakeRule(
        QStringLiteral("two-layer"), 20, 3.0, true, 8.0, true,
        {QStringLiteral("drill-core")});
    HoleRuleLayerCondition second = rule.holeRule.layerConditions.first();
    second.ordinal = 2;
    second.lowerMm = 8.0;
    second.upperMm = 12.0;
    rule.holeRule.layerConditions.append(second);
    layered.featureMatchingRules = {rule};
    HoleRuleMatchRequest request = MakeRequest();
    HoleRuleLayerInput layer = request.feature.layers.first();
    layer.ordinal = 2;
    layer.diameterMm = 10.0;
    layer.startPlaneMm = -10.0;
    layer.endPlaneMm = -20.0;
    request.feature.layers.append(layer);
    result = AutoHoleRuleMatchingService::Match(layered, request);
    return Expect(result.state == HoleRuleMatchState::Draft
                      && result.explanation.layerMatches.size() == 2,
                  "ordered multi-layer conditions should all match");
}

bool TestPlanValidationAndSemanticGate()
{
    AutomationTemplateDocument document = MakeDocument();
    document.featureMatchingRules = {
        MakeRule(QStringLiteral("rule"), 20, 3.0, true, 8.0, true,
                 {QStringLiteral("drill-core")})
    };
    HoleRuleMatchResult result = AutoHoleRuleMatchingService::Match(
        document, MakeRequest());
    if (!Expect(result.state == HoleRuleMatchState::Draft
                    && result.candidate.steps.first().coolantCompat
                        == QStringLiteral("M8"),
                "normalized steps should preserve opaque coolant compatibility")) {
        return false;
    }
    document.machiningPlanSteps.first().depthExpression = QStringLiteral("H1-AT");
    result = AutoHoleRuleMatchingService::Match(document, MakeRequest());
    if (!Expect(result.state == HoleRuleMatchState::Rejected
                    && result.reasons.first().code
                        == QStringLiteral("SEMANTIC_VARIABLE_UNCONFIRMED"),
                "unconfirmed Hn or AT semantics should block a rule")) {
        return false;
    }
    document.machiningPlanSteps.first().depthExpression = QStringLiteral("20");
    result = AutoHoleRuleMatchingService::Match(document, MakeRequest());
    if (!Expect(result.state == HoleRuleMatchState::Rejected
                    && result.reasons.first().code
                        == QStringLiteral("DEPTH_OUT_OF_BOUNDS"),
                "a depth beyond the referenced layer should be rejected")) {
        return false;
    }
    document.machiningPlanSteps.first().depthExpression = QStringLiteral("8");
    document.machiningPlanSteps.first().sourceSlot = 12;
    result = AutoHoleRuleMatchingService::Match(document, MakeRequest());
    return Expect(result.state == HoleRuleMatchState::Rejected
                      && result.reasons.first().code
                          == QStringLiteral("RULE_STEP_INVALID"),
                  "an invalid normalized source slot should reject the rule");
}

bool TestRequiredFeatureFieldsAndEvidence()
{
    AutomationTemplateDocument document = MakeDocument();
    document.featureMatchingRules = {
        MakeRule(QStringLiteral("rule"), 20, 3.0, true, 8.0, true,
                 {QStringLiteral("drill-core")})
    };
    HoleRuleMatchRequest request = MakeRequest();
    request.feature.setupRef.clear();
    HoleRuleMatchResult result =
        AutoHoleRuleMatchingService::Match(document, request);
    if (!Expect(HasReason(result, QStringLiteral("SETUP_UNCONFIRMED"),
                          QStringLiteral("feature.setupRef")),
                "a missing setup reference should be rejected")) {
        return false;
    }
    request = MakeRequest();
    request.feature.geometryRevision.clear();
    result = AutoHoleRuleMatchingService::Match(document, request);
    if (!Expect(HasReason(result, QStringLiteral("GEOMETRY_REF_MISSING"),
                          QStringLiteral("feature.geometryRevision")),
                "a missing geometry revision should be rejected")) {
        return false;
    }
    request = MakeRequest();
    request.feature.category.clear();
    result = AutoHoleRuleMatchingService::Match(document, request);
    if (!Expect(HasReason(result, QStringLiteral("GEOMETRY_UNSUPPORTED"),
                          QStringLiteral("feature.category")),
                "a missing feature category should be rejected")) {
        return false;
    }
    request = MakeRequest();
    request.feature.layers.first().sourceToleranceRef.clear();
    result = AutoHoleRuleMatchingService::Match(document, request);
    if (!Expect(HasReason(result, QStringLiteral("LAYER_GEOMETRY_INVALID"),
                          QStringLiteral(
                              "feature.layers[0].sourceToleranceRef")),
                "a missing source tolerance reference should be rejected")) {
        return false;
    }
    request = MakeRequest();
    request.feature.rejectionEvidence =
        QStringList{QStringLiteral("non-manifold layer")};
    result = AutoHoleRuleMatchingService::Match(document, request);
    return Expect(HasReason(result, QStringLiteral("GEOMETRY_UNSUPPORTED"),
                            QStringLiteral("feature.rejectionEvidence"))
                      && !result.reasons.isEmpty()
                      && result.reasons.first().message
                          .contains(QStringLiteral("non-manifold layer")),
                  "upstream rejection evidence should be retained and rejected");
}

bool TestLayerContinuityAndRuleCatalogIdentity()
{
    AutomationTemplateDocument document = MakeDocument();
    FeatureMatchingRule rule = MakeRule(
        QStringLiteral("two-layer"), 20, 3.0, true, 8.0, true,
        {QStringLiteral("drill-core")});
    HoleRuleLayerCondition second = rule.holeRule.layerConditions.first();
    second.ordinal = 2;
    second.lowerMm = 8.0;
    second.upperMm = 12.0;
    rule.holeRule.layerConditions.append(second);
    document.featureMatchingRules = {rule};
    HoleRuleMatchRequest request = MakeRequest();
    HoleRuleLayerInput second_layer = request.feature.layers.first();
    second_layer.ordinal = 2;
    second_layer.diameterMm = 10.0;
    second_layer.startPlaneMm = -9.0;
    second_layer.endPlaneMm = -20.0;
    request.feature.layers.append(second_layer);
    HoleRuleMatchResult result =
        AutoHoleRuleMatchingService::Match(document, request);
    if (!Expect(HasReason(result, QStringLiteral("LAYER_SEQUENCE_INVALID"),
                          QStringLiteral("feature.layers[1].startPlaneMm")),
                "a gap between adjacent layers should be rejected exactly")) {
        return false;
    }
    request.feature.layers[1].startPlaneMm = -11.0;
    result = AutoHoleRuleMatchingService::Match(document, request);
    if (!Expect(HasReason(result, QStringLiteral("LAYER_SEQUENCE_INVALID"),
                          QStringLiteral("feature.layers[1].startPlaneMm")),
                "an overlap between adjacent layers should be rejected exactly")) {
        return false;
    }
    request = MakeRequest();
    request.ruleCatalogRef.id = QStringLiteral("other-rules");
    result = AutoHoleRuleMatchingService::Match(document, request);
    return Expect(HasReason(result, QStringLiteral("CATALOG_VERSION_MISSING"),
                            QStringLiteral("ruleCatalogRef.id")),
                  "the rule catalog identity should not be ignored");
}

bool TestUnprovenStartExpressionIsRejected()
{
    AutomationTemplateDocument document = MakeDocument();
    document.featureMatchingRules = {
        MakeRule(QStringLiteral("rule"), 20, 3.0, true, 8.0, true,
                 {QStringLiteral("drill-core")})
    };
    document.machiningPlanSteps.first().startExpression = QStringLiteral("5");
    const HoleRuleMatchResult result =
        AutoHoleRuleMatchingService::Match(document, MakeRequest());
    return Expect(result.state == HoleRuleMatchState::Rejected
                      && HasReason(result, QStringLiteral("FORMULA_REJECTED"),
                                   QStringLiteral("holeRule.planStepIds[0]")),
                  "a start expression without an approved geometric meaning should reject");
}

bool TestInputRejectionDoesNotMutateDocument()
{
    AutomationTemplateDocument document = MakeDocument();
    document.featureMatchingRules = {
        MakeRule(QStringLiteral("rule"), 20, 3.0, true, 8.0, true,
                 {QStringLiteral("drill-core")})
    };
    const QList<FeatureMatchingRule> before = document.featureMatchingRules;
    HoleRuleMatchRequest request = MakeRequest();
    request.feature.region = HoleRuleFeatureRegion::Side;
    const HoleRuleMatchResult result =
        AutoHoleRuleMatchingService::Match(document, request);
    return Expect(result.state == HoleRuleMatchState::Rejected
                      && result.reasons.first().code
                          == QStringLiteral("GEOMETRY_UNSUPPORTED")
                      && document.featureMatchingRules.first().id
                          == before.first().id,
                  "unsupported geometry should reject without changing source rules");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    if (!TestExplicitEndpointsAndEligibility() || !TestPriorityAndLayerMatching()
        || !TestPlanValidationAndSemanticGate()
        || !TestRequiredFeatureFieldsAndEvidence()
        || !TestLayerContinuityAndRuleCatalogIdentity()
        || !TestUnprovenStartExpressionIsRejected()
        || !TestInputRejectionDoesNotMutateDocument()) {
        return 1;
    }
    std::cout << "Auto-hole rule matching service tests passed." << std::endl;
    return 0;
}
