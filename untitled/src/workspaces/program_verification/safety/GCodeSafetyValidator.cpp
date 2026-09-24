#include "GCodeSafetyValidator.h"

#include <QRegularExpression>

namespace {

enum class MotionMode {
    Unknown,
    Rapid,
    Linear,
    Arc
};

// 去掉分号和括号注释后再匹配指令，避免把说明文字中的 G/M 码当成执行代码。
static QString stripComments(QString line)
{
    const int semicolon = line.indexOf(QLatin1Char(';'));
    if (semicolon >= 0) {
        line.truncate(semicolon);
    }

    static const QRegularExpression parenComment(QStringLiteral("\\([^)]*\\)"));
    line.remove(parenComment);
    return line.toUpper().trimmed();
}

// 当前匹配依赖空白分隔，无法识别 G0X10 等紧凑写法；此处不是完整的 NC 词法解析器。
static bool containsWord(const QString &line, const QString &word)
{
    static const QString boundary = QStringLiteral("(^|\\s)%1(\\s|$)");
    return QRegularExpression(boundary.arg(QRegularExpression::escape(word))).match(line).hasMatch();
}

static bool containsWorkOffset(const QString &line)
{
    static const QRegularExpression workOffset(QStringLiteral("(^|\\s)G5[4-9](\\s|$)"));
    return workOffset.match(line).hasMatch();
}

// 当前只识别带整数部分的轴数值；X.5 等形式不在此匹配范围内。
static bool containsAxis(const QString &line, QChar axis)
{
    return QRegularExpression(QStringLiteral("(^|\\s)%1[-+]?\\d").arg(axis)).match(line).hasMatch();
}

static bool readAxisValue(const QString &line, QChar axis, double &value)
{
    const QRegularExpression re(QStringLiteral("(^|\\s)%1([-+]?\\d+(?:\\.\\d+)?)").arg(axis));
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

static void addError(GCodeSafetyReport &report, const QString &message)
{
    report.ok = false;
    report.messages.append(message);
}

static bool startsFixedCycle(const QString &line)
{
    return containsWord(line, QStringLiteral("G81")) ||
           containsWord(line, QStringLiteral("G82")) ||
           containsWord(line, QStringLiteral("G83")) ||
           containsWord(line, QStringLiteral("G84")) ||
           containsWord(line, QStringLiteral("G85")) ||
           containsWord(line, QStringLiteral("G86")) ||
           containsWord(line, QStringLiteral("G87")) ||
           containsWord(line, QStringLiteral("G88")) ||
           containsWord(line, QStringLiteral("G89"));
}

// 识别固定循环不代表支持执行；当前明确阻断 G86 至 G89。
static QString unsupportedFixedCycleCode(const QString &line)
{
    for (const QString &code : {QStringLiteral("G86"), QStringLiteral("G87"),
                                QStringLiteral("G88"), QStringLiteral("G89")}) {
        if (containsWord(line, code)) {
            return code;
        }
    }
    return QString();
}

} // namespace

// 对最终文本跟踪已支持的模态和收尾状态；逐行发现的问题附带行号，整体缺项单独报告。
// 不展开任意宏，也不验证刀具与工件、夹具的空间碰撞。
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
    bool coolantOn = false;
    bool fixedCycleActive = false;
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
        if (explicitRapid) {
            motionMode = MotionMode::Rapid;
        } else if (explicitLinear) {
            motionMode = MotionMode::Linear;
        } else if (explicitArc) {
            motionMode = MotionMode::Arc;
        }

        const bool hasXY = containsAxis(line, QLatin1Char('X')) ||
                           containsAxis(line, QLatin1Char('Y'));

        const bool cutterCompCancel = containsWord(line, QStringLiteral("G40"));
        const bool cutterCompStart = containsWord(line, QStringLiteral("G41")) ||
                                     containsWord(line, QStringLiteral("G42"));
        const bool fixedCycleStart = startsFixedCycle(line);
        if (containsWord(line, QStringLiteral("G80"))) {
            fixedCycleActive = false;
        }
        if (fixedCycleStart) {
            fixedCycleActive = true;
        }
        const QString unsupportedCycle = unsupportedFixedCycleCode(line);
        if (!unsupportedCycle.isEmpty()) {
            addError(report,
                     QStringLiteral("Line %1: Fixed cycle %2 is not supported in the first phase.")
                         .arg(i + 1)
                         .arg(unsupportedCycle));
        }
        const bool supportedFixedCycleStart =
            containsWord(line, QStringLiteral("G81")) ||
            containsWord(line, QStringLiteral("G82")) ||
            containsWord(line, QStringLiteral("G83")) ||
            containsWord(line, QStringLiteral("G84")) ||
            containsWord(line, QStringLiteral("G85"));
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
        if (containsWord(line, QStringLiteral("G83"))) {
            double peckDepth = 0.0;
            if (!readAxisValue(line, QLatin1Char('Q'), peckDepth) || peckDepth <= 0.0) {
                addError(report,
                         QStringLiteral("Line %1: G83 requires a positive peck depth Q.")
                             .arg(i + 1));
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
        if (containsWord(line, QStringLiteral("M3"))) {
            if (!hasToolChange) {
                toolBeforeSpindle = false;
            }
            hasSpindleStart = true;
            spindleRunning = true;
        }
        if (containsWord(line, QStringLiteral("M5"))) {
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

        // 固定循环激活后，后续 XY 行是循环孔位调用，不能按之前的 G0 模态误判为快移。
        const bool fixedCyclePositionCall = fixedCycleActive && hasXY && !explicitRapid;
        const bool rapidMove = motionMode == MotionMode::Rapid &&
                               !fixedCycleStart &&
                               !fixedCyclePositionCall;
        double zValue = 0.0;
        const bool hasZ = readAxisValue(line, QLatin1Char('Z'), zValue);
        const double rapidDestinationZ = hasZ ? zValue : currentZ;
        // XY 快移前必须已处于正 Z，终点也必须为正；同一行抬刀加横移不能代替先退刀。
        // 这里只用工件 Z=0 作为阈值，不表示已验证实际夹具高度或指定安全平面。
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
        addError(report, QStringLiteral("Missing spindle start S... M3."));
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
