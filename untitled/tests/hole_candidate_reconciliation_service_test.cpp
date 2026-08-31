#include "../src/services/HoleCandidateReconciliationService.h"

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

VersionedCandidateReference MakeReference(const QString &id,
                                          const QString &version)
{
    return {id, version};
}

VersionedToolCatalogReference MakeToolCatalogReference(
    const QString &id, const QString &catalog_id, const QString &catalog_version)
{
    return {id, catalog_id, catalog_version};
}

CandidateFeatureIdentity MakeFeatureIdentity()
{
    return {QStringLiteral("face:42/hole:7"), QStringLiteral("sha256:hole-a"),
            QStringLiteral("layers-v1")};
}

CandidateProvenance MakeProvenance()
{
    CandidateProvenance provenance;
    provenance.ruleRef = MakeReference(QStringLiteral("rule-a"), QStringLiteral("1"));
    provenance.planRef = MakeReference(QStringLiteral("plan-a"), QStringLiteral("1"));
    provenance.templateRef =
        MakeReference(QStringLiteral("template-a"), QStringLiteral("1"));
    provenance.materialRef =
        MakeReference(QStringLiteral("aluminum-6061"), QStringLiteral("2026.08"));
    provenance.machineProfileRef =
        MakeReference(QStringLiteral("front-z-3axis"), QStringLiteral("2026.08"));
    provenance.toolCatalogRef = MakeToolCatalogReference(
        QStringLiteral("tool-catalog"), QStringLiteral("tools"),
        QStringLiteral("2026.08"));
    return provenance;
}

PreviousCandidateSnapshot MakePrevious(CandidateReconciliationState state)
{
    PreviousCandidateSnapshot candidate;
    candidate.candidateId = QStringLiteral("candidate-42");
    candidate.snapshotRevision = QStringLiteral("snapshot-1");
    candidate.lifecycleState = state;
    candidate.featureIdentity = MakeFeatureIdentity();
    candidate.provenance = MakeProvenance();
    if (state == CandidateReconciliationState::Selected) {
        candidate.selectedPlanRef = candidate.provenance.planRef;
    }
    return candidate;
}

