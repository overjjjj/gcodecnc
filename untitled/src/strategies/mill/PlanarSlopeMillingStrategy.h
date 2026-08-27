#pragma once

#include "../StrategyBase.h"

class PlanarSlopeMillingStrategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("mill_slope_plane_2d"); }
    QString displayName() const override { return QStringLiteral("二维斜面铣"); }

    ProcessParameterSchema parameterSchema() const override;
    StrategyParams defaultParams() const override;

    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
};
