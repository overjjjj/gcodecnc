#pragma once
#include "MachiningOperation.h"

inline bool isSlotStrategy(const QString &id)
{
    return id == QStringLiteral("mill_slot") || id == QStringLiteral("mill_blind_slot") ||
           id == QStringLiteral("mill_tapered_slot");
}

inline StrategyParams slotCuttingParams(StrategyParams params)
{
    params.values.remove(QStringLiteral("slotLength"));
    params.values.remove(QStringLiteral("slotWidth"));
    params.values.remove(QStringLiteral("angle"));
    return params;
}

inline bool sameSlotGeometry(const ContourFeature &a, const ContourFeature &b)
{
    return a.subType == b.subType && a.center == b.center && a.radius == b.radius &&
        a.depth == b.depth && a.width == b.width && a.length == b.length && a.angle == b.angle &&
        a.openSide == b.openSide && a.slopeStartLength == b.slopeStartLength &&
        a.slopeEndLength == b.slopeEndLength && a.slopeMinWidth == b.slopeMinWidth &&
        a.slopeMaxWidth == b.slopeMaxWidth && a.axis == b.axis && a.region == b.region &&
        a.points == b.points && a.islands == b.islands;
}

inline bool sameSlotOperation(const MachiningOperation &a, const MachiningOperation &b)
{
    return a.id == b.id && a.opType == b.opType && a.stage == b.stage &&
        a.featureRef == b.featureRef && a.strategyId == b.strategyId && a.toolId == b.toolId &&
        a.params.values == b.params.values && a.enabled == b.enabled && a.geometryRefs == b.geometryRefs &&
        a.selectionEvidence == b.selectionEvidence && a.toolpathState == b.toolpathState &&
        a.warnings == b.warnings && sameSlotGeometry(a.contourFeature, b.contourFeature);
}
