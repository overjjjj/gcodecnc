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
    VersionedCandidateReference reference;
    reference.id = id;
    reference.version = version;
    return reference;
}

VersionedToolCatalogReference MakeToolCatalogReference(
    const QString &id, const QString &catalog_id, const QString &catalog_version)
{
    VersionedToolCatalogReference reference;
    reference.id = id;
    reference.catalogId = catalog_id;
    reference.catalogVersion = catalog_version;
    return reference;
}

CandidateSnapshot MakeSnapshot(CandidateReconciliationState state)
{
    CandidateSnapshot snapshot;
    snapshot.candidateId = QStringLiteral("candidate-42");
    snapshot.snapshotRevision = QStringLiteral("snapshot-1");
    snapshot.lifecycleState = state;
    snapshot.featureIdentity.geometryRef = QStringLiteral("face:42/hole:7");
    snapshot.featureIdentity.geometryRevision = QStringLiteral("sha256:hole-a");
    snapshot.featureIdentity.layerSignature = QStringLiteral("layers-v1");
    snapshot.provenance.ruleRef = MakeReference(QStringLiteral("rule-a"),
                                                 QStringLiteral("1"));
    snapshot.provenance.planRef = MakeReference(QStringLiteral("plan-a"),
                                                 QStringLiteral("1"));
    snapshot.provenance.templateRef = MakeReference(QStringLiteral("template-a"),
                                                     QStringLiteral("1"));
    snapshot.provenance.materialRef = MakeReference(QStringLiteral("aluminum-6061"),
                                                     QStringLiteral("2026.08"));
    snapshot.provenance.machineProfileRef =
        MakeReference(QStringLiteral("front-z-3axis"), QStringLiteral("2026.08"));
    snapshot.provenance.toolCatalogRef = MakeToolCatalogReference(
        QStringLiteral("tool-catalog"), QStringLiteral("tools"),
        QStringLiteral("2026.08"));
    snapshot.selectedPlanRef = snapshot.provenance.planRef;
    return snapshot;
}

CandidateReconciliationResolutionView MakeResolvableView()
{
    CandidateReconciliationResolutionView view;
    view.entries = {
        {QStringLiteral("previousCandidate.featureIdentity.geometryRef"),
         CandidateReferenceResolutionState::Resolvable},
        {QStringLiteral("previousCandidate.provenance.ruleRef"),
         CandidateReferenceResolutionState::Resolvable},
        {QStringLiteral("previousCandidate.provenance.planRef"),
         CandidateReferenceResolutionState::Resolvable},
        {QStringLiteral("previousCandidate.provenance.templateRef"),
         CandidateReferenceResolutionState::Resolvable},
        {QStringLiteral("previousCandidate.provenance.materialRef"),
         CandidateReferenceResolutionState::Resolvable},
        {QStringLiteral("previousCandidate.provenance.machineProfileRef"),
         CandidateReferenceResolutionState::Resolvable},
        {QStringLiteral("previousCandidate.provenance.toolCatalogRef"),
         CandidateReferenceResolutionState::Resolvable},
        {QStringLiteral("observedCandidate.featureIdentity.geometryRef"),
         CandidateReferenceResolutionState::Resolvable},
        {QStringLiteral("observedCandidate.provenance.ruleRef"),
         CandidateReferenceResolutionState::Resolvable},
        {QStringLiteral("observedCandidate.provenance.planRef"),
         CandidateReferenceResolutionState::Resolvable},
        {QStringLiteral("observedCandidate.provenance.templateRef"),
         CandidateReferenceResolutionState::Resolvable},
        {QStringLiteral("observedCandidate.provenance.materialRef"),
         CandidateReferenceResolutionState::Resolvable},
        {QStringLiteral("observedCandidate.provenance.machineProfileRef"),
         CandidateReferenceResolutionState::Resolvable},
        {QStringLiteral("observedCandidate.provenance.toolCatalogRef"),
         CandidateReferenceResolutionState::Resolvable},
        {QStringLiteral("reconciliationPolicyVersion"),
         CandidateReferenceResolutionState::Resolvable}
    };
    return view;
}