ObservedCandidateSnapshot MakeObserved(CandidateReconciliationState state)
{
    ObservedCandidateSnapshot observed;
    observed.observedCandidateId = QStringLiteral("observed-42");
    observed.observedRevision = QStringLiteral("observed-revision-1");
    observed.state = state;
    observed.featureIdentity = MakeFeatureIdentity();
    if (state == CandidateReconciliationState::Draft) {
        observed.provenance = MakeProvenance();
    } else {
        observed.reasons = {{QStringLiteral("M2_OBSERVED_RESULT"),
                             QStringLiteral("/state")}};
    }
    return observed;
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

void AddFeatureEntries(CandidateReconciliationResolutionView *view,
                       CandidateResolutionSide side,
                       const CandidateFeatureIdentity &feature)
{
    view->entries.append({side, QStringLiteral("/featureIdentity/geometryRef"),
                          OpaqueIdentity(feature.geometryRef),
                          CandidateReferenceResolutionState::Resolvable});
    view->entries.append({side, QStringLiteral("/featureIdentity/geometryRevision"),
                          OpaqueIdentity(feature.geometryRevision),
                          CandidateReferenceResolutionState::Resolvable});
    view->entries.append({side, QStringLiteral("/featureIdentity/layerSignature"),
                          OpaqueIdentity(feature.layerSignature),
                          CandidateReferenceResolutionState::Resolvable});
}

void AddProvenanceEntries(CandidateReconciliationResolutionView *view,
                          CandidateResolutionSide side,
                          const CandidateProvenance &provenance)
{
    view->entries.append({side, QStringLiteral("/provenance/ruleRef"),
                          VersionedIdentity(provenance.ruleRef),
                          CandidateReferenceResolutionState::Resolvable});
    view->entries.append({side, QStringLiteral("/provenance/planRef"),
                          VersionedIdentity(provenance.planRef),
                          CandidateReferenceResolutionState::Resolvable});
    view->entries.append({side, QStringLiteral("/provenance/templateRef"),
                          VersionedIdentity(provenance.templateRef),
                          CandidateReferenceResolutionState::Resolvable});
    view->entries.append({side, QStringLiteral("/provenance/materialRef"),
                          VersionedIdentity(provenance.materialRef),
                          CandidateReferenceResolutionState::Resolvable});
    view->entries.append({side, QStringLiteral("/provenance/machineProfileRef"),
                          VersionedIdentity(provenance.machineProfileRef),
                          CandidateReferenceResolutionState::Resolvable});
    view->entries.append({side, QStringLiteral("/provenance/toolCatalogRef"),
                          CatalogIdentity(provenance.toolCatalogRef),
                          CandidateReferenceResolutionState::Resolvable});
}

CandidateReconciliationResolutionView MakeResolutionView(
    const ObservedCandidateSnapshot &observed,
    const std::optional<PreviousCandidateSnapshot> &previous,
    const VersionedCandidateReference &policy)
{
    CandidateReconciliationResolutionView view;
    view.entries.append({CandidateResolutionSide::Request,
                         QStringLiteral("/reconciliationPolicyVersion"),
                         VersionedIdentity(policy),
                         CandidateReferenceResolutionState::Resolvable});
    if (observed.state == CandidateReconciliationState::Draft) {
        AddFeatureEntries(&view, CandidateResolutionSide::Observed,
                          observed.featureIdentity);
        AddProvenanceEntries(&view, CandidateResolutionSide::Observed,
                             *observed.provenance);
        if (previous.has_value()) {
            AddFeatureEntries(&view, CandidateResolutionSide::Previous,
                              previous->featureIdentity);
            AddProvenanceEntries(&view, CandidateResolutionSide::Previous,
                                 previous->provenance);
        }
    }
    return view;
}

CandidateReconciliationRequest MakeRequest()
{
    CandidateReconciliationRequest request;
    request.requestId = QStringLiteral("reconcile-request-1");
    request.observedCandidate = MakeObserved(CandidateReconciliationState::Draft);
    request.reconciliationPolicyVersion =
        MakeReference(QStringLiteral("m21-reconciliation-policy"),
                      QStringLiteral("2026.08.31"));
    request.resolutionView = MakeResolutionView(
        request.observedCandidate, request.previousCandidate,
        request.reconciliationPolicyVersion);
    return request;
}

void SetPrevious(CandidateReconciliationRequest *request,
                 CandidateReconciliationState state)
{
    request->previousCandidate = MakePrevious(state);
    request->resolutionView = MakeResolutionView(
        request->observedCandidate, request->previousCandidate,
        request->reconciliationPolicyVersion);
}

bool HasIssue(const QList<CandidateReconciliationIssue> &issues,
              const QString &code, const QString &field_path)
{
    for (const CandidateReconciliationIssue &issue : issues) {
        if (issue.code == code && issue.fieldPath == field_path) {
            return true;
        }
    }
    return false;
}

bool SameReference(const VersionedCandidateReference &left,
                   const VersionedCandidateReference &right)
{
    return left.id == right.id && left.version == right.version;
}

bool SamePrevious(const PreviousCandidateSnapshot &left,
                  const PreviousCandidateSnapshot &right)
{
    return left.candidateId == right.candidateId
        && left.snapshotRevision == right.snapshotRevision
        && left.lifecycleState == right.lifecycleState
        && left.lastEditableState == right.lastEditableState
        && left.featureIdentity.geometryRef == right.featureIdentity.geometryRef
        && left.featureIdentity.geometryRevision
            == right.featureIdentity.geometryRevision
        && left.featureIdentity.layerSignature == right.featureIdentity.layerSignature
        && SameReference(left.provenance.ruleRef, right.provenance.ruleRef)
        && SameReference(left.provenance.planRef, right.provenance.planRef)
        && left.selectedPlanRef == right.selectedPlanRef;
}

bool TestDraftAndSelectedRetention()
{
    CandidateReconciliationRequest request = MakeRequest();
    CandidateReconciliationResult result =
        HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Draft
                    && result.manualEditDisposition
                        == CandidateManualEditDisposition::None
                    && result.candidate.has_value(),
                "M21-BB-001 should return a draft candidate without history")) {
        return false;
    }

    SetPrevious(&request, CandidateReconciliationState::Draft);
    result = HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Draft
                    && result.manualEditDisposition
                        == CandidateManualEditDisposition::None
                    && result.differences.isEmpty(),
                "M21-BB-002 should retain an identical draft without edits")) {
        return false;
    }

    SetPrevious(&request, CandidateReconciliationState::Selected);
    request.observedCandidate.provenance = request.previousCandidate->provenance;
    request.resolutionView = MakeResolutionView(
        request.observedCandidate, request.previousCandidate,
        request.reconciliationPolicyVersion);
    result = HoleCandidateReconciliationService::Reconcile(request);
    return Expect(result.state == CandidateReconciliationState::Selected
                      && result.candidate.has_value()
                      && result.candidate->selectedPlanRef
                          == request.previousCandidate->selectedPlanRef,
                  "M21-BB-003 should retain the exact selected plan");
}

