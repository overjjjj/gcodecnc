#include "MillingBatchCommit.h"
#include "../core/MachiningCoordinates.h"

// 最终几何和参数整批验证成功后，才标记有效并一次提交。
MillingBatchCommitResult validateAndCommitMillingBatch(
    QList<MachiningOperation> operations, const StrategyBase &strategy, const ToolEntry &tool,
    const std::function<QStringList(const QList<MachiningOperation> &)> &commit,
    const Coordinate3D &modelOrigin)
{
    MillingBatchCommitResult result;
    if (operations.isEmpty()) {
        result.error = QStringLiteral("No confirmed milling operations.");
        return result;
    }
    for (const MachiningOperation &operation : operations) {
        if (operation.toolId != tool.id || operation.strategyId != strategy.id()) {
            result.error = QStringLiteral("Confirmed operation tool or strategy does not match the milling batch.");
            return result;
        }
        const auto feature=inWorkCoordinates(operation.contourFeature,modelOrigin);
        result.error=workHeightError(feature,operation.params);
        if(!result.error.isEmpty())return result;
        const ToolpathResult path = strategy.generate(feature, tool, operation.params);
        if (!path.ok || path.gcode.isEmpty()) {
            result.error = path.errorMsg.isEmpty() ? QStringLiteral("Milling failed.") : path.errorMsg;
            return result;
        }
    }
    for (MachiningOperation &operation : operations) operation.markToolpathValid();
    result.operationIds = commit(operations);
    result.ok = true;
    return result;
}
