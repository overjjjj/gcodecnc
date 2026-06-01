#pragma once
#include "../StrategyBase.h"

class PocketRoughingStrategy : public StrategyBase
{
public:
    QString id()          const override { return QStringLiteral("mill_pocket_rough"); }
    QString displayName() const override { return QStringLiteral("型腔开粗"); }

    StrategyParams defaultParams() const override;

    ToolpathResult generate(const HoleFeature    &feature,
                            const ToolEntry      &tool,
                            const StrategyParams &params) const override;

    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry      &tool,
                            const StrategyParams &params) const override;
};
