#pragma once
#include "../StrategyBase.h"

class DeepHoleDrillingStrategy : public StrategyBase
{
public:
    QString id()          const override { return "hole_deephole"; }
    QString displayName() const override { return QObject::tr("深孔钻"); }

    StrategyParams defaultParams() const override;
    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry   &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const QVector<HoleFeature> &features,
                            const ToolEntry            &tool,
                            const StrategyParams       &params) const override;
};
