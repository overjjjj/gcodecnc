#pragma once
#include "../StrategyBase.h"

class BlindSlotMillingStrategy : public StrategyBase
{
public:
    QString id()          const override { return QStringLiteral("mill_blind_slot"); }
    QString displayName() const override { return QStringLiteral("Blind Slot Milling"); }
    StrategyParams defaultParams() const override;
    ToolpathResult generate(const HoleFeature    &feature,
                            const ToolEntry      &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry      &tool,
                            const StrategyParams &params) const override;
};
