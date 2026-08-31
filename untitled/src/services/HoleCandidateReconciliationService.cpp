#include "HoleCandidateReconciliationService.h"

namespace {

const QString kGeometryRefPath = QStringLiteral("/featureIdentity/geometryRef");
const QString kGeometryRevisionPath =
    QStringLiteral("/featureIdentity/geometryRevision");
const QString kLayerSignaturePath =
    QStringLiteral("/featureIdentity/layerSignature");
const QString kRulePath = QStringLiteral("/provenance/ruleRef");
const QString kPlanPath = QStringLiteral("/provenance/planRef");
const QString kTemplatePath = QStringLiteral("/provenance/templateRef");
const QString kMaterialPath = QStringLiteral("/provenance/materialRef");
const QString kMachinePath = QStringLiteral("/provenance/machineProfileRef");
const QString kToolCatalogPath = QStringLiteral("/provenance/toolCatalogRef");
const QString kPolicyPath = QStringLiteral("/reconciliationPolicyVersion");

CandidateReconciliationIssue Issue(const QString &code, const QString &path)
{
    return {code, path};
}

bool IsValidReference(const VersionedCandidateReference &reference)
{
    return !reference.id.isEmpty() && !reference.version.isEmpty();
}

bool IsValidToolCatalogReference(const VersionedToolCatalogReference &reference)
{
    return !reference.id.isEmpty() && !reference.catalogId.isEmpty()
        && !reference.catalogVersion.isEmpty();
}

bool IsValidFeatureIdentity(const CandidateFeatureIdentity &identity)
{
    return !identity.geometryRef.isEmpty() && !identity.geometryRevision.isEmpty()
        && !identity.layerSignature.isEmpty();
}

bool IsValidProvenance(const CandidateProvenance &provenance)
{
    return IsValidReference(provenance.ruleRef)
        && IsValidReference(provenance.planRef)
        && IsValidReference(provenance.templateRef)
        && IsValidReference(provenance.materialRef)
        && IsValidReference(provenance.machineProfileRef)
        && IsValidToolCatalogReference(provenance.toolCatalogRef);
}

CandidateReferenceIdentity OpaqueIdentity(const QString &value)
{
    CandidateReferenceIdentity identity;
    identity.kind = CandidateReferenceIdentityKind::Opaque;
    identity.value = value;
    return identity;
}

CandidateReferenceIdentity VersionedIdentity(
    const VersionedCandidateReference &reference)
{
    CandidateReferenceIdentity identity;
    identity.kind = CandidateReferenceIdentityKind::Versioned;
    identity.id = reference.id;
    identity.version = reference.version;
    return identity;
}

CandidateReferenceIdentity CatalogIdentity(
    const VersionedToolCatalogReference &reference)
{
    CandidateReferenceIdentity identity;
    identity.kind = CandidateReferenceIdentityKind::Catalog;
    identity.id = reference.id;
    identity.catalogId = reference.catalogId;
    identity.catalogVersion = reference.catalogVersion;
    return identity;
}

bool SameIdentity(const CandidateReferenceIdentity &left,
                  const CandidateReferenceIdentity &right)
{
    return left.kind == right.kind && left.value == right.value
        && left.id == right.id && left.version == right.version
        && left.catalogId == right.catalogId
        && left.catalogVersion == right.catalogVersion;
}

bool SameProvenance(const CandidateProvenance &left,
                    const CandidateProvenance &right)
{
    return left.ruleRef == right.ruleRef && left.planRef == right.planRef
        && left.templateRef == right.templateRef
        && left.materialRef == right.materialRef
        && left.machineProfileRef == right.machineProfileRef
        && left.toolCatalogRef.id == right.toolCatalogRef.id
        && left.toolCatalogRef.catalogId == right.toolCatalogRef.catalogId
        && left.toolCatalogRef.catalogVersion == right.toolCatalogRef.catalogVersion;
}

CandidateReconciliationResult Rejected(const QString &code, const QString &path,
                                       const CandidateReconciliationRequest &request)
{
    CandidateReconciliationResult result;
    result.state = CandidateReconciliationState::Rejected;
    result.reasons = {Issue(code, path)};
    result.explanation.requestId = request.requestId;
    result.explanation.observationRef = request.observedCandidate.observedCandidateId;
    return result;
}

PreviousCandidateSnapshot ReconcileSnapshot(
    const PreviousCandidateSnapshot &source, CandidateReconciliationState state,
    const CandidateReconciliationRequest &request)
{
    PreviousCandidateSnapshot result = source;
    result.lifecycleState = state;
    result.baseSnapshotRef = source.snapshotRevision;
    result.observationRef = request.observedCandidate.observedCandidateId;
    result.snapshotRevision = source.snapshotRevision + QStringLiteral(":reconciled:")
        + request.observedCandidate.observedRevision;
    return result;
}

PreviousCandidateSnapshot SnapshotFromObserved(
    const CandidateReconciliationRequest &request,
    CandidateReconciliationState state)
{
    PreviousCandidateSnapshot result;
    result.candidateId = request.observedCandidate.observedCandidateId;
    result.snapshotRevision = request.observedCandidate.observedRevision
        + QStringLiteral(":reconciled");
    result.lifecycleState = state;
    result.featureIdentity = request.observedCandidate.featureIdentity;
    result.provenance = *request.observedCandidate.provenance;
    if (state == CandidateReconciliationState::Selected) {
        result.selectedPlanRef = result.provenance.planRef;
    }
    result.observationRef = request.observedCandidate.observedCandidateId;
    return result;
}

void SetExplanation(CandidateReconciliationResult *result,
                    const CandidateReconciliationRequest &request)
{
    result->explanation.requestId = request.requestId;
    result->explanation.observationRef = request.observedCandidate.observedCandidateId;
    if (request.previousCandidate.has_value()) {
        result->explanation.baseSnapshotRef =
            request.previousCandidate->snapshotRevision;
    }
}

bool IsAllowedObservedState(CandidateReconciliationState state)
{
    return state == CandidateReconciliationState::Draft
        || state == CandidateReconciliationState::Conflict
        || state == CandidateReconciliationState::Rejected;
}

bool IsAllowedPreviousState(CandidateReconciliationState state)
{
    return state == CandidateReconciliationState::Draft
        || state == CandidateReconciliationState::Selected
        || state == CandidateReconciliationState::Conflict
        || state == CandidateReconciliationState::Stale
        || state == CandidateReconciliationState::Rejected;
}

bool IsEditableState(CandidateReconciliationState state)
{
    return state == CandidateReconciliationState::Draft
        || state == CandidateReconciliationState::Selected;
}

bool IsValidPreviousSnapshot(const PreviousCandidateSnapshot &candidate,
                             QString *invalid_path)
{
    if (candidate.candidateId.isEmpty() || candidate.snapshotRevision.isEmpty()
        || !IsAllowedPreviousState(candidate.lifecycleState)
        || !IsValidFeatureIdentity(candidate.featureIdentity)
        || !IsValidProvenance(candidate.provenance)) {
        *invalid_path = QStringLiteral("/previousCandidate");
        return false;
    }
    if (candidate.lifecycleState == CandidateReconciliationState::Stale) {
        if (!candidate.lastEditableState.has_value()
            || !IsEditableState(*candidate.lastEditableState)) {
            *invalid_path = QStringLiteral("/previousCandidate/lastEditableState");
            return false;
        }
    } else if (candidate.lastEditableState.has_value()) {
        *invalid_path = QStringLiteral("/previousCandidate/lastEditableState");
        return false;
    }
    const bool requires_selection = candidate.lifecycleState
            == CandidateReconciliationState::Selected
        || (candidate.lastEditableState.has_value()
            && *candidate.lastEditableState
                == CandidateReconciliationState::Selected);
    if (requires_selection) {
        if (!candidate.selectedPlanRef.has_value()
            || *candidate.selectedPlanRef != candidate.provenance.planRef) {
            *invalid_path = QStringLiteral("/previousCandidate/selectedPlanRef");
            return false;
        }
    } else if (candidate.selectedPlanRef.has_value()) {
        *invalid_path = QStringLiteral("/previousCandidate/selectedPlanRef");
        return false;
    }
    return true;
}

bool IsValidObservedSnapshot(const ObservedCandidateSnapshot &observed,
                             QString *invalid_path)
{
    if (observed.observedCandidateId.isEmpty() || observed.observedRevision.isEmpty()
        || !IsAllowedObservedState(observed.state)
        || !IsValidFeatureIdentity(observed.featureIdentity)) {
        *invalid_path = QStringLiteral("/observedCandidate/observedRevision");
        return false;
    }
    if (observed.state == CandidateReconciliationState::Draft) {
        if (!observed.provenance.has_value()
            || !IsValidProvenance(*observed.provenance) || !observed.reasons.isEmpty()) {
            *invalid_path = QStringLiteral("/observedCandidate");
            return false;
        }
    } else if (observed.provenance.has_value() || observed.reasons.isEmpty()) {
        *invalid_path = QStringLiteral("/observedCandidate");
        return false;
    }
    return true;
}

struct ExpectedResolutionEntry {
    CandidateResolutionSide side;
    QString path;
    CandidateReferenceIdentity identity;
};

void AddExpectedFeatureEntries(QList<ExpectedResolutionEntry> *entries,
                               CandidateResolutionSide side,
                               const CandidateFeatureIdentity &feature)
{
    entries->append({side, kGeometryRefPath, OpaqueIdentity(feature.geometryRef)});
    entries->append(
        {side, kGeometryRevisionPath, OpaqueIdentity(feature.geometryRevision)});
    entries->append(
        {side, kLayerSignaturePath, OpaqueIdentity(feature.layerSignature)});
}

void AddExpectedProvenanceEntries(QList<ExpectedResolutionEntry> *entries,
                                  CandidateResolutionSide side,
                                  const CandidateProvenance &provenance)
{
    entries->append({side, kRulePath, VersionedIdentity(provenance.ruleRef)});
    entries->append({side, kPlanPath, VersionedIdentity(provenance.planRef)});
    entries->append(
        {side, kTemplatePath, VersionedIdentity(provenance.templateRef)});
    entries->append(
        {side, kMaterialPath, VersionedIdentity(provenance.materialRef)});
    entries->append(
        {side, kMachinePath, VersionedIdentity(provenance.machineProfileRef)});
    entries->append(
        {side, kToolCatalogPath, CatalogIdentity(provenance.toolCatalogRef)});
}

bool ValidateResolutionView(const CandidateReconciliationRequest &request,
                            QString *invalid_path)
{
    QList<ExpectedResolutionEntry> expected;
    expected.append({CandidateResolutionSide::Request, kPolicyPath,
                     VersionedIdentity(request.reconciliationPolicyVersion)});
    if (request.observedCandidate.state == CandidateReconciliationState::Draft) {
        AddExpectedFeatureEntries(&expected, CandidateResolutionSide::Observed,
                                  request.observedCandidate.featureIdentity);
        AddExpectedProvenanceEntries(&expected, CandidateResolutionSide::Observed,
                                     *request.observedCandidate.provenance);
        if (request.previousCandidate.has_value()) {
            AddExpectedFeatureEntries(&expected, CandidateResolutionSide::Previous,
                                      request.previousCandidate->featureIdentity);
            AddExpectedProvenanceEntries(&expected,
                                         CandidateResolutionSide::Previous,
                                         request.previousCandidate->provenance);
        }
    }
    for (const ExpectedResolutionEntry &entry : expected) {
        int found = 0;
        for (const CandidateResolutionEntry &actual : request.resolutionView.entries) {
            if (actual.side == entry.side && actual.referencePath == entry.path) {
                ++found;
                if (!SameIdentity(actual.referenceIdentity, entry.identity)) {
                    *invalid_path = entry.path;
                    return false;
                }
            }
        }
        if (found != 1) {
            *invalid_path = entry.path;
            return false;
        }
    }
    for (const CandidateResolutionEntry &actual : request.resolutionView.entries) {
        bool allowed = false;
        for (const ExpectedResolutionEntry &entry : expected) {
            if (actual.side == entry.side && actual.referencePath == entry.path) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            *invalid_path = actual.referencePath;
            return false;
        }
    }
    return true;
}

CandidateReferenceResolutionState ResolutionStatus(
    const CandidateReconciliationResolutionView &view,
    CandidateResolutionSide side, const QString &path)
{
    for (const CandidateResolutionEntry &entry : view.entries) {
        if (entry.side == side && entry.referencePath == path) {
            return entry.status;
        }
    }
    return CandidateReferenceResolutionState::Missing;
}

bool HasUnresolvedPreviousReference(const CandidateReconciliationResolutionView &view,
                                    QString *path)
{
    const QList<QString> paths = {kGeometryRefPath, kGeometryRevisionPath,
                                  kLayerSignaturePath, kRulePath, kPlanPath,
                                  kTemplatePath, kMaterialPath, kMachinePath,
                                  kToolCatalogPath};
    for (const QString &candidate_path : paths) {
        if (ResolutionStatus(view, CandidateResolutionSide::Previous,
                             candidate_path)
            != CandidateReferenceResolutionState::Resolvable) {
            *path = candidate_path;
            return true;
        }
    }
    return false;
}

bool HasUnresolvedObservedReference(const CandidateReconciliationResolutionView &view,
                                    QString *path)
{
    const QList<QString> paths = {kGeometryRefPath, kGeometryRevisionPath,
                                  kLayerSignaturePath, kRulePath, kPlanPath,
                                  kTemplatePath, kMaterialPath, kMachinePath,
                                  kToolCatalogPath};
    for (const QString &candidate_path : paths) {
        if (ResolutionStatus(view, CandidateResolutionSide::Observed,
                             candidate_path)
            != CandidateReferenceResolutionState::Resolvable) {
            *path = candidate_path;
            return true;
        }
    }
    return false;
}

CandidateReconciliationResult CopyOldState(
    const CandidateReconciliationRequest &request,
    CandidateReconciliationState state, const CandidateReconciliationIssue &issue)
{
    CandidateReconciliationResult result;
    result.state = state;
    result.candidate = ReconcileSnapshot(*request.previousCandidate, state, request);
    SetExplanation(&result, request);
    if (state == CandidateReconciliationState::Conflict
        || state == CandidateReconciliationState::Stale) {
        result.differences = {issue};
    }
    return result;
}

} // namespace

