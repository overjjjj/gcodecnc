#pragma once
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include "../import/StepImporter.h"
#include "../tool/ToolEntry.h"
#include "../gcode/ParametricToolpathProgram.h"

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

struct ToolpathResult {
    QString     gcode;
    bool        ok = false;
    QString     errorMsg;
    double      estimatedTimeS = 0.0;
    ParametricToolpathProgram parametricProgram;
};

class StrategyBase
{
public:
    virtual ~StrategyBase() = default;

    virtual QString id()          const = 0;
    virtual QString displayName() const = 0;

    virtual StrategyParams defaultParams() const = 0;

    virtual ToolpathResult generate(const HoleFeature   &feature,
                                    const ToolEntry      &tool,
                                    const StrategyParams &params) const = 0;

    virtual ToolpathResult generate(const QVector<HoleFeature> &features,
                                    const ToolEntry            &tool,
                                    const StrategyParams       &params) const {
        ToolpathResult result;
        if (features.isEmpty()) {
            result.errorMsg = QStringLiteral("未选择孔位。");
            return result;
        }

        QStringList blocks;
        double estimated = 0.0;
        for (const HoleFeature &feature : features) {
            ToolpathResult one = generate(feature, tool, params);
            if (!one.ok) {
                return one;
            }
            blocks.append(one.gcode.trimmed());
            estimated += one.estimatedTimeS;
        }
        result.gcode = blocks.join(QLatin1String("\n"));
        result.ok = true;
        result.estimatedTimeS = estimated;
        return result;
    }

    virtual ToolpathResult generate(const ContourFeature &/*feature*/,
                                    const ToolEntry      &/*tool*/,
                                    const StrategyParams &/*params*/) const {
        return {QString(), false, QStringLiteral("当前策略不支持铣削特征。"), 0.0,
                ParametricToolpathProgram()};
    }
};
