#pragma once

#include <QString>
#include <QVariantMap>

struct StrategyParams {
    QVariantMap values;

    double get(const QString &key, double default_value = 0.0) const
    {
        return values.value(key, default_value).toDouble();
    }

    void set(const QString &key, double value)
    {
        values[key] = value;
    }

    QString getString(const QString &key,
                      const QString &default_value = {}) const
    {
        return values.value(key, default_value).toString();
    }
};
