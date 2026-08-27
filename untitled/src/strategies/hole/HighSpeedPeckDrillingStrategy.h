#pragma once

#include "../StrategyBase.h"

class HighSpeedPeckDrillingStrategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("hole_peck_g73"); }
    QString displayName() const override { return QObject::tr("高速排屑钻 G73"); }

    ProcessParameterSchema parameterSchema() const override;
    StrategyParams defaultParams() const override;
    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const QVector<HoleFeature> &features,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
};
