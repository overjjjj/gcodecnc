#include "SimulationController.h"
#include "ExactRetractionCheck.h"
#include "SketchMotionCheck.h"
#include "../../../tool/ToolLibrary.h"
#include <QtGlobal>
#include <QRegularExpression>
#include <QSet>
#include <QMap>
#include <algorithm>
#include <cmath>
#include <limits>

// 函数说明：SimulationController负责当前模块的核心处理。
SimulationController::SimulationController(QObject *parent)
    // 函数说明：QObject负责当前模块的核心处理。
    : QObject(parent)
    // 函数说明：m_timer负责当前模块的核心处理。
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(50); // 20 Hz tick
    connect(m_timer, &QTimer::timeout, this, &SimulationController::onTick);
}

// 函数说明：setMesh负责当前模块的核心处理。
void SimulationController::setMesh(const MeshData &mesh)
{
    m_mesh = mesh;
    m_sketchContext.reset();
    m_checkedContextFingerprint.clear();
    m_checkResult = {};
    emit checkResultChanged();
}

void SimulationController::setSketchVerificationContext(const SketchVerificationContext &context)
{
    if (m_sketchContext && m_sketchContext->fingerprint() == context.fingerprint()) return;
    m_sketchContext = context;
    m_checkedContextFingerprint.clear();
    m_checkResult = {};
    emit checkResultChanged();
}

