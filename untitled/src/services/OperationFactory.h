#pragma once

#include "../core/ProcessContext.h"
#include "../core/ProcessParameterSchema.h"
#include "../strategies/OperationProposal.h"

struct OperationParameterLayers {
    QString templateId;
    QString templateVersion;
    StrategyParams systemDefaults;
    StrategyParams materialTemplate;
    StrategyParams toolTypeTemplate;
    StrategyParams moduleRule;
    StrategyParams manualOverrides;
};

struct OperationFactoryResult {
    bool ok = false;
    QStringList errors;
    MachiningOperation operation;
};

class OperationFactory
{
public:
    static OperationFactoryResult CreateConfirmed(
        const OperationProposal &proposal,
        const OperationParameterLayers &layers);
    static OperationFactoryResult CreateConfirmed(
        const OperationProposal &proposal,
        const OperationParameterLayers &layers,
        const ProcessContext &context);
    static void RestoreTemplateValues(MachiningOperation *operation);
};
