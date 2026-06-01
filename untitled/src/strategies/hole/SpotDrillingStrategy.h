#pragma once
#include "../StrategyBase.h"
#include <QObject>

class SpotDrillingStrategy : public StrategyBase
{
public:
    QString id()          const override { return "hole_spot"; }
    QString displayName() const override { return QObject::tr("孔定点"); }

    StrategyParams defaultParams() const override;
    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry   &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const QVector<HoleFeature> &features,
                            const ToolEntry            &tool,
                            const StrategyParams       &params) const override;
};
