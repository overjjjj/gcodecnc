#include "GCodeSafetyValidator.h"

#include <QRegularExpression>

namespace {

enum class MotionMode {
    Unknown,
    Rapid,
    Linear,
    Arc
};

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

        const bool fixedCyclePositionCall = fixedCycleActive && hasXY && !explicitRapid;
        const bool rapidMove = motionMode == MotionMode::Rapid &&
                               !fixedCycleStart &&
                               !fixedCyclePositionCall;
        double zValue = 0.0;
        const bool hasZ = readAxisValue(line, QLatin1Char('Z'), zValue);
        const bool rapidZKnown = hasZ || zKnown;
        const double rapidDestinationZ = hasZ ? zValue : currentZ;
        if (rapidMove && hasXY) {
            if (!rapidZKnown) {
                addError(report,
                         QStringLiteral("Line %1: G0 X/Y move occurs before a known safe Z is established.")
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
