#pragma once

#include "../StrategyBase.h"

class OuterContourChamferStrategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("mill_outer_chamfer"); }
    QString displayName() const override { return QStringLiteral("外形倒角"); }

    ProcessParameterSchema parameterSchema() const override;
    StrategyParams defaultParams() const override;

    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
};
