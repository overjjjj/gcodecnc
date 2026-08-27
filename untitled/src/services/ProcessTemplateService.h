#pragma once

#include "../core/ProcessTemplateLibrary.h"
#include "OperationFactory.h"

struct ProcessTemplateSelection {
    QStringList templateRefs;
    StrategyParams manualOverrides;
};

struct ProcessTemplateResolution {
    bool ok = false;
    QStringList errors;
    OperationParameterLayers layers;
};

class ProcessTemplateService
{
public:
    static ProcessTemplateResolution Resolve(
        const ProcessTemplateLibrary &library,
        const ProcessTemplateSelection &selection);
};