bool TestIdentityAndProvenanceConflicts()
{
    CandidateReconciliationRequest request = MakeRequest();
    SetPrevious(&request, CandidateReconciliationState::Draft);
    request.observedCandidate.featureIdentity.geometryRevision =
        QStringLiteral("sha256:hole-b");
    request.resolutionView = MakeResolutionView(
        request.observedCandidate, request.previousCandidate,
        request.reconciliationPolicyVersion);
    CandidateReconciliationResult result =
        HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Conflict
                    && HasIssue(result.differences,
                                QStringLiteral("GEOMETRY_IDENTITY_CHANGED"),
                                QStringLiteral("/featureIdentity/geometryRevision")),
                "M21-BB-004 should block a geometry revision change")) {
        return false;
    }

    request = MakeRequest();
    SetPrevious(&request, CandidateReconciliationState::Draft);
    request.observedCandidate.featureIdentity.layerSignature =
        QStringLiteral("layers-v2");
    request.resolutionView = MakeResolutionView(
        request.observedCandidate, request.previousCandidate,
        request.reconciliationPolicyVersion);
    result = HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Conflict
                    && HasIssue(result.differences,
                                QStringLiteral("GEOMETRY_IDENTITY_CHANGED"),
                                QStringLiteral("/featureIdentity/layerSignature")),
                "M21-BB-005 should block a layer signature change")) {
        return false;
    }

    const auto expect_conflict = [](const QString &path) {
        CandidateReconciliationRequest case_request = MakeRequest();
        SetPrevious(&case_request, CandidateReconciliationState::Draft);
        if (path == QStringLiteral("ruleRef")) {
            case_request.observedCandidate.provenance->ruleRef.version = "2";
        } else if (path == QStringLiteral("planRef")) {
            case_request.observedCandidate.provenance->planRef.version = "2";
        } else if (path == QStringLiteral("templateRef")) {
            case_request.observedCandidate.provenance->templateRef.version = "2";
        } else if (path == QStringLiteral("materialRef")) {
            case_request.observedCandidate.provenance->materialRef.version = "2";
        } else if (path == QStringLiteral("machineProfileRef")) {
            case_request.observedCandidate.provenance->machineProfileRef.version = "2";
        } else {
            case_request.observedCandidate.provenance->toolCatalogRef.catalogVersion =
                QStringLiteral("2026.09");
        }
        case_request.resolutionView = MakeResolutionView(
            case_request.observedCandidate, case_request.previousCandidate,
            case_request.reconciliationPolicyVersion);
        const CandidateReconciliationResult case_result =
            HoleCandidateReconciliationService::Reconcile(case_request);
        return case_result.state == CandidateReconciliationState::Conflict
            && HasIssue(case_result.differences,
                        QStringLiteral("CANDIDATE_PROVENANCE_CHANGED"),
                        QStringLiteral("/provenance/") + path);
    };
    return Expect(expect_conflict(QStringLiteral("ruleRef")), "rule conflict")
        && Expect(expect_conflict(QStringLiteral("planRef")), "plan conflict")
        && Expect(expect_conflict(QStringLiteral("templateRef")), "template conflict")
        && Expect(expect_conflict(QStringLiteral("materialRef")), "material conflict")
        && Expect(expect_conflict(QStringLiteral("machineProfileRef")),
                  "machine conflict")
        && Expect(expect_conflict(QStringLiteral("toolCatalogRef")),
                  "tool catalog conflict");
}

