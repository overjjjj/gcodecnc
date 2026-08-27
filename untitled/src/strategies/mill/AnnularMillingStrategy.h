#pragma once

#include "../StrategyBase.h"

#include <QObject>

class AnnularMillingStrategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("mill_annular"); }
    QString displayName() const override { return QObject::tr("环形铣"); }

    ProcessParameterSchema parameterSchema() const override;
    StrategyParams defaultParams() const override;

    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
};
