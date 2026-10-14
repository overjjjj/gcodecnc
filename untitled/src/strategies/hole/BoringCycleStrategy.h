#pragma once

#include "../../strategies/StrategyBase.h"

class BoringCycleStrategy : public StrategyBase
{
public:
    BoringCycleStrategy(QString strategyId, QString cycleCode, bool stopSpindleBeforeRetract);

    QString id() const override { return m_strategyId; }
    QString displayName() const override { return m_cycleCode + QStringLiteral(" Boring"); }
    StrategyParams defaultParams() const override;
    ToolpathResult generate(const HoleFeature &feature,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;
    ToolpathResult generate(const QVector<HoleFeature> &features,
                            const ToolEntry &tool,
                            const StrategyParams &params) const override;

private:
    QString m_strategyId;
    QString m_cycleCode;
    bool m_stopSpindleBeforeRetract = false;
};
