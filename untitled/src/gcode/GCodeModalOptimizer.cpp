#include "GCodeModalOptimizer.h"

#include <QRegularExpression>

namespace {

// 函数说明：motionCode负责当前模块的核心处理。
QString motionCode(const QString &token)
{
    static const QRegularExpression expression(QStringLiteral("^G0?([0-3])$"),
                                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(token);
    return match.hasMatch() ? QStringLiteral("G%1").arg(match.captured(1)) : QString();
}

// The optimizer only removes repeated G0-G3 words.  Any other group-one
// motion code makes that tracked state unusable until a new G0-G3 appears.
bool invalidatesTrackedMotion(const QString &token)
{
    static const QRegularExpression expression(
        QStringLiteral("^G(?:33(?:\\.1)?|38\\.[2-5]|7[346]|8[0-9])$"),
        QRegularExpression::CaseInsensitiveOption);
    return expression.match(token).hasMatch();
}

// 函数说明：isProtectedLine负责当前模块的核心处理。
bool isProtectedLine(const QStringList &tokens)
{
    for (const QString &token : tokens) {
        const QString upper = token.toUpper();
        if (upper.startsWith(QLatin1Char('M'))) {
            return true;
        }
        if (upper.startsWith(QLatin1Char('G')) && motionCode(upper).isEmpty()) {
            return true;
        }
    }
    return false;
}

// 函数说明：wordValue负责当前模块的核心处理。
QString wordValue(const QString &token, QChar word)
{
    if (token.size() <= 1 || token.at(0).toUpper() != word) {
        return QString();
    }
    bool ok = false;
    token.mid(1).toDouble(&ok);
    return ok ? token.mid(1) : QString();
}

} // namespace

// 函数说明：optimize负责当前模块的核心处理。
QString GCodeModalOptimizer::optimize(const QString &gcode)
{
    QStringList output;
    QString activeMotion;
    QString activeFeed;
    QString activeSpindle;

    const QStringList lines = gcode.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char(';'))) {
            output.append(line);
            continue;
        }

        const QStringList tokens = trimmed.split(QRegularExpression(QStringLiteral("\\s+")),
                                                 Qt::SkipEmptyParts);
        if (isProtectedLine(tokens)) {
            for (const QString &token : tokens) {
                const QString upper = token.toUpper();
                const QString motion = motionCode(upper);
                if (!motion.isEmpty()) {
                    activeMotion = motion;
                } else if (invalidatesTrackedMotion(upper)) {
                    activeMotion.clear();
                }
            }
            output.append(line);
            continue;
        }

        QStringList optimized;
        for (const QString &token : tokens) {
            const QString motion = motionCode(token);
            if (!motion.isEmpty()) {
                if (motion != activeMotion) {
                    optimized.append(token);
                }
                activeMotion = motion;
                continue;
            }

            const QString feed = wordValue(token, QLatin1Char('F'));
            if (!feed.isEmpty()) {
                if (feed != activeFeed) {
                    optimized.append(token);
                }
                activeFeed = feed;
                continue;
            }

            const QString spindle = wordValue(token, QLatin1Char('S'));
            if (!spindle.isEmpty()) {
                if (spindle != activeSpindle) {
                    optimized.append(token);
                }
                activeSpindle = spindle;
                continue;
            }

            optimized.append(token);
        }
        output.append(optimized.join(QLatin1Char(' ')));
    }
    return output.join(QLatin1Char('\n'));
}
