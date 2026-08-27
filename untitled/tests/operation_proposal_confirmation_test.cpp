#include "../src/strategies/OperationProposal.h"
#include "../src/core/FeatureIdentity.h"

#include <QCoreApplication>
#include <type_traits>
#include <iostream>

static_assert(!std::is_convertible<OperationProposal, MachiningOperation>::value,
              "an unconfirmed proposal must not be executable");

static int expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << "\n";
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    HoleFeature hole;
    hole.kind = FeatureKind::Hole;
    hole.radius = 6.0;
    hole.depth = 20.0;
    hole.region = FaceRegion::Front;
    hole.faceIndices = {12};

    StrategyParams params;
    params.set(QStringLiteral("feedRate"), 320.0);
    params.set(QStringLiteral("stepDown"), 2.0);

    OperationProposal proposal;
    proposal.kind = OperationProposalKind::Hole;
    proposal.strategyId = QStringLiteral("hole_peck");
    proposal.toolId = 7;
    proposal.params = params;
    proposal.holeFeature = hole;

    const OperationConfirmationResult notConfirmed = confirmOperationProposal(
        proposal, OperationConfirmationIntent::NotConfirmed);
    if (expect(!notConfirmed.ok,
               "a proposal without explicit user confirmation must not become an operation")) {
        return 1;
    }

    const OperationConfirmationResult confirmed = confirmOperationProposal(
        proposal, OperationConfirmationIntent::ExplicitUser);
    if (expect(confirmed.ok, "a valid proposal should be confirmable") ||
        expect(!confirmed.operation.id.isEmpty(), "confirmation should assign an operation id") ||
        expect(confirmed.operation.opType == OperationType::Hole,
               "a hole proposal should become a hole operation") ||
        expect(confirmed.operation.stage == OperationStage::DeepHole,
               "peck drilling should be staged as deep-hole machining") ||
        expect(confirmed.operation.strategyId == proposal.strategyId &&
                   confirmed.operation.toolId == proposal.toolId,
               "strategy and tool should be copied into the confirmed operation") ||
        expect(confirmed.operation.holeFeature.faceIndices == hole.faceIndices &&
                   confirmed.operation.holeFeature.radius == hole.radius,
               "hole geometry should be copied into the confirmed operation") ||
        expect(confirmed.operation.params.get(QStringLiteral("feedRate")) == 320.0,
               "proposal parameters should be copied into the confirmed operation") ||
        expect(confirmed.operation.geometryRefs == QStringList{stableFeatureId(hole)},
               "confirmed holes should retain a stable geometry reference")) {
        return 1;
    }

    proposal.params.set(QStringLiteral("feedRate"), 999.0);
    if (expect(confirmed.operation.params.get(QStringLiteral("feedRate")) == 320.0,
               "editing a proposal after confirmation must not mutate the operation")) {
        return 1;
    }

    OperationProposal contourProposal;
    contourProposal.kind = OperationProposalKind::Contour;
    contourProposal.strategyId = QStringLiteral("mill_contour_finish");
    contourProposal.toolId = 3;
    contourProposal.contourFeature.radius = 20.0;
    contourProposal.contourFeature.depth = 4.0;
    contourProposal.contourFeature.region = FaceRegion::Front;
    const OperationConfirmationResult contourConfirmed =
        confirmOperationProposal(contourProposal,
                                 OperationConfirmationIntent::ExplicitUser);
    if (expect(contourConfirmed.ok, "a valid contour proposal should be confirmable") ||
        expect(contourConfirmed.operation.opType == OperationType::Finish,
               "a contour finish proposal should become a finish operation") ||
        expect(contourConfirmed.operation.stage == OperationStage::FinishCut,
               "a contour finish proposal should use the finish stage") ||
        expect(contourConfirmed.operation.geometryRefs ==
                   QStringList{stableContourId(contourProposal.contourFeature)},
               "confirmed contours should retain a stable geometry reference")) {
        return 1;
    }

    OperationProposal pocketProposal;
    pocketProposal.kind = OperationProposalKind::Contour;
    pocketProposal.strategyId = QStringLiteral("mill_pocket_rough");
    pocketProposal.toolId = 3;
    pocketProposal.contourFeature.subType = QStringLiteral("rectangular_pocket");
    pocketProposal.contourFeature.length = 40.0;
    pocketProposal.contourFeature.width = 20.0;
    pocketProposal.contourFeature.depth = 4.0;
    if (expect(!confirmOperationProposal(
                    pocketProposal, OperationConfirmationIntent::ExplicitUser).ok,
               "pocket roughing must require an explicit entry-method choice")) {
        return 1;
    }
    pocketProposal.params.set(QStringLiteral("entryMode"), 0.0);
    const OperationConfirmationResult pocketConfirmed = confirmOperationProposal(
        pocketProposal, OperationConfirmationIntent::ExplicitUser);
    if (expect(pocketConfirmed.ok,
               "pocket roughing with an explicit entry method should be confirmable") ||
        expect(pocketConfirmed.operation.params.get(QStringLiteral("entryMode"), -1.0) == 0.0,
               "confirmed pocket operation must retain the operator entry choice")) {
        return 1;
    }

    OperationProposal pocketFinishProposal = pocketProposal;
    pocketFinishProposal.strategyId = QStringLiteral("mill_pocket_finish");
    pocketFinishProposal.params.values.remove(QStringLiteral("entryMode"));
    const OperationConfirmationResult pocketFinishConfirmed = confirmOperationProposal(
        pocketFinishProposal, OperationConfirmationIntent::ExplicitUser);
    if (expect(pocketFinishConfirmed.ok,
               "pocket-wall finish must be confirmable without a roughing entry choice") ||
        expect(pocketFinishConfirmed.operation.opType == OperationType::Finish &&
                   pocketFinishConfirmed.operation.stage == OperationStage::FinishCut,
               "pocket-wall finish should become a finish-cut operation")) {
        return 1;
    }

    OperationProposal pocketFloorFinishProposal = pocketFinishProposal;
    pocketFloorFinishProposal.strategyId = QStringLiteral("mill_pocket_floor_finish");
    const OperationConfirmationResult pocketFloorFinishConfirmed = confirmOperationProposal(
        pocketFloorFinishProposal, OperationConfirmationIntent::ExplicitUser);
    if (expect(pocketFloorFinishConfirmed.ok,
               "pocket-floor finish must be confirmable without a roughing entry choice") ||
        expect(pocketFloorFinishConfirmed.operation.opType == OperationType::Finish &&
                   pocketFloorFinishConfirmed.operation.stage == OperationStage::FinishCut,
               "pocket-floor finish should become a finish-cut operation")) {
        return 1;
    }

    OperationProposal missingStrategy = proposal;
    missingStrategy.strategyId.clear();
    if (expect(!confirmOperationProposal(
                    missingStrategy, OperationConfirmationIntent::ExplicitUser).ok,
               "a proposal without a strategy must not become an operation")) {
        return 1;
    }

    OperationProposal missingTool = proposal;
    missingTool.toolId = -1;
    if (expect(!confirmOperationProposal(
                    missingTool, OperationConfirmationIntent::ExplicitUser).ok,
               "a proposal without a valid tool must not become an operation")) {
        return 1;
    }

    OperationProposal invalidGeometry = proposal;
    invalidGeometry.holeFeature.radius = 0.0;
    if (expect(!confirmOperationProposal(
                    invalidGeometry, OperationConfirmationIntent::ExplicitUser).ok,
               "a proposal with invalid geometry must not become an operation")) {
        return 1;
    }

    return 0;
}
