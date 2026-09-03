#pragma once

#include "../core/MachineProfile.h"
#include "../core/ProjectManager.h"
#include "../postprocessor/PostProcessorBase.h"
#include "../strategies/MachiningOperation.h"

#include <QList>
#include <QStringList>

#include <functional>
#include <memory>

class StrategyBase;

struct ProgramGenerationResult {
    bool ok = false;
    ProgramEntry snapshot;
    QStringList errors;
};

struct ProgramGenerationSnapshotOptions {
    QString name;
    QString sourceSummary;
    QString mainProgramName;
    MachineProfile machineProfile;
    QString sourceFingerprint;
    QString setupFingerprint;
};

class ProgramGenerationService
{
public:
    using StrategyLookup =
        std::function<std::shared_ptr<StrategyBase>(const QString &strategyId)>;
    using ToolLookup = std::function<ToolEntry(int toolId)>;

    ProgramGenerationService(StrategyLookup strategyLookup,
                             ToolLookup toolLookup);

    // 中文说明：生成正式程序的唯一服务入口。
    // 前置条件：工序必须已确认、启用且有效，Setup/刀具/参数与当前项目一致。
    // 失败策略：任一安全门或后处理校验失败时返回 errors，不产生可导出的快照。
    ProgramGenerationResult generate(
        const QList<MachiningOperation> &operations,
        const PostProcessorBase &postProcessor,
        const PostProcessorOptions &options,
        const ProgramGenerationSnapshotOptions &snapshotOptions) const;

private:
    StrategyLookup m_strategyLookup;
    ToolLookup m_toolLookup;
};
