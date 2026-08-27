#include "RestrictedFormulaEvaluator.h"

#include <cmath>

namespace {

constexpr double kMaximumMagnitude = 1000000.0;

bool isAllowedVariableName(const QString &name)
{
    if (name == QStringLiteral("AT")) {
        return true;
    }
    if (name.size() < 2 ||
        (name.at(0) != QLatin1Char('H') && name.at(0) != QLatin1Char('D') &&
         name.at(0) != QLatin1Char('T'))) {
        return false;
    }
    for (int index = 1; index < name.size(); ++index) {
        if (!name.at(index).isDigit()) {
            return false;
        }
    }
    return true;
}

class Parser
{
public:
    Parser(const QString &expression, const QMap<QString, double> &variables)
        : expression_(expression), variables_(variables)
    {
    }

    FormulaEvaluationResult parse()
    {
        FormulaEvaluationResult result;
        if (expression_.trimmed().isEmpty()) {
            result.error = QStringLiteral("Formula is empty.");
            return result;
        }
        bool ok = true;
        const double value = parseExpression(&ok);
        skipWhitespace();
        if (!ok || position_ != expression_.size() || !std::isfinite(value) ||
            std::abs(value) > kMaximumMagnitude) {
            result.error = error_.isEmpty()
                ? QStringLiteral("Formula is invalid or outside the allowed range.")
                : error_;
            return result;
        }
        result.ok = true;
        result.value = value;
        return result;
    }

private:
    double parseExpression(bool *ok)
    {
        double value = parseTerm(ok);
        while (*ok) {
            skipWhitespace();
            if (!consume(QLatin1Char('+')) && !consume(QLatin1Char('-'))) {
                break;
            }
            const QChar operation = expression_.at(position_ - 1);
            const double right = parseTerm(ok);
            value = operation == QLatin1Char('+') ? value + right : value - right;
            if (!checkRange(value, ok)) {
                return 0.0;
            }
        }
        return value;
    }

    double parseTerm(bool *ok)
    {
        double value = parsePrimary(ok);
        while (*ok) {
            skipWhitespace();
            if (!consume(QLatin1Char('*')) && !consume(QLatin1Char('/'))) {
                break;
            }
            const QChar operation = expression_.at(position_ - 1);
            const double right = parsePrimary(ok);
            if (operation == QLatin1Char('/') && std::abs(right) < 1.0e-12) {
                fail(QStringLiteral("Division by zero is not allowed."), ok);
                return 0.0;
            }
            value = operation == QLatin1Char('*') ? value * right : value / right;
            if (!checkRange(value, ok)) {
                return 0.0;
            }
        }
        return value;
    }

    double parsePrimary(bool *ok)
    {
        skipWhitespace();
        if (consume(QLatin1Char('('))) {
            const double value = parseExpression(ok);
            skipWhitespace();
            if (!consume(QLatin1Char(')'))) {
                fail(QStringLiteral("Missing closing parenthesis."), ok);
            }
            return value;
        }
        if (position_ >= expression_.size()) {
            fail(QStringLiteral("Formula ends unexpectedly."), ok);
            return 0.0;
        }
        const QChar current = expression_.at(position_);
        if (current.isDigit() || current == QLatin1Char('.')) {
            return parseNumber(ok);
        }
        if (current.isLetter()) {
            return parseVariable(ok);
        }
        fail(QStringLiteral("Unsupported formula token."), ok);
        return 0.0;
    }

    double parseNumber(bool *ok)
    {
        const int start = position_;
        bool sawDigit = false;
        bool sawDecimalPoint = false;
        while (position_ < expression_.size()) {
            const QChar current = expression_.at(position_);
            if (current.isDigit()) {
                sawDigit = true;
                ++position_;
            } else if (current == QLatin1Char('.') && !sawDecimalPoint) {
                sawDecimalPoint = true;
                ++position_;
            } else {
                break;
            }
        }
        bool converted = false;
        const double value = expression_.mid(start, position_ - start).toDouble(&converted);
        if (!sawDigit || !converted || value < 0.0) {
            fail(QStringLiteral("Only positive decimal literals are allowed."), ok);
            return 0.0;
        }
        return value;
    }

    double parseVariable(bool *ok)
    {
        const int start = position_;
        while (position_ < expression_.size() &&
               expression_.at(position_).isLetterOrNumber()) {
            ++position_;
        }
        const QString name = expression_.mid(start, position_ - start);
        if (!isAllowedVariableName(name) || !variables_.contains(name) ||
            !std::isfinite(variables_.value(name))) {
            fail(QStringLiteral("Unknown formula variable: %1").arg(name), ok);
            return 0.0;
        }
        return variables_.value(name);
    }

    bool checkRange(double value, bool *ok)
    {
        if (!std::isfinite(value) || std::abs(value) > kMaximumMagnitude) {
            fail(QStringLiteral("Formula result is outside the allowed range."), ok);
            return false;
        }
        return true;
    }

    void skipWhitespace()
    {
        while (position_ < expression_.size() && expression_.at(position_).isSpace()) {
            ++position_;
        }
    }

    bool consume(QChar token)
    {
        if (position_ < expression_.size() && expression_.at(position_) == token) {
            ++position_;
            return true;
        }
        return false;
    }

    void fail(const QString &error, bool *ok)
    {
        if (error_.isEmpty()) {
            error_ = error;
        }
        *ok = false;
    }

    QString expression_;
    const QMap<QString, double> &variables_;
    int position_ = 0;
    QString error_;
};

} // namespace

FormulaEvaluationResult RestrictedFormulaEvaluator::Evaluate(
    const QString &expression,
    const QMap<QString, double> &variables)
{
    return Parser(expression, variables).parse();
}