CandidateReconciliationRequest MakeRequest()
{
    CandidateReconciliationRequest request;
    request.requestId = QStringLiteral("reconcile-request-1");
    request.observedCandidate = MakeSnapshot(CandidateReconciliationState::Draft);
    request.resolutionView = MakeResolvableView();
    request.reconciliationPolicyVersion =
        MakeReference(QStringLiteral("m21-reconciliation-policy"),
                      QStringLiteral("2026.08.31"));
    return request;
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

bool SameToolCatalogReference(const VersionedToolCatalogReference &left,
                              const VersionedToolCatalogReference &right)
{
    return left.id == right.id && left.catalogId == right.catalogId
        && left.catalogVersion == right.catalogVersion;
}

bool SameSnapshot(const CandidateSnapshot &left, const CandidateSnapshot &right)
{
    return left.candidateId == right.candidateId
        && left.snapshotRevision == right.snapshotRevision
        && left.lifecycleState == right.lifecycleState
        && left.featureIdentity.geometryRef == right.featureIdentity.geometryRef
        && left.featureIdentity.geometryRevision
            == right.featureIdentity.geometryRevision
        && left.featureIdentity.layerSignature == right.featureIdentity.layerSignature
        && SameReference(left.provenance.ruleRef, right.provenance.ruleRef)
        && SameReference(left.provenance.planRef, right.provenance.planRef)
        && SameReference(left.provenance.templateRef, right.provenance.templateRef)
        && SameReference(left.provenance.materialRef, right.provenance.materialRef)
        && SameReference(left.provenance.machineProfileRef,
                         right.provenance.machineProfileRef)
        && SameToolCatalogReference(left.provenance.toolCatalogRef,
                                    right.provenance.toolCatalogRef)
        && SameReference(left.selectedPlanRef, right.selectedPlanRef);
}

bool TestNoPreviousCandidateReturnsDraftWithoutOperationSurface()
{
    const CandidateReconciliationRequest request = MakeRequest();
    const CandidateReconciliationResult result =
        HoleCandidateReconciliationService::Reconcile(request);
    return Expect(result.state == CandidateReconciliationState::Draft,
                  "a valid observed candidate without history should be a draft")
        && Expect(result.manualEditDisposition
                      == CandidateManualEditDisposition::None,
                  "a candidate without history should not claim preserved edits")
        && Expect(result.candidate.candidateId == request.observedCandidate.candidateId,
                  "the result should expose a candidate snapshot, not a formal operation")
        && Expect(result.reasons.isEmpty() && result.differences.isEmpty(),
                  "a valid new candidate should not report a rejection or difference");
}

bool TestIdenticalDraftWithoutOverridesStaysDraft()
{
    CandidateReconciliationRequest request = MakeRequest();
    request.previousCandidate = MakeSnapshot(CandidateReconciliationState::Draft);
    const CandidateReconciliationResult result =
        HoleCandidateReconciliationService::Reconcile(request);
    return Expect(result.state == CandidateReconciliationState::Draft,
                  "an identical draft should remain a draft")
        && Expect(result.manualEditDisposition
                      == CandidateManualEditDisposition::None,
                  "an identical draft without overrides should keep None")
        && Expect(result.differences.isEmpty(),
                  "an identical draft should have no differences");
}

bool TestIdenticalSelectedKeepsSelectedPlan()
{
    CandidateReconciliationRequest request = MakeRequest();
    request.previousCandidate = MakeSnapshot(CandidateReconciliationState::Selected);
    request.observedCandidate = *request.previousCandidate;
    const CandidateReconciliationResult result =
        HoleCandidateReconciliationService::Reconcile(request);
    return Expect(result.state == CandidateReconciliationState::Selected,
                  "an identical selected candidate should remain selected")
        && Expect(SameReference(result.candidate.selectedPlanRef,
                                request.previousCandidate->selectedPlanRef),
                  "an identical selected candidate should retain its selected plan")
        && Expect(result.manualEditDisposition
                      == CandidateManualEditDisposition::None,
                  "a selected candidate without overrides should keep None");
}

bool TestGeometryDifferencesBlockAsConflict()
{
    CandidateReconciliationRequest request = MakeRequest();
    request.previousCandidate = MakeSnapshot(CandidateReconciliationState::Draft);
    request.observedCandidate.featureIdentity.geometryRevision =
        QStringLiteral("sha256:hole-b");
    CandidateReconciliationResult result =
        HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Conflict
                    && HasIssue(result.differences,
                                QStringLiteral("GEOMETRY_IDENTITY_CHANGED"),
                                QStringLiteral("featureIdentity.geometryRevision")),
                "a geometry revision difference should be a conflict")) {
        return false;
    }

    request = MakeRequest();
    request.previousCandidate = MakeSnapshot(CandidateReconciliationState::Draft);
    request.observedCandidate.featureIdentity.layerSignature =
        QStringLiteral("layers-v2");
    result = HoleCandidateReconciliationService::Reconcile(request);
    return Expect(result.state == CandidateReconciliationState::Conflict
                      && HasIssue(result.differences,
                                  QStringLiteral("GEOMETRY_IDENTITY_CHANGED"),
                                  QStringLiteral("featureIdentity.layerSignature")),
                  "a layer signature difference should be a conflict");
}

