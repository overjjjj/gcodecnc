#include "OperationProposal.h"

#include <QUuid>
#include <cmath>

namespace {

OperationType operationTypeForProposal(const OperationProposal &proposal)
{
    if (proposal.kind == OperationProposalKind::Hole) {
        return OperationType::Hole;
    }
    if (proposal.strategyId == QStringLiteral("mill_pocket_rough")) {
        return OperationType::Roughing;
    }
    if (proposal.strategyId == QStringLiteral("mill_contour_finish") ||
        proposal.strategyId == QStringLiteral("mill_pocket_finish") ||
        proposal.strategyId == QStringLiteral("mill_pocket_floor_finish") ||
        proposal.strategyId == QStringLiteral("mill_surface_finish") ||
        proposal.strategyId == QStringLiteral("mill_closed_contour") ||
        proposal.strategyId == QStringLiteral("mill_open_contour")) {
        return OperationType::Finish;
    }
    return OperationType::Contour;
}

OperationStage operationStageForProposal(const OperationProposal &proposal,
                                          OperationType operationType)
{
    if (proposal.strategyId == QStringLiteral("hole_spot")) {
        return OperationStage::Setup;
    }
    if (proposal.strategyId == QStringLiteral("mill_pocket_rough") ||
        proposal.strategyId == QStringLiteral("mill_slot") ||
        proposal.strategyId == QStringLiteral("mill_blind_slot") ||
        proposal.strategyId == QStringLiteral("mill_tapered_slot")) {
        return OperationStage::RoughCut;
    }
    if (proposal.strategyId == QStringLiteral("hole_deephole") ||
        proposal.strategyId == QStringLiteral("hole_peck")) {
        return OperationStage::DeepHole;
    }
    if (proposal.strategyId == QStringLiteral("hole_chamfer")) {
        return OperationStage::Cleanup;
    }
    if (operationType == OperationType::Finish ||
        proposal.strategyId == QStringLiteral("hole_reaming") ||
        proposal.strategyId == QStringLiteral("hole_circular_mill")) {
        return OperationStage::FinishCut;
    }
    return OperationStage::RoughCut;
}

bool hasUsableContourGeometry(const ContourFeature &feature)
{
    return feature.radius > 0.0 ||
           (feature.length > 0.0 && feature.width > 0.0) ||
           !feature.points.isEmpty();
}

} // namespace

FaceRegion OperationProposal::featureRegion() const
{
    return kind == OperationProposalKind::Hole
        ? holeFeature.region
        : contourFeature.region;
}

OperationConfirmationResult confirmOperationProposal(
    const OperationProposal &proposal,
    OperationConfirmationIntent confirmationIntent)
{
    OperationConfirmationResult result;
    if (confirmationIntent != OperationConfirmationIntent::ExplicitUser) {
        result.error = QStringLiteral("Explicit user confirmation is required.");
        return result;
    }
    if (proposal.strategyId.trimmed().isEmpty()) {
        result.error = QStringLiteral("A machining strategy is required.");
        return result;
    }
    if (proposal.toolId <= 0) {
        result.error = QStringLiteral("A valid tool is required.");
        return result;
    }
    if (proposal.kind == OperationProposalKind::Hole &&
        (proposal.holeFeature.radius <= 0.0 || proposal.holeFeature.depth <= 0.0)) {
        result.error = QStringLiteral("Hole geometry is invalid.");
        return result;
    }
    if (proposal.kind == OperationProposalKind::Contour &&
        !hasUsableContourGeometry(proposal.contourFeature)) {
        result.error = QStringLiteral("Contour geometry is invalid.");
        return result;
    }
    if (proposal.strategyId == QStringLiteral("mill_pocket_rough")) {
        if (!proposal.params.values.contains(QStringLiteral("entryMode"))) {
            result.error = QStringLiteral("Pocket entry method requires explicit operator selection.");
            return result;
        }
        const double entryMode = proposal.params.get(QStringLiteral("entryMode"), -1.0);
        if (std::abs(entryMode) > 1.0e-6 &&
            std::abs(entryMode - 1.0) > 1.0e-6) {
            result.error = QStringLiteral("Pocket entry method is invalid.");
            return result;
        }
    }

    MachiningOperation operation;
    operation.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    operation.opType = operationTypeForProposal(proposal);
    operation.stage = operationStageForProposal(proposal, operation.opType);
    operation.strategyId = proposal.strategyId;
    operation.toolId = proposal.toolId;
    operation.params = proposal.params;
    if (proposal.kind == OperationProposalKind::Hole) {
        operation.holeFeature = proposal.holeFeature;
    } else {
        operation.contourFeature = proposal.contourFeature;
    }

    result.ok = true;
    result.operation = operation;
    return result;
}
