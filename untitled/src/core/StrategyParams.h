#pragma once

#include <QString>
#include <QVariantMap>

// 工艺参数值由候选、正式工序和策略共同使用，不依赖策略实现。
struct StrategyParams {
    QVariantMap values;
    double get(const QString &k, double def = 0.0) const {
        return values.value(k, def).toDouble();
    }
    void set(const QString &k, double v) { values[k] = v; }
    QString getString(const QString &k, const QString &def = {}) const {
        return values.value(k, def).toString();
    }
};
