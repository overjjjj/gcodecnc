#pragma once

#include "../StrategyBase.h"

class PocketFinishStrategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("mill_pocket_finish"); }
    QString displayName() const override { return QStringLiteral("Pocket Wall Finish"); }

    StrategyParams defaultParams() const override;
    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
};