CandidateReconciliationResult HoleCandidateReconciliationService::Reconcile(
    const CandidateReconciliationRequest &request)
{
    QString invalid_path;
    if (!IsValidObservedSnapshot(request.observedCandidate, &invalid_path)) {
        return Rejected(QStringLiteral("CANDIDATE_SNAPSHOT_INVALID"), invalid_path,
                        request);
    }
    if (request.previousCandidate.has_value()
        && !IsValidPreviousSnapshot(*request.previousCandidate, &invalid_path)) {
        return Rejected(QStringLiteral("CANDIDATE_SNAPSHOT_INVALID"), invalid_path,
                        request);
    }
    if (!ValidateResolutionView(request, &invalid_path)) {
        return Rejected(QStringLiteral("RESOLUTION_VIEW_INVALID"), invalid_path,
                        request);
    }
    if (!IsValidReference(request.reconciliationPolicyVersion)
        || ResolutionStatus(request.resolutionView, CandidateResolutionSide::Request,
                            kPolicyPath)
            != CandidateReferenceResolutionState::Resolvable) {
        return Rejected(QStringLiteral("RECONCILIATION_POLICY_UNRESOLVED"),
                        kPolicyPath, request);
    }

    if (request.observedCandidate.state != CandidateReconciliationState::Draft) {
        CandidateReconciliationResult result;
        result.state = request.observedCandidate.state;
        result.reasons = request.observedCandidate.reasons;
        SetExplanation(&result, request);
        return result;
    }

    if (!request.previousCandidate.has_value()) {
        CandidateReconciliationResult result;
        result.state = CandidateReconciliationState::Draft;
        result.candidate = SnapshotFromObserved(request,
                                                CandidateReconciliationState::Draft);
        SetExplanation(&result, request);
        return result;
    }

    if (request.previousCandidate->lifecycleState
        == CandidateReconciliationState::Conflict) {
        return CopyOldState(request, CandidateReconciliationState::Conflict,
                            Issue(QStringLiteral("CANDIDATE_REPROCESSING_REQUIRED"),
                                  QStringLiteral("/previousCandidate/lifecycleState")));
    }
    QString unresolved_path;
    if (HasUnresolvedPreviousReference(request.resolutionView, &unresolved_path)) {
        return CopyOldState(request, CandidateReconciliationState::Stale,
                            Issue(QStringLiteral("CANDIDATE_REFERENCE_UNRESOLVED"),
                                  unresolved_path));
    }
    if (HasUnresolvedObservedReference(request.resolutionView, &unresolved_path)) {
        return Rejected(QStringLiteral("CANDIDATE_SNAPSHOT_INVALID"), unresolved_path,
                        request);
    }

    const PreviousCandidateSnapshot &previous = *request.previousCandidate;
    const ObservedCandidateSnapshot &observed = request.observedCandidate;
    if (previous.featureIdentity.geometryRef != observed.featureIdentity.geometryRef) {
        return CopyOldState(request, CandidateReconciliationState::Conflict,
                            Issue(QStringLiteral("GEOMETRY_IDENTITY_CHANGED"),
                                  kGeometryRefPath));
    }
    if (previous.featureIdentity.geometryRevision
        != observed.featureIdentity.geometryRevision) {
        return CopyOldState(request, CandidateReconciliationState::Conflict,
                            Issue(QStringLiteral("GEOMETRY_IDENTITY_CHANGED"),
                                  kGeometryRevisionPath));
    }
    if (previous.featureIdentity.layerSignature
        != observed.featureIdentity.layerSignature) {
        return CopyOldState(request, CandidateReconciliationState::Conflict,
                            Issue(QStringLiteral("GEOMETRY_IDENTITY_CHANGED"),
                                  kLayerSignaturePath));
    }
    if (!SameProvenance(previous.provenance, *observed.provenance)) {
        const QString path = previous.provenance.ruleRef != observed.provenance->ruleRef
                ? kRulePath
                : previous.provenance.planRef != observed.provenance->planRef
                ? kPlanPath
                : previous.provenance.templateRef != observed.provenance->templateRef
                ? kTemplatePath
                : previous.provenance.materialRef != observed.provenance->materialRef
                ? kMaterialPath
                : previous.provenance.machineProfileRef
                        != observed.provenance->machineProfileRef
                ? kMachinePath
                : kToolCatalogPath;
        return CopyOldState(request, CandidateReconciliationState::Conflict,
                            Issue(QStringLiteral("CANDIDATE_PROVENANCE_CHANGED"),
                                  path));
    }

    CandidateReconciliationState state = previous.lifecycleState;
    if (state == CandidateReconciliationState::Stale) {
        state = *previous.lastEditableState;
    }
    if (state == CandidateReconciliationState::Rejected) {
        state = CandidateReconciliationState::Draft;
    }
    CandidateReconciliationResult result;
    result.state = state;
    result.candidate = SnapshotFromObserved(request, state);
    if (state == CandidateReconciliationState::Selected) {
        result.candidate->selectedPlanRef = previous.selectedPlanRef;
    }
    SetExplanation(&result, request);
    return result;
}
