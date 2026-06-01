#pragma once
#include "../StrategyBase.h"

class OpenContourMillingStrategy : public StrategyBase
{
public:
    QString id()          const override { return QStringLiteral("mill_open_contour"); }
    QString displayName() const override { return QStringLiteral("开放轮廓铣边"); }

    StrategyParams defaultParams() const override;

    ToolpathResult generate(const HoleFeature    &feature,
                            const ToolEntry      &tool,
                            const StrategyParams &params) const override;

    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry      &tool,
                            const StrategyParams &params) const override;
};
