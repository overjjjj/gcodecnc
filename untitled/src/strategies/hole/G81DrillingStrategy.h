#pragma once

#include "../../strategies/StrategyBase.h"

class G81DrillingStrategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("hole_g81"); }
    QString displayName() const override { return QStringLiteral("G81 Drilling"); }

    StrategyParams defaultParams() const override;
    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const QVector<HoleFeature> &features,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
};
