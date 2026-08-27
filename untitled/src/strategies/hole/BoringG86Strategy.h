#pragma once

#include "../StrategyBase.h"

class BoringG86Strategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("hole_bore_g86"); }
    QString displayName() const override { return QObject::tr("镗孔 G86"); }

    ProcessParameterSchema parameterSchema() const override;
    StrategyParams defaultParams() const override;
    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const QVector<HoleFeature> &features,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
};
