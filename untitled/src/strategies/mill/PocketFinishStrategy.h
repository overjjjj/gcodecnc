#pragma once

#include "../StrategyBase.h"

class PocketFinishStrategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("mill_pocket_finish"); }
    QString displayName() const override { return QStringLiteral("Pocket Wall Finish"); }

    // Shared bounded perimeter pass; floor finishing appends it without a tool change.
    static ToolpathResult generateRegionPass(const ContourFeature &feature,
                                            const ToolEntry &tool,
                                            const StrategyParams &params,
                                            bool includeToolCall);
    StrategyParams defaultParams() const override;
    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
};