bool TestResolutionPriorityAndValidation()
{
    CandidateReconciliationRequest request = MakeRequest();
    SetPrevious(&request, CandidateReconciliationState::Draft);
    request.observedCandidate.provenance->planRef.version = QStringLiteral("2");
    request.resolutionView = MakeResolutionView(
        request.observedCandidate, request.previousCandidate,
        request.reconciliationPolicyVersion);
    request.resolutionView.entries[10].status =
        CandidateReferenceResolutionState::Missing;
    CandidateReconciliationResult result =
        HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Stale
                    && result.candidate.has_value()
                    && result.candidate->provenance.planRef
                        == request.previousCandidate->provenance.planRef
                    && result.candidate->snapshotRevision
                        != request.previousCandidate->snapshotRevision,
                "M21-BB-012 should stale before conflict and copy old content")) {
        return false;
    }

    request = MakeRequest();
    SetPrevious(&request, CandidateReconciliationState::Draft);
    request.resolutionView.entries[0].status =
        CandidateReferenceResolutionState::Withdrawn;
    request.resolutionView.entries[10].status =
        CandidateReferenceResolutionState::Missing;
    result = HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Rejected
                    && HasIssue(result.reasons,
                                QStringLiteral("RECONCILIATION_POLICY_UNRESOLVED"),
                                QStringLiteral("/reconciliationPolicyVersion")),
                "M21-BB-028 policy resolution should win over old stale input")) {
        return false;
    }

    request = MakeRequest();
    SetPrevious(&request, CandidateReconciliationState::Draft);
    request.resolutionView.entries.removeAt(18);
    result = HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Rejected
                    && HasIssue(result.reasons,
                                QStringLiteral("RESOLUTION_VIEW_INVALID"),
                                QStringLiteral("/provenance/toolCatalogRef")),
                "M21-BB-039 should reject a missing previous resolution entry")) {
        return false;
    }

    request = MakeRequest();
    request.resolutionView.entries.append(request.resolutionView.entries.first());
    result = HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Rejected
                    && HasIssue(result.reasons,
                                QStringLiteral("RESOLUTION_VIEW_INVALID"),
                                QStringLiteral("/reconciliationPolicyVersion")),
                "M21-BB-040 should reject a duplicate resolution key")) {
        return false;
    }

    request = MakeRequest();
    request.resolutionView.entries[1].referenceIdentity =
        OpaqueIdentity(QStringLiteral("wrong-geometry"));
    result = HoleCandidateReconciliationService::Reconcile(request);
    return Expect(result.state == CandidateReconciliationState::Rejected
                      && HasIssue(result.reasons,
                                  QStringLiteral("RESOLUTION_VIEW_INVALID"),
                                  QStringLiteral("/featureIdentity/geometryRef")),
                  "a resolution entry must carry the exact referenced identity");
}

bool TestObservedPropagationAndStaleRecovery()
{
    CandidateReconciliationRequest request = MakeRequest();
    SetPrevious(&request, CandidateReconciliationState::Draft);
    request.observedCandidate = MakeObserved(CandidateReconciliationState::Conflict);
    request.resolutionView = MakeResolutionView(
        request.observedCandidate, request.previousCandidate,
        request.reconciliationPolicyVersion);
    CandidateReconciliationResult result =
        HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Conflict
                    && !result.candidate.has_value()
                    && result.manualEditDisposition
                        == CandidateManualEditDisposition::None,
                "M21-BB-037 should propagate an observed conflict before old resolution")) {
        return false;
    }

    request.observedCandidate = MakeObserved(CandidateReconciliationState::Rejected);
    request.resolutionView = MakeResolutionView(
        request.observedCandidate, request.previousCandidate,
        request.reconciliationPolicyVersion);
    result = HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Rejected
                    && !result.candidate.has_value(),
                "M21-BB-038 should propagate an observed rejection")) {
        return false;
    }

    request = MakeRequest();
    SetPrevious(&request, CandidateReconciliationState::Stale);
    request.previousCandidate->lastEditableState = CandidateReconciliationState::Selected;
    request.previousCandidate->selectedPlanRef = request.previousCandidate->provenance.planRef;
    request.resolutionView = MakeResolutionView(
        request.observedCandidate, request.previousCandidate,
        request.reconciliationPolicyVersion);
    result = HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Selected
                    && result.candidate.has_value()
                    && result.candidate->selectedPlanRef
                        == request.previousCandidate->provenance.planRef,
                "M21-BB-030 should recover exactly to selected")) {
        return false;
    }

    request.previousCandidate->lastEditableState = CandidateReconciliationState::Draft;
    request.previousCandidate->selectedPlanRef.reset();
    result = HoleCandidateReconciliationService::Reconcile(request);
    return Expect(result.state == CandidateReconciliationState::Draft
                      && result.candidate.has_value()
                      && !result.candidate->selectedPlanRef.has_value(),
                  "M21-BB-043 should recover exactly to draft");
}

