#pragma once

#include <QMap>
#include <QString>

struct FormulaEvaluationResult {
    bool ok = false;
    double value = 0.0;
    QString error;
};

class RestrictedFormulaEvaluator
{
public:
    static FormulaEvaluationResult Evaluate(
        const QString &expression,
        const QMap<QString, double> &variables);
};
