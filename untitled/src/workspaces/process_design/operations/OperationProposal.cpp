#include "MachiningInput.h"
#include "OperationProposal.h"
#include "../../../core/SketchSelectionBinding.h"

#include "../selection/FeatureIdentity.h"
#include "../selection/PocketOpening.h"
#include "../../../strategies/mill/ContourSideGeometry.h"
#include "../../../strategies/hole/HoleStrategyUtils.h"
#include "../../../tool/ToolLibrary.h"

#include <QUuid>
#include <cmath>

namespace {

// 函数说明：operationTypeForProposal负责当前模块的核心处理。
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

// 函数说明：operationStageForProposal负责当前模块的核心处理。
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
        proposal.strategyId == QStringLiteral("hole_g73") ||
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

// 函数说明：hasUsableContourGeometry负责当前模块的核心处理。
bool hasUsableContourGeometry(const ContourFeature &feature)
{
    return feature.radius > 0.0 ||
           (feature.length > 0.0 && feature.width > 0.0) ||
           !feature.points.isEmpty();
}


} // namespace

// 函数说明：featureRegion负责当前模块的核心处理。
FaceRegion OperationProposal::featureRegion() const
{
    return kind == OperationProposalKind::Hole
        ? holeFeature.region
        : contourFeature.region;
}

// 函数说明：confirmOperationProposal负责当前模块的核心处理。
OperationConfirmationResult confirmOperationProposal(
    const OperationProposal &proposal,
    OperationConfirmationIntent confirmationIntent,
    const OperationConfirmationContext &context)
{
    return OperationFactory::confirm(proposal, confirmationIntent, context);
}

// 函数说明：confirm负责当前模块的核心处理。
OperationConfirmationResult OperationFactory::confirm(
    const OperationProposal &input,
    OperationConfirmationIntent confirmationIntent,
    const OperationConfirmationContext &context)
{
    OperationProposal proposal = input;
    if (proposal.kind == OperationProposalKind::Contour)
        proposal.contourFeature = applyMachiningParameters(proposal.contourFeature,proposal.params);
    OperationConfirmationResult result;
    if (context.sketchDocument) {
        if (proposal.kind!=OperationProposalKind::Contour || proposal.strategyId!="mill_closed_contour" ||
            context.sourceFingerprint!=context.sketchDocument->fingerprint()) {
            result.error=QStringLiteral("草图仅支持当前完整轮廓的外形铣。");
            return result;
        }
        const auto binding=bindSketchSelection(*context.sketchDocument,proposal.selectionEvidence,
                                               proposal.params,context.setupFingerprint,context.coordinateSystemId);
        if(!binding.error.isEmpty()){result.error=binding.error;return result;}
        proposal.contourFeature.precisePoints=binding.points;
        proposal.contourFeature.center.setZ(context.sketchDocument->planeZ);
    }
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
    const QString spindleError = holeSpindleOutputError(proposal.strategyId, proposal.params);
    if (!spindleError.isEmpty()) { result.error = spindleError; return result; }
    if (proposal.kind == OperationProposalKind::Hole &&
        (proposal.holeFeature.radius <= 0.0 || proposal.holeFeature.depth <= 0.0)) {
        result.error = QStringLiteral("Hole geometry is invalid.");
        return result;
    }
    if (proposal.kind == OperationProposalKind::Hole) {
        result.error = holeAxisOutputError(proposal.holeFeature,
            holeCuttingDepth(proposal.strategyId, proposal.holeFeature, proposal.params,
                             ToolLibrary::instance().tool(proposal.toolId)));
        if (!result.error.isEmpty()) return result;
    }
    if (proposal.kind == OperationProposalKind::Hole && proposal.strategyId == "hole_tapping") {
        result.error = validateTapForHole(proposal.holeFeature, ToolLibrary::instance().tool(proposal.toolId));
        if (!result.error.isEmpty()) return result;
    }
    if (proposal.kind == OperationProposalKind::Contour &&
        !hasUsableContourGeometry(proposal.contourFeature)) {
        result.error = QStringLiteral("Contour geometry is invalid.");
        return result;
    }
    if (proposal.kind == OperationProposalKind::Contour &&
        (proposal.selectionEvidence.explicitUserSelection ||
         proposal.contourFeature.region == FaceRegion::Side)) {
        const SetupAccessResult access = evaluateSelectionAccess(
            proposal.selectionEvidence,
            context.sourceFingerprint,
            context.setupFingerprint,
            context.coordinateSystemId,
            machiningRequirements(proposal.strategyId).closedBoundary);
        result.accessStatus = access.status;
        if (!access.ok()) {
            result.error = access.issueCode;
            return result;
        }
    }
    if (proposal.kind == OperationProposalKind::Contour) {
        const QString sideError = contourSideOperationError(proposal.selectionEvidence,
            proposal.contourFeature,proposal.params,proposal.strategyId);
        if (!sideError.isEmpty()) { result.error = sideError; return result; }
        const QString openingError = pocketOpeningOperationError(proposal.selectionEvidence,
            proposal.contourFeature,proposal.params,proposal.strategyId,context.sourceMesh);
        if (!openingError.isEmpty()) { result.error = openingError; return result; }
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
    operation.selectionEvidence = proposal.selectionEvidence;
    if (proposal.kind == OperationProposalKind::Hole) {
        operation.holeFeature = proposal.holeFeature;
        operation.geometryRefs = QStringList{stableFeatureId(proposal.holeFeature)};
    } else {
        operation.contourFeature = proposal.contourFeature;
        operation.geometryRefs = QStringList{stableContourId(proposal.contourFeature)};
    }

    result.ok = true;
    result.accessStatus = SetupAccessStatus::Ready;
    result.operation = operation;
    return result;
}
