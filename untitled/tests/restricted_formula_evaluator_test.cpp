#include "../src/services/RestrictedFormulaEvaluator.h"

#include <QCoreApplication>

#include <cmath>
#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const QMap<QString, double> variables{
        {QStringLiteral("H1"), 12.0},
        {QStringLiteral("H2"), 4.5},
        {QStringLiteral("AT"), 0.2}
    };

    const FormulaEvaluationResult valid = RestrictedFormulaEvaluator::Evaluate(
        QStringLiteral("(H1 + H2) / 2 - AT"), variables);
    if (!expect(valid.ok && std::abs(valid.value - 8.05) < 1.0e-9,
                "allowed variables, decimals, operators and parentheses should evaluate")) {
        return 1;
    }

    QMap<QString, double> compoundVariables = variables;
    compoundVariables.insert(QStringLiteral("D1"), 10.0);
    compoundVariables.insert(QStringLiteral("T1"), 8.0);
    const FormulaEvaluationResult compoundValue = RestrictedFormulaEvaluator::Evaluate(
        QStringLiteral("D1 / 2 + T1"), compoundVariables);
    if (!expect(compoundValue.ok && std::abs(compoundValue.value - 13.0) < 1.0e-9,
                "declared diameter and tool variables should evaluate in compound plans")) {
        return 1;
    }

    const QStringList invalidExpressions{
        QStringLiteral("H3-AT"),
        QStringLiteral("system('dir')"),
        QStringLiteral("H1/0"),
        QStringLiteral("-1"),
        QStringLiteral("1e9"),
        QStringLiteral("H1 +")
    };
    for (const QString &expression : invalidExpressions) {
        if (!expect(!RestrictedFormulaEvaluator::Evaluate(expression, variables).ok,
                    "unsafe or invalid formulas must be rejected")) {
            return 1;
        }
    }

    const FormulaEvaluationResult excessive = RestrictedFormulaEvaluator::Evaluate(
        QStringLiteral("H1*H1*H1*H1*H1*H1"), variables);
    if (!expect(!excessive.ok, "formula results outside the bounded range must be rejected")) {
        return 1;
    }

    return 0;
}
