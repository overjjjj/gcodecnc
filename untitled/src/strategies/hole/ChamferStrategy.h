#pragma once
#include "../StrategyBase.h"

class ChamferStrategy : public StrategyBase
{
public:
    QString id()          const override { return "hole_chamfer"; }
    QString displayName() const override { return QObject::tr("孔倒角"); }

    StrategyParams defaultParams() const override;
    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry   &tool,
                            const StrategyParams &params) const override;
};
