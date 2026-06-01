#pragma once
#include "../StrategyBase.h"

class ClosedContourMillingStrategy : public StrategyBase
{
public:
    QString id()          const override { return QStringLiteral("mill_closed_contour"); }
    QString displayName() const override { return QStringLiteral("封闭轮廓铣边"); }

    StrategyParams defaultParams() const override;

    ToolpathResult generate(const HoleFeature    &feature,
                            const ToolEntry      &tool,
                            const StrategyParams &params) const override;

    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry      &tool,
                            const StrategyParams &params) const override;
};
