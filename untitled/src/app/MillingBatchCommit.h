#pragma once

#include "../workspaces/process_design/operations/MachiningOperation.h"
#include "../strategies/StrategyBase.h"
#include <functional>

struct MillingBatchCommitResult
{
    bool ok = false;
    QString error;
    QStringList operationIds;
};

MillingBatchCommitResult validateAndCommitMillingBatch(
    QList<MachiningOperation> operations, const StrategyBase &strategy, const ToolEntry &tool,
    const std::function<QStringList(const QList<MachiningOperation> &)> &commit,
    const Coordinate3D &modelOrigin = {});
