#pragma once

#include "../core/automation/AutomationTemplateDocument.h"

enum class HoleRuleFeatureRegion {
    Front,
    Side,
    Unsupported
};

struct VersionedCatalogReference {
    QString id;
    QString catalogId;
    QString catalogVersion;
};

struct HoleRuleLayerInput {
    int ordinal = 0;
    QString kind;
    double diameterMm = 0.0;
    double startPlaneMm = 0.0;
    double endPlaneMm = 0.0;
    bool through = false;
    QString sourceToleranceRef;
};

struct HoleRuleFeatureInput {
    QString geometryRef;
    QString geometryRevision;
    QString setupRef;
    bool setupConfirmed = false;
    HoleRuleFeatureRegion region = HoleRuleFeatureRegion::Unsupported;
    bool axisMatchesPositiveZ = false;
    QString category;
    QList<HoleRuleLayerInput> layers;
    QStringList rejectionEvidence;
};

struct HoleRuleMatchRequest {
    QString requestId;
    QString asOfDate;
    VersionedCatalogReference materialRef;
    VersionedCatalogReference machineProfileRef;
    VersionedCatalogReference toolCatalogRef;
    VersionedCatalogReference ruleCatalogRef;
    HoleRuleFeatureInput feature;
};

enum class HoleRuleMatchState {
    Draft,
    Conflict,
    Stale,
    Rejected
};

struct HoleRuleMatchReason {
    QString code;
    QString fieldPath;
    QString message;
};

struct HoleRuleMatchExplanation {
    QStringList consideredRuleIds;
    QStringList filteredRuleIds;
    QStringList topRuleIds;
    QStringList layerMatches;
};

struct HoleRuleMatchCandidate {
    QString geometryRef;
    QString geometryRevision;
    QString layerSignature;
    QString ruleId;
    QString ruleVersion;
    QString planId;
    QString planVersion;
    VersionedCatalogReference materialRef;
    VersionedCatalogReference machineProfileRef;
    VersionedCatalogReference toolCatalogRef;
    VersionedCatalogReference ruleCatalogRef;
    QList<AutomationMachiningPlanStep> steps;
};

struct HoleRuleMatchResult {
    HoleRuleMatchState state = HoleRuleMatchState::Rejected;
    HoleRuleMatchCandidate candidate;
    HoleRuleMatchExplanation explanation;
    QList<HoleRuleMatchReason> reasons;
};

class AutoHoleRuleMatchingService {
public:
    static HoleRuleMatchResult Match(
        const AutomationTemplateDocument &document,
        const HoleRuleMatchRequest &request);
};
