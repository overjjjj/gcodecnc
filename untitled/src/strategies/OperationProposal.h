#pragma once

#include "MachiningOperation.h"

enum class OperationProposalKind {
    Hole,
    Contour
};

enum class OperationConfirmationIntent {
    NotConfirmed,
    ExplicitUser
};

struct OperationProposal {
    OperationProposalKind kind = OperationProposalKind::Hole;
    QString strategyId;
    int toolId = -1;
    StrategyParams params;
    HoleFeature holeFeature;
    ContourFeature contourFeature;
    SelectionChain selectionChain;

    FaceRegion featureRegion() const;
};

struct OperationConfirmationResult {
    bool ok = false;
    QString error;
    MachiningOperation operation;
};

OperationConfirmationResult confirmOperationProposal(
    const OperationProposal &proposal,
    OperationConfirmationIntent confirmationIntent);
