#pragma once

#include "../StrategyBase.h"
#include <QObject>

class CircleMillingStrategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("mill_circle"); }
    QString displayName() const override { return QObject::tr("铣边(封闭圆)"); }

    StrategyParams defaultParams() const override;

    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;

    ToolpathResult generate(const ContourFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
};
