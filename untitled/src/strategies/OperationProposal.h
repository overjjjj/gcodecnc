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
    SelectionChain selectionEvidence;

    FaceRegion featureRegion() const;
};

struct OperationConfirmationContext {
    QString sourceFingerprint;
    QString setupFingerprint;
    QString coordinateSystemId = QStringLiteral("G54");
};

struct OperationConfirmationResult {
    bool ok = false;
    QString error;
    SetupAccessStatus accessStatus = SetupAccessStatus::InsufficientTopology;
    MachiningOperation operation;
};

class OperationFactory
{
public:
    static OperationConfirmationResult confirm(
        const OperationProposal &proposal,
        OperationConfirmationIntent confirmationIntent,
        const OperationConfirmationContext &context = {});
};

OperationConfirmationResult confirmOperationProposal(
    const OperationProposal &proposal,
    OperationConfirmationIntent confirmationIntent,
    const OperationConfirmationContext &context = {});
