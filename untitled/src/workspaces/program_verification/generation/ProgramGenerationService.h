#pragma once

#include "../../../workspaces/machine_operation/model/MachineProfile.h"
#include "../../../core/ProjectManager.h"
#include "../../../postprocessor/PostProcessorBase.h"
#include "../../../tool/ToolEntry.h"
#include "../../process_design/operations/MachiningOperation.h"

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

    using SlotGeometryValidator = std::function<bool(const ContourFeature &)>;

    ProgramGenerationService(StrategyLookup strategyLookup,
                             ToolLookup toolLookup,
                             SlotGeometryValidator slotGeometryValidator = {});

    // 根据有 ID 且启用的工序生成程序，拒绝已过期或出错的刀路及不满足准入条件的输入。
    // 策略生成、后处理和文本检查全部通过后才返回快照；失败原因汇总在 errors 中。
    // 此服务不写入项目，也不承担实机执行或完整碰撞验证。
    ProgramGenerationResult generate(
        const QList<MachiningOperation> &operations,
        const PostProcessorBase &postProcessor,
        const PostProcessorOptions &options,
        const ProgramGenerationSnapshotOptions &snapshotOptions) const;

private:
    StrategyLookup m_strategyLookup;
    ToolLookup m_toolLookup;
    SlotGeometryValidator m_slotGeometryValidator;
};
