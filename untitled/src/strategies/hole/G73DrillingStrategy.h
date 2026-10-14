#pragma once

#include "../StrategyBase.h"

class G73DrillingStrategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("hole_g73"); }
    QString displayName() const override { return QObject::tr("高速断屑钻 G73"); }

    StrategyParams defaultParams() const override;
    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const QVector<HoleFeature> &features,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
};