bool TestRecoveryAndInputSafetyRejections()
{
    CandidateReconciliationRequest request = MakeRequest();
    request.observedCandidate.observedRevision.clear();
    CandidateReconciliationResult result =
        HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Rejected
                    && HasIssue(result.reasons,
                                QStringLiteral("CANDIDATE_SNAPSHOT_INVALID"),
                                QStringLiteral("/observedCandidate/observedRevision")),
                "an observed snapshot requires its independent revision")) {
        return false;
    }

    request = MakeRequest();
    SetPrevious(&request, CandidateReconciliationState::Stale);
    result = HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Rejected
                    && HasIssue(result.reasons,
                                QStringLiteral("CANDIDATE_SNAPSHOT_INVALID"),
                                QStringLiteral("/previousCandidate/lastEditableState")),
                "M21-BB-035 needs last editable state for stale recovery")) {
        return false;
    }

    request.previousCandidate->lastEditableState = CandidateReconciliationState::Selected;
    result = HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Rejected,
                "M21-BB-036 needs an exact selected plan reference")) {
        return false;
    }

    request = MakeRequest();
    SetPrevious(&request, CandidateReconciliationState::Conflict);
    request.observedCandidate.provenance->planRef.version = QStringLiteral("2");
    request.resolutionView = MakeResolutionView(
        request.observedCandidate, request.previousCandidate,
        request.reconciliationPolicyVersion);
    const CandidateReconciliationRequest before = request;
    result = HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Conflict
                    && result.candidate.has_value()
                    && result.candidate->snapshotRevision
                        != before.previousCandidate->snapshotRevision
                    && result.candidate->provenance.planRef
                        == before.previousCandidate->provenance.planRef
                    && result.explanation.observationRef
                        == before.observedCandidate.observedCandidateId
                    && SamePrevious(*request.previousCandidate,
                                    *before.previousCandidate),
                "M21-BB-041 keeps an old conflict snapshot immutable and revised")) {
        return false;
    }

    request = MakeRequest();
    SetPrevious(&request, CandidateReconciliationState::Draft);
    request.observedCandidate.provenance->planRef.version = QStringLiteral("2");
    request.resolutionView = MakeResolutionView(
        request.observedCandidate, request.previousCandidate,
        request.reconciliationPolicyVersion);
    request.resolutionView.entries[18].status =
        CandidateReferenceResolutionState::Withdrawn;
    result = HoleCandidateReconciliationService::Reconcile(request);
    return Expect(result.state == CandidateReconciliationState::Stale
                      && result.candidate.has_value()
                      && result.candidate->provenance.planRef
                          == request.previousCandidate->provenance.planRef
                      && result.candidate->snapshotRevision
                          != request.previousCandidate->snapshotRevision,
                  "M21-BB-042 must preserve the old snapshot when a catalog withdraws");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    if (!TestDraftAndSelectedRetention() || !TestIdentityAndProvenanceConflicts()
        || !TestResolutionPriorityAndValidation()
        || !TestObservedPropagationAndStaleRecovery()
        || !TestRecoveryAndInputSafetyRejections()) {
        return 1;
    }
    std::cout << "Hole candidate reconciliation service tests passed."
              << std::endl;
    return 0;
}
