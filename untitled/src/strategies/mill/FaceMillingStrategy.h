#pragma once
#include "../StrategyBase.h"

class FaceMillingStrategy : public StrategyBase
{
public:
    QString id()          const override { return QStringLiteral("mill_face"); }
    QString displayName() const override { return QStringLiteral("铣平面"); }

    StrategyParams defaultParams() const override;

    ToolpathResult generate(const HoleFeature    &feature,
                            const ToolEntry      &tool,
                            const StrategyParams &params) const override;

    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry      &tool,
                            const StrategyParams &params) const override;
};