bool TestResolvableProvenanceDifferencesBlockAsConflict()
{
    const auto expect_conflict = [](const QString &field_path,
                                    const QString &replacement_version) {
        CandidateReconciliationRequest request = MakeRequest();
        request.previousCandidate = MakeSnapshot(CandidateReconciliationState::Draft);
        if (field_path == QStringLiteral("ruleRef")) {
            request.observedCandidate.provenance.ruleRef.version = replacement_version;
        } else if (field_path == QStringLiteral("planRef")) {
            request.observedCandidate.provenance.planRef.version = replacement_version;
        } else if (field_path == QStringLiteral("templateRef")) {
            request.observedCandidate.provenance.templateRef.version = replacement_version;
        } else if (field_path == QStringLiteral("materialRef")) {
            request.observedCandidate.provenance.materialRef.version = replacement_version;
        } else if (field_path == QStringLiteral("machineProfileRef")) {
            request.observedCandidate.provenance.machineProfileRef.version =
                replacement_version;
        } else if (field_path == QStringLiteral("toolCatalogRef")) {
            request.observedCandidate.provenance.toolCatalogRef.catalogVersion =
                replacement_version;
        }
        const CandidateReconciliationResult result =
            HoleCandidateReconciliationService::Reconcile(request);
        return result.state == CandidateReconciliationState::Conflict
            && HasIssue(result.differences,
                        QStringLiteral("CANDIDATE_PROVENANCE_CHANGED"),
                        QStringLiteral("provenance.") + field_path);
    };

    return Expect(expect_conflict(QStringLiteral("ruleRef"), QStringLiteral("2")),
                  "a resolvable rule version difference should be a conflict")
        && Expect(expect_conflict(QStringLiteral("planRef"), QStringLiteral("2")),
                  "a resolvable plan version difference should be a conflict")
        && Expect(expect_conflict(QStringLiteral("templateRef"), QStringLiteral("2")),
                  "a resolvable template version difference should be a conflict")
        && Expect(expect_conflict(QStringLiteral("materialRef"), QStringLiteral("2")),
                  "a resolvable material version difference should be a conflict")
        && Expect(expect_conflict(QStringLiteral("machineProfileRef"),
                                  QStringLiteral("2")),
                  "a resolvable machine version difference should be a conflict")
        && Expect(expect_conflict(QStringLiteral("toolCatalogRef"),
                                  QStringLiteral("2026.09")),
                  "a resolvable tool catalog version difference should be a conflict");
}

