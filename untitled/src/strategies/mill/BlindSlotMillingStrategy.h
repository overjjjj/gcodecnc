#pragma once
#include "../StrategyBase.h"

class BlindSlotMillingStrategy : public StrategyBase
{
public:
    // Matches the CAM offset threshold used by generation.
    static bool isUnsupportedG42(double compensation)
    {
        return compensation <= -1.0e-9;
    }
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
