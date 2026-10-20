#include "GCodeSafetyValidator.h"

#include <QRegularExpression>
#include <QHash>
#include <QThreadStorage>
#include <cmath>
#include <limits>

namespace {

enum class MotionMode {
    Unknown,
    Rapid,
    Linear,
    Arc
};

// 函数说明：stripComments负责当前模块的核心处理。
static QString stripComments(QString line)
{
    const int semicolon = line.indexOf(QLatin1Char(';'));
    if (semicolon >= 0) {
        line.truncate(semicolon);
    }

    static const QRegularExpression parenComment(QStringLiteral("\\([^)]*\\)"));
    line.remove(parenComment);
    line = line.toUpper().trimmed();
    // Preserve named commands and exponent-like literals; normalize numeric word addresses.
    static const QRegularExpression word(
        QStringLiteral("(?<![A-Z])([A-DF-Z])\\s*([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+))(?![\\d.E])"));
    QString canonical;
    int cursor = 0;
    auto words = word.globalMatch(line);
    while (words.hasNext()) {
        const auto token = words.next();
        canonical += line.mid(cursor, token.capturedStart() - cursor);
        const QString letter = token.captured(1);
        const QString value = (letter == "G" || letter == "M")
            ? QString::number(token.captured(2).toDouble(), 'g', 16) : token.captured(2);
        canonical += QLatin1Char(' ') + letter + value + QLatin1Char(' ');
        cursor = token.capturedEnd();
    }
    canonical += line.mid(cursor);
    return canonical.simplified();
}

// Patterns depend only on fixed command words/axes, never on the NC contents.
// Keep compiled patterns per thread; every validation still matches every line.
static const QRegularExpression &compiledExpression(const QString &pattern)
{
    static QThreadStorage<QHash<QString,QRegularExpression> *> storage;
    if(!storage.hasLocalData())storage.setLocalData(new QHash<QString,QRegularExpression>);
    auto &expressions=*storage.localData();
    auto found=expressions.constFind(pattern);
    if(found==expressions.constEnd())
        found=expressions.insert(pattern,QRegularExpression(pattern));
    return found.value();
}

// 函数说明：containsWord负责当前模块的核心处理。
static bool containsWord(const QString &line, const QString &word)
{
    static const QString boundary = QStringLiteral("(^|\\s)%1(\\s|$)");
    return compiledExpression(boundary.arg(QRegularExpression::escape(word))).match(line).hasMatch();
}

// 函数说明：containsWorkOffset负责当前模块的核心处理。
static bool containsWorkOffset(const QString &line)
{
    static const QRegularExpression workOffset(QStringLiteral("(^|\\s)G5[4-9](\\s|$)"));
    return workOffset.match(line).hasMatch();
}

// 函数说明：containsAxis负责当前模块的核心处理。
static bool containsAxis(const QString &line, QChar axis)
{
    return compiledExpression(QStringLiteral("(^|\\s)%1[-+]?(?:\\d|\\.\\d)").arg(axis)).match(line).hasMatch();
}

// 函数说明：readAxisValue负责当前模块的核心处理。
static bool readAxisValue(const QString &line, QChar axis, double &value)
{
    const QRegularExpression &re=compiledExpression(QStringLiteral("(^|\\s)%1([-+]?(?:\\d+(?:\\.\\d*)?|\\.\\d+))").arg(axis));
    const QRegularExpressionMatch match = re.match(line);
    if (!match.hasMatch()) {
        return false;
    }

    bool ok = false;
    const double parsed = match.captured(2).toDouble(&ok);
    if (!ok) {
        return false;
    }
    value = parsed;
    return true;
}

// 函数说明：addError负责当前模块的核心处理。
static void addError(GCodeSafetyReport &report, const QString &message)
{
    report.ok = false;
    report.messages.append(message);
}

// 函数说明：startsFixedCycle负责当前模块的核心处理。
static bool startsFixedCycle(const QString &line)
{
    return containsWord(line, QStringLiteral("G73")) ||
           containsWord(line, QStringLiteral("G81")) ||
           containsWord(line, QStringLiteral("G82")) ||
           containsWord(line, QStringLiteral("G83")) ||
           containsWord(line, QStringLiteral("G84")) ||
           containsWord(line, QStringLiteral("G85")) ||
           containsWord(line, QStringLiteral("G76")) ||
           containsWord(line, QStringLiteral("G86")) ||
           containsWord(line, QStringLiteral("G87")) ||
           containsWord(line, QStringLiteral("G88")) ||
           containsWord(line, QStringLiteral("G89"));
}

// 函数说明：unsupportedFixedCycleCode负责当前模块的核心处理。
static QString unsupportedFixedCycleCode(const QString &line)
{
    for (const QString &code : {QStringLiteral("G87"),
                                QStringLiteral("G88"), QStringLiteral("G89")}) {
        if (containsWord(line, code)) {
            return code;
        }
    }
    return QString();
}

} // namespace

