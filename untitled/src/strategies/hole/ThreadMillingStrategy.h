#pragma once

#include "../StrategyBase.h"

class ThreadMillingStrategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("hole_thread_mill"); }
    QString displayName() const override { return QStringLiteral("铣牙"); }

    ProcessParameterSchema parameterSchema() const override;
    StrategyParams defaultParams() const override;

    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const QVector<HoleFeature> &features,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
};
