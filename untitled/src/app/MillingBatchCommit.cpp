#include "MillingBatchCommit.h"

// 最终几何和参数整批验证成功后，才标记有效并一次提交。
MillingBatchCommitResult validateAndCommitMillingBatch(
    QList<MachiningOperation> operations, const StrategyBase &strategy, const ToolEntry &tool,
    const std::function<QStringList(const QList<MachiningOperation> &)> &commit)
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
        // 使用确认后最终保存的几何和参数复算，不能把编辑前的预览结果当作验证依据。
        const ToolpathResult path = strategy.generate(operation.contourFeature, tool, operation.params);
        if (!path.ok || path.gcode.isEmpty()) {
            result.error = path.errorMsg.isEmpty() ? QStringLiteral("Milling failed.") : path.errorMsg;
            return result;
        }
    }
    for (MachiningOperation &operation : operations) operation.markToolpathValid();
    // 只在整批验证通过后调用一次；实际写入的原子性由调用方的 commit 回调保证。
    result.operationIds = commit(operations);
    if (result.operationIds.size() != operations.size() || result.operationIds.contains(QString())) {
        result.error = QStringLiteral("The milling batch was not fully committed.");
        return result;
    }
    result.ok = true;
    return result;
}
