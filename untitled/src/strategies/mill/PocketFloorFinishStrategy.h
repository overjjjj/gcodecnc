#pragma once

#include "../StrategyBase.h"

class PocketFloorFinishStrategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("mill_pocket_floor_finish"); }
    QString displayName() const override { return QStringLiteral("Pocket Floor Finish"); }

    StrategyParams defaultParams() const override;
    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
};