// 中文说明：只检查最终文本中的危险状态和收尾完整性，避免“显示路径安全”
// 与“实际导出代码危险”出现分叉；发现问题时返回可直接展示给用户的原因。
GCodeSafetyReport GCodeSafetyValidator::validate(const QString &gcode)
{
    GCodeSafetyReport report;
    if (gcode.trimmed().isEmpty()) {
        addError(report, QStringLiteral("G-code is empty."));
        return report;
    }

    bool hasPlaneAndCancel = false;
    bool hasMetricUnits = false;
    bool hasImperialUnits = false;
    bool hasAbsolute = false;
    bool hasIncremental = false;
    bool hasFeedPerRevolution = false;
    bool hasWorkOffset = false;
    bool hasToolChange = false;
    bool hasSpindleStart = false;
    bool hasSpindleStop = false;
    bool hasCoolantOff = false;
    bool hasProgramEnd = false;
    bool toolBeforeSpindle = true;
    bool cutterCompActive = false;
    bool spindleRunning = false;
    bool spindleSpeedKnown = false;
    double spindleSpeed = 0.0;
    bool coolantOn = false;
    bool fixedCycleActive = false;
    bool siemensCycleActive = false;
    int toolChangeCount = 0;
    MotionMode motionMode = MotionMode::Unknown;
    int firstSpindleStopLine = -1;
    int firstCoolantOffLine = -1;
    int firstProgramEndLine = -1;
    bool zKnown = false;
    double currentZ = 0.0;

    const QStringList rawLines = gcode.split(QLatin1Char('\n'));
    for (int i = 0; i < rawLines.size(); ++i) {
        const QString line = stripComments(rawLines.at(i));
        if (line.isEmpty()) {
            continue;
        }

        hasPlaneAndCancel = hasPlaneAndCancel ||
                            (containsWord(line, QStringLiteral("G17")) &&
                             containsWord(line, QStringLiteral("G40")) &&
                             containsWord(line, QStringLiteral("G49")) &&
                             containsWord(line, QStringLiteral("G80")));
        hasMetricUnits = hasMetricUnits || containsWord(line, QStringLiteral("G21"));
        hasImperialUnits = hasImperialUnits || containsWord(line, QStringLiteral("G20"));
        hasAbsolute = hasAbsolute || containsWord(line, QStringLiteral("G90"));
        hasIncremental = hasIncremental || containsWord(line, QStringLiteral("G91"));
        hasFeedPerRevolution = hasFeedPerRevolution || containsWord(line, QStringLiteral("G95"));
        hasWorkOffset = hasWorkOffset || containsWorkOffset(line);

        const bool explicitRapid = containsWord(line, QStringLiteral("G0")) ||
                                   containsWord(line, QStringLiteral("G00"));
        const bool explicitLinear = containsWord(line, QStringLiteral("G1")) ||
                                    containsWord(line, QStringLiteral("G01"));
        const bool explicitArc = containsWord(line, QStringLiteral("G2")) ||
                                 containsWord(line, QStringLiteral("G02")) ||
                                 containsWord(line, QStringLiteral("G3")) ||
                                 containsWord(line, QStringLiteral("G03"));
        const bool hasXY = containsAxis(line, QLatin1Char('X')) ||
                           containsAxis(line, QLatin1Char('Y'));

        const bool cutterCompCancel = containsWord(line, QStringLiteral("G40"));
        const bool cutterCompStart = containsWord(line, QStringLiteral("G41")) ||
                                     containsWord(line, QStringLiteral("G42"));
        static const QRegularExpression siemensCycleWord(QStringLiteral("\\bCYCLE8[1-6]\\b"));
        const bool siemensCycleCall = siemensCycleWord.match(line).hasMatch();
        if (containsWord(line, QStringLiteral("MCALL"))) siemensCycleActive = siemensCycleCall;
        const bool fixedCycleStart = startsFixedCycle(line);
        const bool fixedCycleCancel = containsWord(line, QStringLiteral("G80"));
        if (fixedCycleCancel) {
            fixedCycleActive = false;
        }
        if (fixedCycleStart) {
            fixedCycleActive = true;
        }
        if (explicitRapid) {
            motionMode = MotionMode::Rapid;
        } else if (explicitLinear) {
            motionMode = MotionMode::Linear;
        } else if (explicitArc) {
            motionMode = MotionMode::Arc;
        } else if (fixedCycleStart || fixedCycleCancel) {
            motionMode = MotionMode::Unknown;
        }
        const QString unsupportedCycle = unsupportedFixedCycleCode(line);
        if (!unsupportedCycle.isEmpty()) {
            addError(report,
                     QStringLiteral("Line %1: Fixed cycle %2 is not supported in the first phase.")
                         .arg(i + 1)
                         .arg(unsupportedCycle));
        }
        const bool supportedFixedCycleStart =
            containsWord(line, QStringLiteral("G73")) ||
            containsWord(line, QStringLiteral("G81")) ||
            containsWord(line, QStringLiteral("G82")) ||
            containsWord(line, QStringLiteral("G83")) ||
            containsWord(line, QStringLiteral("G84")) ||
            containsWord(line, QStringLiteral("G85")) ||
            containsWord(line, QStringLiteral("G76")) ||
            containsWord(line, QStringLiteral("G86"));
        if (supportedFixedCycleStart) {
            double holeBottom = 0.0;
            double returnPlane = 0.0;
            if (!readAxisValue(line, QLatin1Char('Z'), holeBottom) ||
                !readAxisValue(line, QLatin1Char('R'), returnPlane) ||
                returnPlane <= holeBottom) {
                addError(report,
                         QStringLiteral("Line %1: Fixed cycle requires return plane R above hole bottom Z.")
                             .arg(i + 1));
            }
        }
        const bool g73Peck = containsWord(line, QStringLiteral("G73"));
        const bool g83Peck = containsWord(line, QStringLiteral("G83"));
        const bool g76Boring = containsWord(line, QStringLiteral("G76"));
        if (g73Peck || g83Peck || g76Boring) {
            double peckDepth = 0.0;
            if (!readAxisValue(line, QLatin1Char('Q'), peckDepth) || peckDepth <= 0.0) {
                addError(report,
                         QStringLiteral("Line %1: %2 requires a positive %3 Q.")
                             .arg(i + 1)
                             .arg(g73Peck ? QStringLiteral("G73")
                                         : g83Peck ? QStringLiteral("G83") : QStringLiteral("G76"))
                             .arg(g76Boring ? QStringLiteral("lateral offset")
                                           : QStringLiteral("peck depth")));
            }
        }
        if (fixedCycleActive && explicitRapid) {
            addError(report,
                     QStringLiteral("Line %1: Cancel fixed cycle with G80 before G0 motion.")
                         .arg(i + 1));
        }
        if (cutterCompCancel) {
            if (cutterCompActive && (motionMode != MotionMode::Linear || !hasXY)) {
                addError(report,
                         QStringLiteral("Line %1: G40 requires a linear X/Y lead-out on the cancellation line.")
                             .arg(i + 1));
            }
            cutterCompActive = false;
        }
        if (cutterCompStart) {
            if (motionMode != MotionMode::Linear || !hasXY) {
                addError(report,
                         QStringLiteral("Line %1: G41/G42 requires a linear X/Y lead-in on the activation line.")
                             .arg(i + 1));
            }
            cutterCompActive = true;
        }

        if (containsWord(line, QStringLiteral("M6"))) {
            if (fixedCycleActive) {
                addError(report,
                         QStringLiteral("Line %1: Cancel fixed cycle with G80 before tool change.")
                             .arg(i + 1));
            }
            if (cutterCompActive) {
                addError(report,
                         QStringLiteral("Line %1: Cancel cutter compensation with G40 before tool change.")
                             .arg(i + 1));
            }
            if (toolChangeCount > 0) {
                if (spindleRunning) {
                    addError(report,
                             QStringLiteral("Line %1: Stop the spindle with M5 before tool change.")
                                 .arg(i + 1));
                }
                if (coolantOn) {
                    addError(report,
                             QStringLiteral("Line %1: Turn coolant off with M9 before tool change.")
                                 .arg(i + 1));
                }
                if (!zKnown || currentZ <= 0.0) {
                    addError(report,
                             QStringLiteral("Line %1: Retract to a known safe Z above 0 before tool change.")
                                 .arg(i + 1));
                }
            }
            ++toolChangeCount;
            hasToolChange = true;
        }
        // S is modal and applies to the whole block, even when written after M3/M4.
        static const QRegularExpression speedWord(QStringLiteral("(^|\\s)S([^\\s]*)"));
        static const QRegularExpression speedNumber(QStringLiteral("^[+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)$"));
        auto speeds = speedWord.globalMatch(line);
        int speedCount = 0;
        while (speeds.hasNext()) {
            const QString value = speeds.next().captured(2);
            bool numeric = false;
            spindleSpeed = value.toDouble(&numeric);
            spindleSpeedKnown = numeric && speedNumber.match(value).hasMatch() &&
                std::isfinite(spindleSpeed) && spindleSpeed >= 0.0 &&
                spindleSpeed <= std::numeric_limits<int>::max();
            if (++speedCount > 1 || !spindleSpeedKnown) {
                addError(report, QStringLiteral("Line %1: Invalid or repeated spindle speed S.").arg(i + 1));
            }
        }
        static const QRegularExpression spindleCommand(QStringLiteral("(^|\\s)M0?[345](?=\\s|$)"));
        auto commands = spindleCommand.globalMatch(line);
        int commandCount = 0;
        while (commands.hasNext()) { commands.next(); ++commandCount; }
        if (commandCount > 1) {
            addError(report, QStringLiteral("Line %1: Repeated or conflicting spindle commands in one block.").arg(i + 1));
        }
        const bool spindleStart = containsWord(line, QStringLiteral("M3")) ||
            containsWord(line, QStringLiteral("M03")) ||
            containsWord(line, QStringLiteral("M4")) || containsWord(line, QStringLiteral("M04"));
        if (spindleStart) {
            if (!hasToolChange) {
                toolBeforeSpindle = false;
            }
            hasSpindleStart = true;
            spindleRunning = true;
            if (!spindleSpeedKnown || spindleSpeed < 1.0) {
                addError(report, QStringLiteral("Line %1: M3/M4 requires a known positive spindle speed S of at least 1 rpm.").arg(i + 1));
            }
        }
        if (containsWord(line, QStringLiteral("M5")) || containsWord(line, QStringLiteral("M05"))) {
            hasSpindleStop = true;
            spindleRunning = false;
            if (firstSpindleStopLine < 0) {
                firstSpindleStopLine = i;
            }
        }
        if (containsWord(line, QStringLiteral("M7")) ||
            containsWord(line, QStringLiteral("M8"))) {
            coolantOn = true;
        }
        if (containsWord(line, QStringLiteral("M9"))) {
            hasCoolantOff = true;
            coolantOn = false;
            if (firstCoolantOffLine < 0) {
                firstCoolantOffLine = i;
            }
        }
        if (containsWord(line, QStringLiteral("M30"))) {
            hasProgramEnd = true;
            if (firstProgramEndLine < 0) {
                firstProgramEndLine = i;
            }
            if (spindleRunning) {
                addError(report,
                         QStringLiteral("Line %1: Stop the spindle with M5 before program end M30.")
                             .arg(i + 1));
            }
            if (coolantOn) {
                addError(report,
                         QStringLiteral("Line %1: Turn coolant off with M9 before program end M30.")
                             .arg(i + 1));
            }
            if (fixedCycleActive) {
                addError(report,
                         QStringLiteral("Line %1: Cancel fixed cycle with G80 before program end M30.")
                             .arg(i + 1));
            }
        }

        const bool fixedCyclePositionCall = fixedCycleActive && hasXY && !explicitRapid;
        const bool hasCoordinate = hasXY || containsAxis(line, QLatin1Char('Z'));
        const bool siemensCyclePositionCall = siemensCycleActive && hasCoordinate;
        const bool cuttingMotion = fixedCycleStart || fixedCyclePositionCall || siemensCyclePositionCall ||
            ((motionMode == MotionMode::Linear || motionMode == MotionMode::Arc) && hasCoordinate) ||
            siemensCycleCall;
        if ((spindleRunning || cuttingMotion) &&
            (!spindleRunning || !spindleSpeedKnown || spindleSpeed < 1.0)) {
            addError(report, QStringLiteral("Line %1: Cutting/spindle motion requires an active M3/M4 and positive spindle speed S.").arg(i + 1));
        }
        if (hasCoordinate && !fixedCycleStart && !fixedCyclePositionCall &&
            !explicitRapid && !explicitLinear && !explicitArc &&
            motionMode == MotionMode::Unknown) {
            addError(report,
                     QStringLiteral("Line %1: Coordinate motion after G80 requires an explicit G0/G1/G2/G3; use explicit G0 for safe retract.")
                         .arg(i + 1));
        }
        const bool rapidMove = motionMode == MotionMode::Rapid &&
                               !fixedCycleStart &&
                               !fixedCyclePositionCall && !siemensCyclePositionCall;
        double zValue = 0.0;
        const bool hasZ = readAxisValue(line, QLatin1Char('Z'), zValue);
        const double rapidDestinationZ = hasZ ? zValue : currentZ;
        if (rapidMove && hasXY) {
            if (!zKnown || currentZ <= 0.0) {
                addError(report,
                         QStringLiteral("Line %1: Retract to a known safe Z above 0 before G0 X/Y motion.")
                             .arg(i + 1));
            } else if (rapidDestinationZ <= 0.0) {
                addError(report,
                         QStringLiteral("Line %1: G0 X/Y move occurs at or below cutting Z (Z=%2).")
                             .arg(i + 1)
                             .arg(rapidDestinationZ, 0, 'f', 3));
            }
        }

        if (hasZ) {
            currentZ = zValue;
            zKnown = true;
        }
    }

    if (!hasPlaneAndCancel) {
        addError(report, QStringLiteral("Missing safe modal reset line containing G17 G40 G49 G80."));
    }
    if (!hasMetricUnits) {
        addError(report, QStringLiteral("Missing explicit units mode G21/G20."));
    }
    if (hasImperialUnits) {
        addError(report, QStringLiteral("Imperial units G20 are not supported; use millimetres G21."));
    }
    if (!hasAbsolute) {
        addError(report, QStringLiteral("Missing absolute coordinate mode G90."));
    }
    if (hasIncremental) {
        addError(report, QStringLiteral("Incremental coordinate mode G91 is not supported."));
    }
    if (hasFeedPerRevolution) {
        addError(report, QStringLiteral("Feed-per-revolution mode G95 is not supported; use G94."));
    }
    if (!hasWorkOffset) {
        addError(report, QStringLiteral("Missing work offset G54-G59."));
    }
    if (!hasToolChange) {
        addError(report, QStringLiteral("Missing tool change T... M6."));
    }
    if (!hasSpindleStart) {
        addError(report, QStringLiteral("Missing spindle start S... M3/M4."));
    }
    if (!toolBeforeSpindle) {
        addError(report, QStringLiteral("Spindle starts before a tool change."));
    }
    if (!hasSpindleStop) {
        addError(report, QStringLiteral("Missing spindle stop M5."));
    }
    if (!hasCoolantOff) {
        addError(report, QStringLiteral("Missing coolant off M9."));
    }
    if (!hasProgramEnd) {
        addError(report, QStringLiteral("Missing program end M30."));
    }
    if (hasProgramEnd &&
        ((hasSpindleStop && firstSpindleStopLine >= firstProgramEndLine) ||
         (hasCoolantOff && firstCoolantOffLine >= firstProgramEndLine))) {
        addError(report, QStringLiteral("M5 and M9 must occur before program end M30."));
    }
    if (cutterCompActive) {
        addError(report, QStringLiteral("Cancel cutter compensation with G40 before program end."));
    }

    return report;
}
