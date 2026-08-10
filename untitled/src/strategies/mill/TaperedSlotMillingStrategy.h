#pragma once
#include "../StrategyBase.h"

class TaperedSlotMillingStrategy : public StrategyBase
{
public:
    QString id()          const override { return QStringLiteral("mill_tapered_slot"); }
    QString displayName() const override { return QStringLiteral("Tapered Slot Milling"); }
    StrategyParams defaultParams() const override;
    ToolpathResult generate(const HoleFeature    &feature,
                            const ToolEntry      &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry      &tool,
                            const StrategyParams &params) const override;
};