bool TestUnresolvedOldReferenceWinsOverNewVersion()
{
    CandidateReconciliationRequest request = MakeRequest();
    request.previousCandidate = MakeSnapshot(CandidateReconciliationState::Draft);
    request.observedCandidate.provenance.ruleRef.version = QStringLiteral("2");
    request.resolutionView.entries[1].state =
        CandidateReferenceResolutionState::Missing;
    CandidateReconciliationResult result =
        HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Stale
                    && HasIssue(result.differences,
                                QStringLiteral("CANDIDATE_REFERENCE_UNRESOLVED"),
                                QStringLiteral("previousCandidate.provenance.ruleRef")),
                "a missing old rule should be stale before a newer rule conflict")) {
        return false;
    }

    request.resolutionView.entries[1].state =
        CandidateReferenceResolutionState::Withdrawn;
    result = HoleCandidateReconciliationService::Reconcile(request);
    return Expect(result.state == CandidateReconciliationState::Stale,
                  "a withdrawn old rule should remain stale and not use a new version");
}

bool TestInvalidSnapshotAndPolicyAreRejected()
{
    CandidateReconciliationRequest request = MakeRequest();
    request.observedCandidate.snapshotRevision.clear();
    CandidateReconciliationResult result =
        HoleCandidateReconciliationService::Reconcile(request);
    if (!Expect(result.state == CandidateReconciliationState::Rejected
                    && HasIssue(result.reasons,
                                QStringLiteral("CANDIDATE_SNAPSHOT_INVALID"),
                                QStringLiteral("observedCandidate.snapshotRevision")),
                "a missing observed snapshot revision should be rejected")) {
        return false;
    }

    request = MakeRequest();
    request.reconciliationPolicyVersion.version.clear();
    result = HoleCandidateReconciliationService::Reconcile(request);
    return Expect(result.state == CandidateReconciliationState::Rejected
                      && HasIssue(result.reasons,
                                  QStringLiteral("RECONCILIATION_POLICY_UNRESOLVED"),
                                  QStringLiteral("reconciliationPolicyVersion")),
                  "a missing reconciliation policy should be rejected");
}

bool TestConflictDoesNotRecoverWithoutAuthorization()
{
    CandidateReconciliationRequest request = MakeRequest();
    request.previousCandidate = MakeSnapshot(CandidateReconciliationState::Conflict);
    request.observedCandidate = *request.previousCandidate;
    const CandidateReconciliationResult result =
        HoleCandidateReconciliationService::Reconcile(request);
    return Expect(result.state == CandidateReconciliationState::Conflict,
                  "a conflict should not recover without an approved reprocessing command")
        && Expect(result.candidate.candidateId
                      == request.previousCandidate->candidateId,
                  "an unresolved conflict should retain the auditable candidate");
}

bool TestReconcileDoesNotMutateInput()
{
    CandidateReconciliationRequest request = MakeRequest();
    request.previousCandidate = MakeSnapshot(CandidateReconciliationState::Draft);
    const CandidateReconciliationRequest before = request;
    const CandidateReconciliationResult result =
        HoleCandidateReconciliationService::Reconcile(request);
    return Expect(result.state == CandidateReconciliationState::Draft,
                  "the baseline request should reconcile to a draft")
        && Expect(SameSnapshot(request.observedCandidate, before.observedCandidate)
                      && request.previousCandidate.has_value()
                      && before.previousCandidate.has_value()
                      && SameSnapshot(*request.previousCandidate,
                                      *before.previousCandidate)
                      && SameReference(request.reconciliationPolicyVersion,
                                       before.reconciliationPolicyVersion)
                      && request.resolutionView.entries.size()
                          == before.resolutionView.entries.size(),
                  "reconciliation should not mutate request snapshots or policy input");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    if (!TestNoPreviousCandidateReturnsDraftWithoutOperationSurface()
        || !TestIdenticalDraftWithoutOverridesStaysDraft()
        || !TestIdenticalSelectedKeepsSelectedPlan()
        || !TestGeometryDifferencesBlockAsConflict()
        || !TestResolvableProvenanceDifferencesBlockAsConflict()
        || !TestUnresolvedOldReferenceWinsOverNewVersion()
        || !TestInvalidSnapshotAndPolicyAreRejected()
        || !TestConflictDoesNotRecoverWithoutAuthorization()
        || !TestReconcileDoesNotMutateInput()) {
        return 1;
    }

    std::cout << "Hole candidate reconciliation service tests passed."
              << std::endl;
    return 0;
}
