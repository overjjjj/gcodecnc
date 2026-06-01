#pragma once
#include "StrategyBase.h"
#include <QMap>
#include <memory>

class StrategyFactory
{
public:
    static StrategyFactory &instance();

    void registerStrategy(std::shared_ptr<StrategyBase> strategy);
    std::shared_ptr<StrategyBase> strategy(const QString &id) const;
    QStringList                   availableIds() const;

    void registerDefaults();

private:
    StrategyFactory();
    QMap<QString, std::shared_ptr<StrategyBase>> m_strategies;
};
