#pragma once
#include "../StrategyBase.h"

class SurfaceFinishStrategy : public StrategyBase
{
public:
    QString id()          const override { return QStringLiteral("mill_surface_finish"); }
    QString displayName() const override { return QStringLiteral("精面/打磨"); }

    ProcessParameterSchema parameterSchema() const override;
    StrategyParams defaultParams() const override;

    ToolpathResult generate(const HoleFeature    &feature,
                            const ToolEntry      &tool,
                            const StrategyParams &params) const override;

    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry      &tool,
                            const StrategyParams &params) const override;
};
