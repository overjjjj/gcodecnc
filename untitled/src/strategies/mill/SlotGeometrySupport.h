#pragma once
#include "../../import/StepImporter.h"

inline bool isUnsupportedArcSlot(const ContourFeature &feature)
{
    return feature.subType == QStringLiteral("arc_slot");
}

inline QString unsupportedArcSlotMessage()
{
    return QStringLiteral("Arc slots are not supported by rectangular slot strategies: true arc boundaries and cutter envelope safety are not proven.");
}
