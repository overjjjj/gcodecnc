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

// 可编辑的工艺建议；只有显式人工确认成功后，才产生带工序 ID 的 MachiningOperation。
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

// 调用方提供当前项目上下文，用于核对选择证据是否仍属于当前模型和 Setup。
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
    // 只完成基本几何与确认准入检查；成功结果的刀路仍需由策略生成并验证。
    static OperationConfirmationResult confirm(
        const OperationProposal &proposal,
        OperationConfirmationIntent confirmationIntent,
        const OperationConfirmationContext &context = {});
};

OperationConfirmationResult confirmOperationProposal(
    const OperationProposal &proposal,
    OperationConfirmationIntent confirmationIntent,
    const OperationConfirmationContext &context = {});
