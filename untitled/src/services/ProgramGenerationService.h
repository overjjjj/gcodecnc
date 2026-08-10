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
};

class ProgramGenerationService
{
public:
    using StrategyLookup =
        std::function<std::shared_ptr<StrategyBase>(const QString &strategyId)>;
    using ToolLookup = std::function<ToolEntry(int toolId)>;

    ProgramGenerationService(StrategyLookup strategyLookup,
                             ToolLookup toolLookup);

    ProgramGenerationResult generate(
        const QList<MachiningOperation> &operations,
        const PostProcessorBase &postProcessor,
        const PostProcessorOptions &options,
        const ProgramGenerationSnapshotOptions &snapshotOptions) const;

private:
    StrategyLookup m_strategyLookup;
    ToolLookup m_toolLookup;
};
