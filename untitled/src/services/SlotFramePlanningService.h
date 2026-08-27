#pragma once

#include "../import/SlotFrameFeature.h"
#include "OperationFactory.h"

struct SlotFrameToolInfo {
    int id = -1;
    QString type;
    double diameter = 0.0;
    double fluteLength = 0.0;
    double totalLength = 0.0;
};

struct SlotFrameConfirmationResult {
    bool ok = false;
    QStringList errors;
    QList<MachiningOperation> operations;
};

class SlotFramePlanningService
{
public:
    static SlotFrameConfirmationResult Confirm(
        const SlotFrameFeature &feature,
        const QMap<int, SlotFrameToolInfo> &available_tools,
        const ProcessContext &context,
        const OperationParameterLayers &layers);
};
