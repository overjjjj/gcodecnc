#pragma once
#include "../StrategyBase.h"

class ReamingStrategy : public StrategyBase
{
public:
    QString id()          const override { return "hole_reaming"; }
    QString displayName() const override { return QObject::tr("铰孔"); }

    StrategyParams defaultParams() const override;
    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry   &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const QVector<HoleFeature> &features,
                            const ToolEntry            &tool,
                            const StrategyParams       &params) const override;
};
