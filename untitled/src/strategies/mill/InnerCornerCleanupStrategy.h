#pragma once

#include "../StrategyBase.h"

class InnerCornerCleanupStrategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("mill_inner_corner_cleanup"); }
    QString displayName() const override { return QStringLiteral("矩形型腔清内角"); }

    ProcessParameterSchema parameterSchema() const override;
    StrategyParams defaultParams() const override;

    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
};
