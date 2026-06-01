#pragma once

#include "../StrategyBase.h"

class HoleCircularMillingStrategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("hole_circular_mill"); }
    QString displayName() const override { return QStringLiteral("圆插补铣孔"); }

    StrategyParams defaultParams() const override;

    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;

    ToolpathResult generate(const QVector<HoleFeature> &features,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
};