// 函数说明：loadGCode负责当前模块的核心处理。
void SimulationController::loadGCode(const QString &gcode, bool useProjectTools, int sourceLineOffset,
                                    const Coordinate3D &modelOrigin)
{
    m_modelOrigin = modelOrigin;
    m_timer->stop();
    m_running = false;
    m_step = 0;
    m_gcode = gcode;
    static const QRegularExpression fanucG73(
        QStringLiteral("(?:^|\\s)G73(?:\\s|$)"),
        QRegularExpression::CaseInsensitiveOption);
    m_hasUnverifiedRetract = fanucG73.match(gcode).hasMatch();
    static const QRegularExpression fanucG76(
        QStringLiteral("(?:^|\\s)G76(?:\\s|$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression fanucG86(
        QStringLiteral("(?:^|\\s)G86(?:\\s|$)"),
        QRegularExpression::CaseInsensitiveOption);
    m_hasUnverifiedG76LateralShift = fanucG76.match(gcode).hasMatch();
    m_hasUnverifiedG86SpindleStopRetract = fanucG86.match(gcode).hasMatch();
    static const QRegularExpression tappingCycle(QStringLiteral("(?:G0*84(?![\\d.])|CYCLE84\\s*\\()"),
        QRegularExpression::CaseInsensitiveOption);
    m_hasUnverifiedSpindleSync = tappingCycle.match(gcode).hasMatch();
    m_moves = parseGCode(gcode, useProjectTools, &m_motionError);
    if (m_sketchContext && sourceLineOffset!=0)
        m_motionError=QStringLiteral("草图内部程序检查不支持外部分段行号偏移。");
    for (auto &move : m_moves) move.sourceLine += sourceLineOffset;
    auto modelMoves = m_moves;
    for (auto &move : modelMoves) { move.from += m_modelOrigin; move.to += m_modelOrigin; }
    m_checkedContextFingerprint = m_sketchContext ? m_sketchContext->fingerprint() : QString();
    m_checkResult = m_motionError.isEmpty() ?
        (m_sketchContext ? checkSketchMotion(*m_sketchContext, m_moves, gcode, modelOrigin) : checkMotion(m_mesh, modelMoves))
        : SimulationCheckResult{SimulationCheckState::InvalidMotion, m_motionError, {}};
    emit checkResultChanged();

    m_path.clear();
    m_rapidSegments.clear();
    QVector<QVector3D> starts;
    for (const ToolMove &mv : m_moves) {
        if (mv.fromKnownAxes != 7 || mv.toKnownAxes != 7) continue;
        if (m_path.isEmpty()) {
            m_path.append((mv.from + m_modelOrigin).toVector3D());
        }
        m_path.append((mv.to + m_modelOrigin).toVector3D());
        starts.append((mv.from + m_modelOrigin).toVector3D());
        m_rapidSegments.append(mv.isRapid);
    }
    emit toolPathReady(m_path, m_rapidSegments, starts);
    emit progressChanged(0.0);
    emit currentGCodeLineChanged(-1);
    if (!m_moves.isEmpty()) {
        emit toolDiameterChanged(m_moves.first().toolDiameter);
        emit toolModelPathChanged(m_moves.first().toolModelPath);
    }
    if (!m_path.isEmpty()) {
        emit toolPositionChanged(m_path.first());
    }
}

// 函数说明：play负责当前模块的核心处理。
void SimulationController::play()
{
    if (std::none_of(m_moves.cbegin(), m_moves.cend(), [](const ToolMove &move) {
            return move.toKnownAxes == 7;
        })) return;
    m_running = true;
    m_timer->start();
}

// 函数说明：pause负责当前模块的核心处理。
void SimulationController::pause()
{
    m_running = false;
    m_timer->stop();
}

// 函数说明：stop负责当前模块的核心处理。
void SimulationController::stop()
{
    m_running = false;
    m_timer->stop();
    m_step = 0;
    emit progressChanged(0.0);
    emit currentGCodeLineChanged(m_moves.isEmpty() ? -1 : m_moves.first().sourceLine);
    if (!m_moves.isEmpty()) {
        emit toolDiameterChanged(m_moves.first().toolDiameter);
        emit toolModelPathChanged(m_moves.first().toolModelPath);
    }
    if (!m_path.isEmpty()) {
        emit toolPositionChanged(m_path.first());
    }
}

// 函数说明：setSpeed负责当前模块的核心处理。
void SimulationController::setSpeed(double multiplier)
{
    m_speed = multiplier > 0 ? multiplier : 1.0;
    // faster speed = shorter interval; base 50ms / multiplier, clamped 10-500ms
    int interval = qBound(10, int(50.0 / m_speed), 500);
    m_timer->setInterval(interval);
}

// 函数说明：seekToGCodeLine负责当前模块的核心处理。
void SimulationController::seekToGCodeLine(int line)
{
    if (m_moves.isEmpty() || line < 0) {
        return;
    }

    int target = -1;
    for (int i = 0; i < m_moves.size(); ++i) {
        if (m_moves[i].sourceLine >= line) {
            target = i;
            break;
        }
    }
    if (target < 0) {
        target = m_moves.size() - 1;
    }

    m_step = target;
    const ToolMove &mv = m_moves[target];
    emit toolDiameterChanged(mv.toolDiameter);
    emit toolModelPathChanged(mv.toolModelPath);
    if (mv.toKnownAxes == 7) emit toolPositionChanged((mv.to + m_modelOrigin).toVector3D());
    emit progressChanged(double(target + 1) / double(m_moves.size()));
    emit currentGCodeLineChanged(mv.sourceLine);
}

// 函数说明：onTick负责当前模块的核心处理。
void SimulationController::onTick()
{
    if (!m_running || m_moves.isEmpty()) return;

    if (m_step >= m_moves.size()) {
        m_timer->stop();
        m_running = false;
        emit progressChanged(1.0);
        emit simulationFinished();
        return;
    }

    const ToolMove &mv = m_moves[m_step];
    emit toolDiameterChanged(mv.toolDiameter);
    emit toolModelPathChanged(mv.toolModelPath);
    if (mv.toKnownAxes == 7) emit toolPositionChanged((mv.to + m_modelOrigin).toVector3D());
    emit progressChanged(double(m_step + 1) / double(m_moves.size()));
    emit currentGCodeLineChanged(mv.sourceLine);
    ++m_step;
}

// ---------------------------------------------------------------------------
// G-code parser: extracts G0/G1/G2/G3 moves, tracks current X/Y/Z position.
// G2/G3 arcs are approximated as short line segments for visual simulation.
// Also expands Siemens CYCLE81/82/83/84/85 and common Fanuc modal hole cycles.
// ---------------------------------------------------------------------------

namespace {

struct DrillCycle {
    bool   active = false;
    int    kind   = 83;    // 73, 76, or 81-86
    double initialPlane = 0.0;
    double rtp    = 5.0;   // retract plane (safe Z)
    double rfp    = 0.0;   // reference plane (top of workpiece)
    double sdis   = 2.0;   // safety clearance distance
    double dp     = -10.0; // final drill depth (absolute)
    double dtb    = 0.0;   // dwell at bottom (seconds)
    double mdep   = 5.0;   // max peck depth (CYCLE83 only)
    int    vari   = 1;     // Siemens CYCLE83 VARI: 0 chip breaking, 1 chip removal
    double vrt    = 0.0;   // Siemens CYCLE83 _VRT; zero means the documented 1 mm default
    bool   siemens = false;
};

// Parse "CYCLE83(arg0,arg1,...)" — returns values by position, ignoring empties.
static QVector<double> parseCycleArgs(const QString &line)
{
    const int lp = line.indexOf('(');
    const int rp = line.lastIndexOf(')');
    if (lp < 0 || rp <= lp) return {};
    const QString inner = line.mid(lp + 1, rp - lp - 1);
    QVector<double> vals;
    for (const QString &tok : inner.split(',')) {
        vals.append(tok.trimmed().isEmpty() ? 0.0 : tok.trimmed().toDouble());
    }
    return vals;
}

constexpr double kPi = 3.14159265358979323846;

// 函数说明：normalizeSweep负责当前模块的核心处理。
static double normalizeSweep(double sweep, bool clockwise)
{
    if (clockwise) {
        while (sweep >= 0.0) sweep -= 2.0 * kPi;
    } else {
        while (sweep <= 0.0) sweep += 2.0 * kPi;
    }
    return sweep;
}

// 函数说明：addMove负责当前模块的核心处理。
static void addMove(QVector<ToolMove> &moves, Coordinate3D &cur, Coordinate3D to,
                    bool isRapid, double toolDiameter, const QString &toolModelPath, int sourceLine)
{
    if (to == cur) return;
    ToolMove mv;
    mv.from = cur;
    mv.to = to;
    mv.isRapid = isRapid;
    mv.toolDiameter = toolDiameter;
    mv.toolModelPath = toolModelPath;
    mv.sourceLine = sourceLine;
    moves.append(mv);
    cur = to;
}

// Append a rapid move helper (skips zero-length moves).
static void addRapid(QVector<ToolMove> &moves, Coordinate3D &cur, Coordinate3D to,
                     double toolDiameter, const QString &toolModelPath, int sourceLine)
{
    addMove(moves, cur, to, true, toolDiameter, toolModelPath, sourceLine);
}

// Append a feed move helper.
static void addFeed(QVector<ToolMove> &moves, Coordinate3D &cur, Coordinate3D to,
                    double toolDiameter, const QString &toolModelPath, int sourceLine)
{
    addMove(moves, cur, to, false, toolDiameter, toolModelPath, sourceLine);
}

// 函数说明：addArc负责当前模块的核心处理。
static bool arcCenterRepresentable(const Coordinate3D &start, double i, double j)
{
    const double actualI = (start.x() + i) - start.x();
    const double actualJ = (start.y() + j) - start.y();
    const double radius = std::hypot(i, j);
    if ((i != 0.0 && actualI == 0.0) || (j != 0.0 && actualJ == 0.0)) return false;
    return std::isfinite(actualI) && std::isfinite(actualJ) && radius > 1e-6
        && std::hypot(actualI-i, actualJ-j) <= std::max(1e-12, radius*1e-9);
}

static bool addArc(QVector<ToolMove> &moves, Coordinate3D &cur,
                   Coordinate3D end, double iOffset, double jOffset,
                   bool clockwise, double toolDiameter, const QString &toolModelPath, int sourceLine)
{
    if (!arcCenterRepresentable(cur, iOffset, jOffset)) return false;
    const double cx = cur.x() + iOffset;
    const double cy = cur.y() + jOffset;
    const double radius = std::hypot(cur.x() - cx, cur.y() - cy);
    if (!std::isfinite(radius) || radius <= 1.0e-6) return false;

    const double startAngle = std::atan2(cur.y() - cy, cur.x() - cx);
    const double endAngle = std::atan2(end.y() - cy, end.x() - cx);
    const bool fullCircle = end.x() == cur.x() && end.y() == cur.y();
    if (!fullCircle && std::remainder(endAngle-startAngle, 2.0*kPi) == 0.0) return false;
    const double sweep = fullCircle
                             ? (clockwise ? -2.0 * kPi : 2.0 * kPi)
                             : normalizeSweep(endAngle - startAngle, clockwise);
    const int segments = qBound(12, int(std::ceil(std::abs(sweep) * radius / 1.0)), 256);
    const Coordinate3D start = cur;
    for (int s = 1; s <= segments; ++s) {
        const double t = double(s) / double(segments);
        const double a = startAngle + sweep * t;
        Coordinate3D p(double(cx + std::cos(a) * radius),
                    double(cy + std::sin(a) * radius),
                    double(start.z() + (end.z() - start.z()) * t));
        if (s == segments) {
            p = end;
        }
        const int previousCount=moves.size();
        addFeed(moves, cur, p, toolDiameter, toolModelPath, sourceLine);
        if(moves.size()>previousCount) moves.last().chordDeviation =
            radius*(1-std::cos(std::abs(sweep)/(2*segments))) +
            std::abs(std::hypot(double(end.x())-cx,double(end.y())-cy)-radius);
    }
    return true;
}

// Expand one hole position (hx, hy) for the active drill cycle.
static void expandDrillHole(QVector<ToolMove> &moves, Coordinate3D &cur,
                             double hx, double hy, const DrillCycle &c,
                             double toolDiameter, const QString &toolModelPath, int sourceLine)
{
    const double safeZ  = c.rtp;
    const double clearZ = c.rfp + c.sdis;
    const double botZ   = c.dp;

    // 1. Rapid to XY at safe height
    addRapid(moves, cur, {double(hx), double(hy), safeZ}, toolDiameter, toolModelPath, sourceLine);
    // 2. Rapid down to clearance plane
    addRapid(moves, cur, {double(hx), double(hy), clearZ}, toolDiameter, toolModelPath, sourceLine);

    if (c.kind == 73 || c.kind == 83) {
        // Fanuc G73 has a controller/machine-parameter short retract that is not
        // present in the final NC. Siemens CYCLE83 VARI=0 is explicit: it uses a
        // 1 mm G1 retract by default, or the positive _VRT value.
        const bool siemensShortRetract = c.siemens && c.kind == 83 && c.vari == 0;
        const double shortRetract = c.vrt > 0.0 ? c.vrt : 1.0;
        double z = c.rfp + c.sdis;
        while (z > c.dp + 1e-6) {
            double next = z - c.mdep;
            if (next < c.dp) next = c.dp;
            addFeed(moves, cur, {double(hx), double(hy), double(next)}, toolDiameter, toolModelPath, sourceLine);
            if (next > c.dp + 1e-6) {
                if (siemensShortRetract) {
                    const double retractZ = std::min(double(clearZ), double(next) + shortRetract);
                    addFeed(moves, cur, {double(hx), double(hy), double(retractZ)},
                            toolDiameter, toolModelPath, sourceLine);
                } else {
                    addRapid(moves, cur, {double(hx), double(hy), clearZ},
                             toolDiameter, toolModelPath, sourceLine);
                }
            }
            z = next;
        }
    } else {
        // CYCLE81/82/84/85: single feed to bottom.
        addFeed(moves, cur, {double(hx), double(hy), botZ}, toolDiameter, toolModelPath, sourceLine);
    }
    if (c.kind == 84 || c.kind == 85) {
        addFeed(moves, cur, {double(hx), double(hy), clearZ}, toolDiameter, toolModelPath, sourceLine);
    }
    // 3. Retract to safe height
    addRapid(moves, cur, {double(hx), double(hy), safeZ}, toolDiameter, toolModelPath, sourceLine);
}

} // namespace

QVector<ExternalProgramSection> SimulationController::externalPrograms(const QString &text, QString *error)
{
    if (error) error->clear();
    QVector<ExternalProgramSection> result;
    const QStringList lines = text.split('\n');
    const auto fail = [&](const QString &message) {
        if (error) *error = message;
        return QVector<ExternalProgramSection>{};
    };
    bool delimited = false;
    for (const auto &line : lines) delimited |= line.trimmed() == "%";
    if (!delimited) result.append({tr("程序 1"), text, 0, lines.size() - 1});
    else {
        int start = -1;
        QString label;
        static const QRegularExpression sectionRe(QStringLiteral("^\\s*\\(SECTION\\s+([^:]+):\\s*(.*?)\\)\\s*$"));
        static const QRegularExpression comments(QStringLiteral("\\([^()]*\\)"));
        for (int i = 0; i < lines.size(); ++i) {
            const QString line = lines[i].trimmed();
            if (line == "%") {
                if (start < 0) start = i;
                else {
                    result.append({label.isEmpty() ? tr("程序 %1").arg(result.size() + 1) : label,
                                   lines.mid(start, i - start + 1).join('\n'), start, i});
                    start = -1; label.clear();
                }
            } else if (start < 0) {
                const auto match = sectionRe.match(line);
                if (match.hasMatch()) {
                    label = match.captured(1) + " · " + match.captured(2).section('/', 0, -2);
                    continue;
                }
                if (line.startsWith("//")) continue;
                QString outside = line;
                outside.remove(comments);
                if (!outside.section(';', 0, 0).trimmed().isEmpty())
                    return fail(tr("第 %1 行：百分号程序边界外存在指令或无效注释。").arg(i + 1));
            }
        }
        if (start >= 0) return fail(tr("第 %1 行：程序缺少结束百分号。").arg(start + 1));
    }
    for (int i = 0; i < result.size(); ++i) {
        const auto &section = result[i];
        const QString detail = externalGCodeError(section.text, section.firstLine);
        if (!detail.isEmpty()) return fail(tr("程序段 %1：%2").arg(i + 1).arg(detail));
    }
    if (result.isEmpty()) return fail(tr("文件没有可仿真的程序。"));
    return result;
}

QString SimulationController::externalGCodeError(const QString &gcode, int sourceLineOffset)
{
    static const QRegularExpression wordRe(QStringLiteral("([A-Z])\\s*([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+))"));
    Coordinate3D current;
    int modal = 0, knownAxes = 7, activeCycle = 0;
    double initialPlane = 0, referencePlane = 0;
    bool incremental = false, returnInitial = true;
    bool ended = false, programNumberSeen = false, moved = false, delimiterSeen = false;
    const QStringList lines = gcode.split('\n');
    for (int index = 0; index < lines.size(); ++index) {
        const auto fail = [index, sourceLineOffset](const QString &detail) {
            return tr("第 %1 行：%2").arg(index + sourceLineOffset + 1).arg(detail);
        };
        if (lines[index].trimmed().startsWith("//")) continue;
        QString line;
        bool comment = false;
        for (QChar c : lines[index]) {
            if (c == '(') {
                if (comment) return fail(tr("不支持嵌套括号注释。"));
                comment = true; line += ' ';
            } else if (c == ')') {
                if (!comment) return fail(tr("括号注释不匹配。"));
                comment = false; line += ' ';
            } else if (!comment) {
                if (c == ';') break;
                line += c.toUpper();
            }
        }
        if (comment) return fail(tr("括号注释未闭合。"));
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line == "%") {
            if (delimiterSeen || moved) ended = true;
            delimiterSeen = true;
            continue;
        }
        if (ended) return fail(tr("程序结束后仍有指令；请用独立百分号边界分段。"));
        QSet<QChar> seen;
        QSet<int> groups;
        QMap<QChar, double> values;
        Coordinate3D next = current;
        double i = 0.0, j = 0.0;
        bool offsets = false, reset = false, endBlock = false;
        int axes = 0, cycleStart = 0, explicitMotion = -1;
        int consumed = 0;
        auto words = wordRe.globalMatch(line);
        while (words.hasNext()) {
            const auto word = words.next();
            if (!line.mid(consumed, word.capturedStart() - consumed).trimmed().isEmpty())
                return fail(tr("无法识别的语法；不支持宏、表达式或子程序。"));
            consumed = word.capturedEnd();
            const QChar letter = word.captured(1)[0];
            const double value = word.captured(2).toDouble();
            if (!std::isfinite(value) || std::abs(value) > 1000000.0)
                return fail(tr("数值超出仿真支持范围（±1000000）。"));
            if (letter != 'G' && letter != 'M' && seen.contains(letter))
                return fail(tr("同一地址重复：%1").arg(letter));
            seen.insert(letter); values.insert(letter, value);
            if (letter == 'G') {
                int group = -1;
                if (value == 0 || value == 1 || value == 2 || value == 3) { explicitMotion = int(value); group = 1; }
                else if (value == 73 || value == 76 || value == 81 || value == 83 || value == 86) { cycleStart = int(value); group = 1; }
                else if (value == 17) group = 2;
                else if (value == 21) group = 3;
                else if (value == 90 || value == 91) { incremental = value == 91; group = 4; }
                else if (value == 91.1) group = 5;
                else if (value == 94) group = 6;
                else if (value == 98 || value == 99) { returnInitial = value == 98; group = 10; }
                else if (value == 69 || value == 28 || value == 43 || value == 80) group = int(value);
                else if (value == 40 || value == 49 || value == 54) {
                    if (moved) return fail(tr("G%1 仅支持在运动前声明。").arg(value));
                    reset = true; group = int(value);
                } else return fail(tr("不支持指令 G%1；G68 旋转及其他未实现模式不能预览。").arg(value));
                if (groups.contains(group)) return fail(tr("同一模态组包含多条 G 指令。"));
                groups.insert(group);
            } else if (letter == 'M') {
                if (value == 2 || value == 30) endBlock = true;
                else if (value != 3 && value != 4 && value != 5 && value != 6 && value != 7 && value != 8 && value != 9)
                    return fail(tr("不支持 M%1（含子程序/流程控制）。").arg(value));
            } else if (letter == 'X') { next.setX(double(value)); axes |= 1; }
            else if (letter == 'Y') { next.setY(double(value)); axes |= 2; }
            else if (letter == 'Z') { next.setZ(double(value)); axes |= 4; }
            else if (letter == 'I') { i = value; offsets = true; }
            else if (letter == 'J') { j = value; offsets = true; }
            else if (letter == 'N' || letter == 'T' || letter == 'O' || letter == 'H') {
                if (value < 0 || value != std::floor(value)) return fail(tr("%1 必须为非负整数。").arg(letter));
                if (letter == 'O') {
                    if (programNumberSeen || moved) return fail(tr("仅支持单个主程序。"));
                    programNumberSeen = true;
                }
            } else if (letter == 'F' || letter == 'S') {
                if (value < 0 || (letter == 'F' && value == 0)) return fail(tr("进给/转速数值无效。"));
            } else if (letter != 'R' && letter != 'Q') return fail(tr("不支持地址 %1。").arg(letter));
        }
        if (!line.mid(consumed).trimmed().isEmpty() || consumed == 0)
            return fail(tr("无法识别的语法；只支持数值字地址。"));
        if ((groups.contains(69) || reset || endBlock || groups.contains(80)) && (axes || offsets || cycleStart))
            return fail(tr("初始化、取消或结束指令不能与坐标运动放在同一行。"));
        if (groups.contains(43) != seen.contains('H') ||
            (groups.contains(43) && (axes || offsets || groups.contains(1) || groups.contains(49) || groups.contains(28))))
            return fail(tr("G43 仅支持独立的 H 整数声明；补偿表未知，不应用 H 偏置。"));
        if (groups.contains(80)) { activeCycle = 0; modal = -1; }
        if (explicitMotion >= 0) { modal = explicitMotion; activeCycle = 0; }
        if (groups.contains(28)) {
            if (!incremental || !axes || offsets || groups.contains(1) || activeCycle || reset || endBlock || seen.contains('R') || seen.contains('Q'))
                return fail(tr("G28 仅支持 G91 下的零增量回参考点声明。"));
            for (QChar axis : QStringLiteral("XYZ"))
                if (seen.contains(axis) && values[axis] != 0) return fail(tr("G28 仅支持零增量轴值。"));
            knownAxes &= ~axes; // Machine reference coordinates are not program coordinates.
            continue;
        }
        if (incremental && (axes || offsets || cycleStart || groups.contains(4)))
            return fail(tr("G91 仅支持零增量 G28；其他增量运动尚未支持。"));
        if ((seen.contains('R') || seen.contains('Q')) && !cycleStart)
            return fail(tr("R/Q 仅支持完整的钻孔循环启动行；R 圆弧尚未支持。"));
        if (cycleStart) {
            if (knownAxes != 7 || !seen.contains('Z') || !seen.contains('R') || offsets)
                return fail(tr("钻孔循环需要已知 XYZ 起点及明确的 Z、R。"));
            if (values['Z'] >= values['R'] || values['R'] > current.z())
                return fail(tr("当前仅支持 Z < R ≤ 循环起始平面的向下钻孔。"));
            if ((cycleStart == 73 || cycleStart == 83) &&
                (!seen.contains('Q') || values['Q'] <= 0 || (values['R'] - values['Z']) / values['Q'] > 10000))
                return fail(tr("啄钻需要正 Q，且每孔不超过 10000 次进刀。"));
            if (seen.contains('Q') && (values['Q'] <= 0 || (cycleStart != 73 && cycleStart != 83 && cycleStart != 76)))
                return fail(tr("该循环的 Q 参数无效。"));
            activeCycle = cycleStart; initialPlane = current.z(); referencePlane = values['R'];
            next.setZ(double(returnInitial ? initialPlane : referencePlane));
            current = next; moved = true;
            continue;
        }
        if (activeCycle && (axes || offsets)) {
            if ((axes & 4) || offsets) return fail(tr("活动循环仅支持 XY 孔位与 G98/G99 返回模式；修改深度请重新声明循环。"));
            next.setZ(double(returnInitial ? initialPlane : referencePlane));
            current = next; moved = true;
            continue;
        }
        if (axes || offsets) {
            if (modal < 0) return fail(tr("取消运动后必须显式指定 G0/G1/G2/G3。"));
            if (modal == 2 || modal == 3) {
                if (knownAxes != 7) return fail(tr("回参考点后必须先以绝对坐标确定 XYZ，再执行圆弧。"));
                const double radius = std::hypot(i, j);
                if (!arcCenterRepresentable(current, i, j))
                    return fail(tr("圆心在当前坐标范围内无法可靠表示。"));
                const double cx=current.x()+i, cy=current.y()+j;
                if ((next.x()!=current.x() || next.y()!=current.y()) &&
                    std::remainder(std::atan2(next.y()-cy,next.x()-cx)-std::atan2(current.y()-cy,current.x()-cx),2.0*kPi)==0.0)
                    return fail(tr("不同端点不能使用零扫角圆弧。"));
                const double endRadius = std::hypot(next.x() - current.x() - i, next.y() - current.y() - j);
                if (!offsets || radius <= 1e-6 || std::abs(endRadius - radius) > qMax(.001, radius * 1e-5))
                    return fail(tr("IJ 圆弧缺少有效圆心或起终点半径不一致。"));
            } else if (offsets) return fail(tr("IJ 圆心仅适用于 G2/G3。"));
            knownAxes |= axes; moved = true; current = next;
        }
        ended = endBlock;
    }
    if (parseGCode(gcode, false).isEmpty()) return tr("程序没有可仿真的有效运动。");
    return {};
}


