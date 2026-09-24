#pragma once
#include "../StrategyBase.h"

class TaperedSlotMillingStrategy : public StrategyBase
{
public:
    // Shared by generation and saved-program output checks; direction and rotation do not make this floor safe.
    static bool isLinearDirectionProfile(const ContourFeature &feature)
    {
        return feature.slopeStartLength == 0.0 && feature.slopeEndLength == 0.0 &&
               !(feature.slopeMinWidth > 1.0e-6 || feature.slopeMaxWidth > 1.0e-6);
    }
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
