#pragma once

#include <QList>
#include <QString>

#include <optional>

enum class CandidateReconciliationState {
    Draft,
    Selected,
    Conflict,
    Stale,
    Rejected
};

enum class CandidateManualEditDisposition {
    None
};

struct VersionedCandidateReference {
    QString id;
    QString version;
};

inline bool operator==(const VersionedCandidateReference &left,
                       const VersionedCandidateReference &right)
{
    return left.id == right.id && left.version == right.version;
}

inline bool operator!=(const VersionedCandidateReference &left,
                       const VersionedCandidateReference &right)
{
    return !(left == right);
}

struct VersionedToolCatalogReference {
    QString id;
    QString catalogId;
    QString catalogVersion;
};

struct CandidateFeatureIdentity {
    QString geometryRef;
    QString geometryRevision;
    QString layerSignature;
};

struct CandidateProvenance {
    VersionedCandidateReference ruleRef;
    VersionedCandidateReference planRef;
    VersionedCandidateReference templateRef;
    VersionedCandidateReference materialRef;
    VersionedCandidateReference machineProfileRef;
    VersionedToolCatalogReference toolCatalogRef;
};

struct PreviousCandidateSnapshot {
    QString candidateId;
    QString snapshotRevision;
    CandidateReconciliationState lifecycleState =
        CandidateReconciliationState::Rejected;
    std::optional<CandidateReconciliationState> lastEditableState;
    CandidateFeatureIdentity featureIdentity;
    CandidateProvenance provenance;
    std::optional<VersionedCandidateReference> selectedPlanRef;
    QString baseSnapshotRef;
    QString observationRef;
};

struct CandidateReconciliationIssue {
    QString code;
    QString fieldPath;
};

struct ObservedCandidateSnapshot {
    QString observedCandidateId;
    QString observedRevision;
    CandidateReconciliationState state = CandidateReconciliationState::Rejected;
    CandidateFeatureIdentity featureIdentity;
    std::optional<CandidateProvenance> provenance;
    QList<CandidateReconciliationIssue> reasons;
};

enum class CandidateResolutionSide {
    Previous,
    Observed,
    Request
};

enum class CandidateReferenceResolutionState {
    Resolvable,
    Missing,
    Withdrawn
};

enum class CandidateReferenceIdentityKind {
    Opaque,
    Versioned,
    Catalog
};

struct CandidateReferenceIdentity {
    CandidateReferenceIdentityKind kind = CandidateReferenceIdentityKind::Opaque;
    QString value;
    QString id;
    QString version;
    QString catalogId;
    QString catalogVersion;
};

struct CandidateResolutionEntry {
    CandidateResolutionSide side = CandidateResolutionSide::Request;
    QString referencePath;
    CandidateReferenceIdentity referenceIdentity;
    CandidateReferenceResolutionState status =
        CandidateReferenceResolutionState::Missing;
};

struct CandidateReconciliationResolutionView {
    QList<CandidateResolutionEntry> entries;
};

struct CandidateReconciliationExplanation {
    QString requestId;
    QString baseSnapshotRef;
    QString observationRef;
};

struct CandidateReconciliationRequest {
    QString requestId;
    ObservedCandidateSnapshot observedCandidate;
    std::optional<PreviousCandidateSnapshot> previousCandidate;
    CandidateReconciliationResolutionView resolutionView;
    VersionedCandidateReference reconciliationPolicyVersion;
};

struct CandidateReconciliationResult {
    CandidateReconciliationState state = CandidateReconciliationState::Rejected;
    std::optional<PreviousCandidateSnapshot> candidate;
    CandidateManualEditDisposition manualEditDisposition =
        CandidateManualEditDisposition::None;
    QList<CandidateReconciliationIssue> differences;
    QList<CandidateReconciliationIssue> reasons;
    CandidateReconciliationExplanation explanation;
};

class HoleCandidateReconciliationService {
public:
    static CandidateReconciliationResult Reconcile(
        const CandidateReconciliationRequest &request);
};