// 中文说明：解析最终导出的 G 代码并展开常见直线、圆弧和钻孔循环，
// 保留源行号用于界面高亮；不支持或参数不完整的指令不得静默伪造路径。
QVector<ToolMove> SimulationController::parseGCode(const QString &gcode, bool useProjectTools, QString *error)
{
    QVector<ToolMove> moves;
    if(error)error->clear();

    // Match word-address tokens like G0, X12.3, Z-5.0
    static const QRegularExpression wordRe(
        QStringLiteral("([A-Za-z])\\s*([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+))"),
        QRegularExpression::CaseInsensitiveOption);

    // Detect MCALL CYCLE81/82/83/84/85(...) lines
    static const QRegularExpression cycleRe(
        QStringLiteral("MCALL\\s+CYCLE(8[12345])\\s*\\("),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression fanucCycleRe(
        QStringLiteral("(?:^|\\s)G(73|76|8[123456])(?:\\s|$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression fanucCycleCancelRe(
        QStringLiteral("(?:^|\\s)G80(?:\\s|$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression fanucReturnInitialRe(
        QStringLiteral("(?:^|\\s)G98(?:\\s|$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression fanucReturnReferenceRe(
        QStringLiteral("(?:^|\\s)G99(?:\\s|$)"),
        QRegularExpression::CaseInsensitiveOption);

    Coordinate3D cur(0, 0, 0);
    int gModal = 0;
    int knownAxes = 7; // Preserve external G28 discontinuity handling.
    int positionKnownAxes = 0; // Zero is a display placeholder, never evidence of an actual start.
    bool incremental = false;
    ToolEntry currentTool;
    int currentToolId = -1;
    double currentToolDiameter = 0.0;
    QString currentToolModelPath;
    DrillCycle cycle;
    bool fanucReturnToInitial = true;

    const QStringList lines = gcode.split('\n');
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString &rawLine = lines[lineIndex];
        if (rawLine.trimmed().startsWith("//")) continue;
        QString line;
        // Siemens cycle parentheses are arguments, not RS274 comments.
        static const QRegularExpression numberedMCall(QStringLiteral("^\\s*(?:N\\s*\\d+\\s*)?MCALL\\b"),
                                                       QRegularExpression::CaseInsensitiveOption);
        if (numberedMCall.match(rawLine).hasMatch()) {
            line = rawLine.section(';', 0, 0).trimmed();
        } else {
            bool comment = false;
            for (QChar c : rawLine) {
                if (c == '(') { comment = true; line += ' '; }
                else if (c == ')') { comment = false; line += ' '; }
                else if (!comment) {
                    if (c == ';') break;
                    line += c;
                }
            }
            line = line.trimmed();
        }
        if (line.isEmpty()) continue;
        const auto fail=[&](const QString &reason){
            if(error)*error=QString("MOTION_UNSUPPORTED at NC line %1: %2").arg(lineIndex+1).arg(reason);
            return QVector<ToolMove>();
        };
        const bool mcall=numberedMCall.match(line).hasMatch();
        if (line.contains('#') || line.contains('[') || line.contains(']') || line.contains('='))
            return fail("unexpanded variables or expressions are not supported");
        if (mcall) {
            line.remove(QRegularExpression(QStringLiteral("^\\s*N\\s*\\d+\\s*"), QRegularExpression::CaseInsensitiveOption));
            if (line.compare(QStringLiteral("MCALL"), Qt::CaseInsensitive) != 0) {
                static const QRegularExpression literalCycle(QStringLiteral("^MCALL\\s+CYCLE8[12345]\\s*\\([^()]*\\)\\s*$"),
                    QRegularExpression::CaseInsensitiveOption);
                if (!literalCycle.match(line).hasMatch()) return fail("unexpanded or unsupported MCALL");
                const QString args = line.mid(line.indexOf('(')+1, line.lastIndexOf(')')-line.indexOf('(')-1);
                for (const QString &arg : args.split(',')) {
                    if (arg.trimmed().isEmpty()) continue;
                    bool ok = false; const double number = arg.trimmed().toDouble(&ok);
                    if (!ok || !std::isfinite(number)) return fail("cycle arguments must be finite numeric literals");
                }
            }
        } else {
            QString remainder = line;
            remainder.remove(wordRe);
            remainder.remove(QRegularExpression(QStringLiteral("[\\s%/]")));
            if (!remainder.isEmpty()) return fail("unexpanded control flow or nonnumeric words are not supported");
        }
        int commandedAxes=0;bool reference=false,nonzeroReference=false;
        auto modeWords=wordRe.globalMatch(line);
        while(!mcall && modeWords.hasNext()){
            const auto word=modeWords.next();const auto letter=word.captured(1).toUpper();
            bool numeric = false;
            const double value = word.captured(2).toDouble(&numeric);
            if (letter == "T") {
                if (!numeric || !std::isfinite(value) || value < 0 ||
                    value > std::numeric_limits<int>::max() || value != std::floor(value))
                    return fail("tool number must be a finite, representable nonnegative integer");
                currentToolId = static_cast<int>(value);
                if (useProjectTools) {
                    currentTool = ToolLibrary::instance().tool(static_cast<int>(value));
                    currentToolDiameter = currentTool.diameter;
                    currentToolModelPath = currentTool.modelPath;
                }
            }
            if (letter == "L" || (letter == "M" && (value == 17 || value == 97 || value == 98 || value == 99 || value == 198)))
                return fail("subprogram calls/returns require verified expanded motion");
            if (QStringLiteral("ABCUVW").contains(letter))
                return fail("additional-axis motion is not supported by the three-axis checker");
            if(letter=="X" || letter=="Y" || letter=="Z"){
                commandedAxes |= letter=="X"?1:letter=="Y"?2:4;
                nonzeroReference |= value!=0;
            }
            if(letter!="G")continue;
            if(value==90)incremental=false;
            if(value==91)incremental=true;
            if(value==28)reference=true;
            // Motion grammar is shared with CAM parsing, including its existing hole cycles.
            // Unsupported planes, units, centers and transforms cannot inherit a linear mode.
            static const QSet<double> supportedCodes={0,1,2,3,4,17,21,28,40,41,42,43,49,
                54,55,56,57,58,59,69,73,76,80,81,82,83,84,85,86,90,91,91.1,94,95,98,99};
            if(!supportedCodes.contains(value))
                return fail(QString("G%1 is not implemented by the motion parser").arg(value));
        }
        if(reference){
            if(!incremental || !commandedAxes || nonzeroReference)return fail("only zero-increment G91 G28 is supported");
            knownAxes &= ~commandedAxes;positionKnownAxes &= ~commandedAxes;cycle.active=false;continue;
        }
        if(incremental && commandedAxes)return fail("incremental XYZ motion is not implemented");
        const auto markMoves=[&](int first,int targetKnown,int axes){
            int fromKnown=positionKnownAxes;
            for(int i=first;i<moves.size();++i){
                moves[i].fromKnownAxes=fromKnown;moves[i].toKnownAxes=targetKnown;moves[i].commandedAxes=axes;fromKnown=targetKnown;
                moves[i].toolDiameter=currentToolDiameter;moves[i].fluteLength=currentTool.fluteLen;
                moves[i].totalLength=currentTool.totalLen;moves[i].shankDiameter=currentTool.shankDia;
                moves[i].toolId=currentToolId;
            }
            positionKnownAxes=targetKnown;
        };
        if (!mcall) {
            QStringList canonical;
            auto words = wordRe.globalMatch(line);
            while (words.hasNext()) {
                const auto word = words.next();
                const QString letter = word.captured(1).toUpper();
                canonical.append(letter + (letter == "G" ? QString::number(word.captured(2).toDouble(), 'g', 16) : word.captured(2)));
            }
            line = canonical.join(' ');
        }

        // --- MCALL (bare) closes an active cycle ---
        if (line.compare(QStringLiteral("MCALL"), Qt::CaseInsensitive) == 0) {
            cycle.active = false;
            continue;
        }

        if (fanucReturnInitialRe.match(line).hasMatch()) {
            fanucReturnToInitial = true;
        } else if (fanucReturnReferenceRe.match(line).hasMatch()) {
            fanucReturnToInitial = false;
        }

        // Explicit motion replaces a Fanuc canned cycle in every parsing mode.
        // Siemens MCALL remains active while its positioning blocks execute.
        if (cycle.active && !cycle.siemens) {
            auto words = wordRe.globalMatch(line);
            while (words.hasNext()) {
                const auto word = words.next();
                const double value = word.captured(2).toDouble();
                if (word.captured(1).compare("G", Qt::CaseInsensitive) == 0 &&
                    (value == 0 || value == 1 || value == 2 || value == 3))
                    cycle.active = false;
            }
        }
        if (fanucCycleCancelRe.match(line).hasMatch()) cycle.active = false;
        if (cycle.active && !cycle.siemens)
            cycle.rtp = fanucReturnToInitial ? cycle.initialPlane : cycle.rfp;

        // --- MCALL CYCLE83/82/81(...) opens a cycle ---
        auto cm = cycleRe.match(line);
        if (cm.hasMatch()) {
            const int kind = cm.captured(1).toInt();
            const QVector<double> a = parseCycleArgs(line);
            // Siemens CYCLE83 arg order: RTP,RFP,SDIS,DP,DPR,FDEP,FDPR,DAM,DTB,
            // DTS,FRF,VARI,_AXN,_MDEP,_VRT,_DTD,_DIS1.
            // CYCLE84:                  RTP,RFP,SDIS,DP,,DTB,SDAC,MPIT,PIT,POSS,SST,SST1
            // CYCLE85:                  RTP,RFP,SDIS,DP,,DTB,FFR,RFF
            // CYCLE82:                  RTP,RFP,SDIS,DP,,DTB
            // CYCLE81:                  RTP,RFP,SDIS,DP
            cycle.active = true;
            cycle.kind   = kind;
            cycle.siemens = true;
            cycle.rtp    = a.value(0, 5.0);
            cycle.rfp    = a.value(1, 0.0);
            cycle.sdis   = a.value(2, 2.0);
            cycle.dp     = a.value(3, -10.0);
            cycle.dtb    = kind == 83 ? a.value(8, 0.0) : a.value(5, 0.0);
            if (kind == 83) {
                cycle.mdep = a.value(6, 5.0); // FDPR is arg index 6
                cycle.vari = int(std::round(a.value(11, 1.0)));
                cycle.vrt = a.value(14, 0.0); // _VRT is arg index 14
                if (cycle.mdep <= 0) cycle.mdep = std::abs(cycle.dp - cycle.rfp);
            }
            continue;
        }

        // --- Fanuc/CQ8 modal hole cycle opens and processes its first position ---
        auto fm = fanucCycleRe.match(line);
        if (fm.hasMatch()) {
            double hx = cur.x(), hy = cur.y();
            double bottomZ = cycle.dp, returnZ = cycle.rfp, peckDepth = cycle.mdep;
            bool hasZ = false, hasR = false;
            auto it = wordRe.globalMatch(line);
            while (it.hasNext()) {
                const auto m = it.next();
                const QChar letter = m.captured(1).at(0).toUpper();
                const double val = m.captured(2).toDouble();
                if (letter == 'X') { hx = val; }
                // 函数说明：if用于完成当前模块的对应处理。
                else if (letter == 'Y') { hy = val; }
                // 函数说明：if用于完成当前模块的对应处理。
                else if (letter == 'Z') { bottomZ = val; hasZ = true; }
                // 函数说明：if用于完成当前模块的对应处理。
                else if (letter == 'R') { returnZ = val; hasR = true; }
                // 函数说明：if用于完成当前模块的对应处理。
                else if (letter == 'Q') { peckDepth = val; }
            }

            cycle.active = true;
            cycle.kind = fm.captured(1).toInt();
            cycle.siemens = false;
            cycle.vari = 1;
            cycle.vrt = 0.0;
            cycle.initialPlane = cur.z();
            cycle.rtp = fanucReturnToInitial ? cycle.initialPlane : returnZ;
            cycle.rfp = returnZ;
            cycle.sdis = 0.0;
            if (hasZ) cycle.dp = bottomZ;
            if (hasR) cycle.rfp = returnZ;
            if ((cycle.kind == 73 || cycle.kind == 83) && peckDepth > 0.0)
                cycle.mdep = peckDepth;
            if (useProjectTools || knownAxes == 7) {
                const int firstMove=moves.size();
                expandDrillHole(moves, cur, hx, hy, cycle, currentToolDiameter, currentToolModelPath, lineIndex);
                markMoves(firstMove,positionKnownAxes | commandedAxes | 4,commandedAxes | 4);
            }
            continue;
        }

        // --- If a cycle is active, an X/Y line is a hole position ---
        if (cycle.active) {
            double hx = cur.x(), hy = cur.y();
            bool hasX = false, hasY = false;
            auto it = wordRe.globalMatch(line);
            while (it.hasNext()) {
                const auto m = it.next();
                const QChar letter = m.captured(1).at(0).toUpper();
                const double val = m.captured(2).toDouble();
                if      (letter == 'X') { hx = val; hasX = true; }
                // 函数说明：if用于完成当前模块的对应处理。
                else if (letter == 'Y') { hy = val; hasY = true; }
                else if (letter == 'G' && (val == 0 || val == 1 || val == 2 || val == 3))
                    gModal = int(val);
            }
            if (gModal == 2 || gModal == 3)
                return fail("arc positioning during MCALL is not implemented");
            if (hasX || hasY) {
                const int firstMove=moves.size();
                expandDrillHole(moves, cur, hx, hy, cycle, currentToolDiameter, currentToolModelPath, lineIndex);
                markMoves(firstMove,positionKnownAxes | commandedAxes | 4,commandedAxes | 4);
                continue;
            }
        }

        // --- Standard G0/G1/G2/G3 word-address parsing ---
        double x = cur.x(), y = cur.y(), z = cur.z();
        double iOffset = 0.0, jOffset = 0.0;
        bool hasX = false, hasY = false, hasZ = false;
        bool hasI = false, hasJ = false, hasR = false, hasK = false;
        int gThisLine = -1;
        bool programEnd = false;

        auto it = wordRe.globalMatch(line);
        while (it.hasNext()) {
            const auto m = it.next();
            const QChar letter = m.captured(1).at(0).toUpper();
            const double val = m.captured(2).toDouble();
            if (letter == 'G') {
                if (val == 0 || val == 1 || val == 2 || val == 3) gThisLine = int(val);
            } else if (letter == 'M' && (val == 2 || val == 30)) {
                programEnd = true;
            } else if (letter == 'X') { x = val; hasX = true; }
            // 函数说明：if用于完成当前模块的对应处理。
            else if (letter == 'Y') { y = val; hasY = true; }
            // 函数说明：if用于完成当前模块的对应处理。
            else if (letter == 'Z') { z = val; hasZ = true; }
            // 函数说明：if用于完成当前模块的对应处理。
            else if (letter == 'I') { iOffset = val; hasI = true; }
            // 函数说明：if用于完成当前模块的对应处理。
            else if (letter == 'J') { jOffset = val; hasJ = true; }
            else if (letter == 'R') { hasR = true; }
            else if (letter == 'K') { hasK = true; }
        }

        if (programEnd) break;
        if (gThisLine >= 0) gModal = gThisLine;

        const bool hasMotion = hasX || hasY || hasZ || ((gModal == 2 || gModal == 3) && (hasI || hasJ || hasR || gThisLine>=0));
        if (!hasMotion) continue;
        if (gModal != 0 && gModal != 1 && gModal != 2 && gModal != 3) continue;

        Coordinate3D next{double(x), double(y), double(z)};
        if(gModal==2 || gModal==3){
            if(positionKnownAxes != 7)return fail("G2/G3 require a known XYZ start position");
            if(hasR || hasK || !(hasI || hasJ))return fail("G2/G3 require relative IJ center offsets in G17; R/K or missing center are unsupported");
            const double radius=std::hypot(iOffset,jOffset);
            const double endRadius=std::hypot(x-cur.x()-iOffset,y-cur.y()-jOffset);
            if(radius<=1e-6 || std::abs(endRadius-radius)>qMax(.002,radius*1e-5))return fail("invalid IJ arc center or inconsistent radius");
        }
        const int firstMove=moves.size();
        const int targetKnown=positionKnownAxes | commandedAxes;
        if (knownAxes != 7) {
            knownAxes |= (hasX ? 1 : 0) | (hasY ? 2 : 0) | (hasZ ? 4 : 0);
            cur = next;
            positionKnownAxes=targetKnown;
            continue; // Establish position without inventing an unknown start.
        }
        if ((gModal == 2 || gModal == 3) && (hasI || hasJ)) {
            if (!addArc(moves, cur, next, iOffset, jOffset, gModal == 2, currentToolDiameter, currentToolModelPath, lineIndex))
                return fail("arc center or sweep cannot be represented at the current coordinates");
        } else if (gModal == 0) {
            addRapid(moves, cur, next, currentToolDiameter, currentToolModelPath, lineIndex);
        } else {
            addFeed(moves, cur, next, currentToolDiameter, currentToolModelPath, lineIndex);
        }
        // Even an explicit zero coordinate establishes knowledge and must reach validation.
        if(moves.size()==firstMove && targetKnown!=positionKnownAxes){
            ToolMove move;move.from=cur;move.to=next;move.isRapid=gModal==0;move.sourceLine=lineIndex;moves.append(move);
        }
        markMoves(firstMove,targetKnown,commandedAxes);
    }
    return moves;
}

// Model-surface sampling and bottom checks only, not complete cutter/shank envelopes
// or material-removal simulation. An unfinished check never certifies collision freedom.
SimulationCheckResult SimulationController::checkMotion(const MeshData &mesh, const QVector<ToolMove> &moves)
{
    SimulationCheckResult result;
    if (moves.isEmpty()) return result;
    if (mesh.isEmpty()) {
        result.state = SimulationCheckState::Incomplete;
        result.message = QObject::tr("No model is available for geometric checks; trajectory playback only.");
        result.limitations.append(result.message);
        return result;
    }
    result.state = SimulationCheckState::Complete;
    auto limitation = [&](const QString &message, bool incomplete = true) {
        if (!result.limitations.contains(message)) result.limitations.append(message);
        if (incomplete) result.state = SimulationCheckState::Incomplete;
        else if (result.state == SimulationCheckState::Complete) result.state = SimulationCheckState::Limited;
    };
    auto collision = [&](const QString &message) {
        result.state = SimulationCheckState::Collision;
        result.message = message;
        return result;
    };

    QVector3D bbMin = mesh.bbMin;
    QVector3D bbMax = mesh.bbMax;
    if (bbMin == bbMax) {
        bbMin = QVector3D(std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max());
        bbMax = QVector3D(-std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max());
        for (const Triangle &tri : mesh.triangles) {
            const QVector3D points[] = {tri.v0, tri.v1, tri.v2};
            for (const QVector3D &p : points) {
                bbMin.setX(std::min(bbMin.x(), p.x()));
                bbMin.setY(std::min(bbMin.y(), p.y()));
                bbMin.setZ(std::min(bbMin.z(), p.z()));
                bbMax.setX(std::max(bbMax.x(), p.x()));
                bbMax.setY(std::max(bbMax.y(), p.y()));
                bbMax.setZ(std::max(bbMax.z(), p.z()));
            }
        }
    }

    const double stockTopZ = bbMax.z();
    const double stockBottomZ = bbMin.z();
    const double rapidClearance = 0.1;
    const double bottomTol = 5.0;
    const double xyMoveTol = 0.01;
    const double baryTol = 1.0e-6;
    const qint64 maxCollisionChecks = 12000000;
    qint64 estimatedCollisionChecks = 0;
    constexpr double collisionPi = 3.14159265358979323846;
    bool budgetExhausted = false;
    bool positionEstablished = false;
    bool checkedKnownMotion = false;
    bool hasProjectedSurface = false;
    for (const auto &tri : mesh.triangles) {
        const double den = (double(tri.v1.y())-tri.v2.y())*(double(tri.v0.x())-tri.v2.x())
            +(double(tri.v2.x())-tri.v1.x())*(double(tri.v0.y())-tri.v2.y());
        hasProjectedSurface |= std::isfinite(den) && std::abs(den) >= 1e-9;
    }
    if (!hasProjectedSurface)
        limitation(QObject::tr("The model has no usable XY surface coverage for rapid-motion checks."));

    auto meshTopZAtXY = [&](double x, double y, double &topZ) {
        bool found = false;
        double bestZ = -std::numeric_limits<double>::max();
        for (const Triangle &tri : mesh.triangles) {
            const double x0 = tri.v0.x();
            const double y0 = tri.v0.y();
            const double x1 = tri.v1.x();
            const double y1 = tri.v1.y();
            const double x2 = tri.v2.x();
            const double y2 = tri.v2.y();
            const double den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
            if (std::abs(den) < 1.0e-9) {
                continue;
            }

            const double a = ((y1 - y2) * (x - x2) + (x2 - x1) * (y - y2)) / den;
            const double b = ((y2 - y0) * (x - x2) + (x0 - x2) * (y - y2)) / den;
            const double c = 1.0 - a - b;
            if (a < -baryTol || b < -baryTol || c < -baryTol) {
                continue;
            }

            const double z = a * tri.v0.z() + b * tri.v1.z() + c * tri.v2.z();
            if (!found || z > bestZ) {
                bestZ = z;
                found = true;
            }
        }

        if (found) {
            topZ = bestZ;
        }
        return found;
    };

    for (const ToolMove &mv : moves) {
        const int line = mv.sourceLine >= 0 ? mv.sourceLine + 1 : 0;
        for (int axis = 0; axis < 3; ++axis) {
            if (((mv.fromKnownAxes & (1 << axis)) && !std::isfinite(mv.from[axis])) ||
                ((mv.toKnownAxes & (1 << axis)) && !std::isfinite(mv.to[axis]))) {
                result.state = SimulationCheckState::InvalidMotion;
                result.message = QObject::tr("Non-finite motion coordinate near G-code line %1.").arg(line);
                return result;
            }
        }
        // Even an incomplete XYZ segment can prove that a known endpoint is too deep.
        if (((mv.fromKnownAxes & 4) && mv.from.z() < stockBottomZ-bottomTol) ||
            ((mv.toKnownAxes & 4) && mv.to.z() < stockBottomZ-bottomTol))
            return collision(QObject::tr("Collision warning: tool path goes far below stock bottom near G-code line %1.").arg(line));
        if (mv.fromKnownAxes != 7 || mv.toKnownAxes != 7) {
            const bool initialSafePositioning = !positionEstablished && mv.isRapid &&
                (mv.toKnownAxes & 4) && mv.to.z() >= stockTopZ+rapidClearance;
            limitation(initialSafePositioning
                ? QObject::tr("Initial positioning starts at an unknown machine position; only subsequent known segments were checked.")
                : QObject::tr("Some motion lacks known XYZ endpoints and could not be checked."), !initialSafePositioning);
            positionEstablished |= mv.toKnownAxes == 7;
            continue;
        }
        positionEstablished = true;
        checkedKnownMotion = true;
        if (!std::isfinite(mv.toolDiameter) || mv.toolDiameter <= 0.02)
            limitation(QObject::tr("Tool diameter is missing or invalid; only tool-center samples are available."));
        const double dx = double(mv.to.x()) - mv.from.x();
        const double dy = double(mv.to.y()) - mv.from.y();
        const double dz = double(mv.to.z()) - mv.from.z();
        const double horizontalLength = std::hypot(dx, dy);
        const bool hasHorizontalMove = horizontalLength > xyMoveTol;
        const double moveLength = std::sqrt(dx * dx + dy * dy + dz * dz);

        // Linear Z stays between its endpoints; an entirely clear segment needs no surface samples.
        const bool aboveModel = std::min(mv.from.z(), mv.to.z()) >= stockTopZ + rapidClearance;
#ifdef CNEXT_ENABLE_OCC
        if (mv.isRapid && !aboveModel && dx == 0.0 && dy == 0.0 && dz > 0.0 && !mesh.sourceShape.IsNull()) {
            const auto evidence = checkExactRetraction(mesh, mv);
            if (evidence.state == RetractionEvidenceState::Interference)
                return collision(QObject::tr("Exact cutter/shank interference near G-code line %1: %2").arg(line).arg(evidence.message));
            if (evidence.state == RetractionEvidenceState::Uncertain)
                limitation(QObject::tr("Retract clearance is unproven near G-code line %1: %2").arg(line).arg(evidence.message));
            continue; // Later known depth violations still take precedence.
        }
#endif
        if (mv.isRapid && !aboveModel && moveLength > xyMoveTol && hasProjectedSurface && !budgetExhausted) {
            // With exactly fixed XY, the footprint sees the same surface throughout.
            const bool vertical = dx == 0.0 && dy == 0.0;
            const int samples = vertical ? 0 : qMax(2, int(std::ceil(std::min(moveLength / 2.0, 100.0))));
            QVector<QVector3D> footprintOffsets;
            footprintOffsets.append(QVector3D(0.0f, 0.0f, 0.0f));
            const double toolRadius = std::isfinite(mv.toolDiameter) && mv.toolDiameter > 0.0 ? mv.toolDiameter * 0.5 : 0.0;
            if (toolRadius > 0.01) {
                for (int j = 0; j < 8; ++j) {
                    const double a = 2.0 * collisionPi * double(j) / 8.0;
                    footprintOffsets.append(QVector3D(float(std::cos(a) * toolRadius),
                                                      float(std::sin(a) * toolRadius),
                                                      0.0f));
                }
            }
            estimatedCollisionChecks += qint64(samples + 1)
                                      * qint64(footprintOffsets.size())
                                      * qint64(mesh.triangles.size());
            if (estimatedCollisionChecks > maxCollisionChecks) {
                budgetExhausted = true;
                limitation(QObject::tr("Geometric check budget exceeded; remaining surface samples were not checked."));
                continue; // Keep checking every subsequent known Z endpoint.
            }

            for (int i = 0; i <= samples; ++i) {
                const double t = vertical ? (dz < 0.0 ? 1.0 : 0.0) : double(i) / double(samples);
                const double x = double(mv.from.x()) + dx * t;
                const double y = double(mv.from.y()) + dy * t;
                const double z = double(mv.from.z()) + dz * t;
                for (const QVector3D &offset : footprintOffsets) {
                    double localTopZ = stockTopZ;
                    if (!meshTopZAtXY(x + offset.x(), y + offset.y(), localTopZ)) {
                        continue;
                    }
                    if (z < localTopZ + rapidClearance) {
                        if (vertical && dz > 0.0 && z >= localTopZ) {
                            limitation(QObject::tr("A vertical retract starts at surface contact and moves away; clearance at its start is not established."), false);
                            continue;
                        }
                        if (hasHorizontalMove) {
                            return collision(QObject::tr("Collision warning: sampled tool footprint approaches the model surface near G-code line %1.").arg(line));
                        }
                        return collision(QObject::tr("Collision warning: rapid Z move approaches the model surface near G-code line %1.").arg(line));
                    }
                }
            }
        }

    }

    if (!checkedKnownMotion)
        limitation(QObject::tr("No fully known motion segment was available for geometric checks."));
    result.message = result.limitations.join(QLatin1Char('\n'));
    return result;
}
