#include "SimulationController.h"
#include "../tool/ToolLibrary.h"
#include <QtGlobal>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

SimulationController::SimulationController(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(50); // 20 Hz tick
    connect(m_timer, &QTimer::timeout, this, &SimulationController::onTick);
}

void SimulationController::setMesh(const MeshData &mesh)
{
    m_mesh = mesh;
}

void SimulationController::loadGCode(const QString &gcode)
{
    m_timer->stop();
    m_running = false;
    m_step = 0;
    m_gcode = gcode;
    m_moves = parseGCode(gcode);
    const QString collisionMessage = detectCollisionMessage(m_mesh, m_moves);
    emit collisionChecked(!collisionMessage.isEmpty(), collisionMessage);

    m_path.clear();
    m_rapidSegments.clear();
    for (const ToolMove &mv : m_moves) {
        if (m_path.isEmpty()) {
            m_path.append(mv.from);
        }
        m_path.append(mv.to);
        m_rapidSegments.append(mv.isRapid);
    }
    emit toolPathReady(m_path, m_rapidSegments);
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

void SimulationController::play()
{
    if (m_moves.isEmpty()) return;
    m_running = true;
    m_timer->start();
}

void SimulationController::pause()
{
    m_running = false;
    m_timer->stop();
}

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

void SimulationController::setSpeed(double multiplier)
{
    m_speed = multiplier > 0 ? multiplier : 1.0;
    // faster speed = shorter interval; base 50ms / multiplier, clamped 10-500ms
    int interval = qBound(10, int(50.0 / m_speed), 500);
    m_timer->setInterval(interval);
}

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
    emit toolPositionChanged(mv.to);
    emit progressChanged(double(target + 1) / double(m_moves.size()));
    emit currentGCodeLineChanged(mv.sourceLine);
}

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
    emit toolPositionChanged(mv.to);
    emit progressChanged(double(m_step + 1) / double(m_moves.size()));
    emit currentGCodeLineChanged(mv.sourceLine);
    ++m_step;
}

// ---------------------------------------------------------------------------
// G-code parser: extracts G0/G1/G2/G3 moves, tracks current X/Y/Z position.
// G2/G3 arcs are approximated as short line segments for visual simulation.
// Also expands Siemens CYCLE81/82/83/84/85 and Fanuc-style G81-G85 modal canned cycles.
// ---------------------------------------------------------------------------

namespace {

struct DrillCycle {
    bool   active = false;
    int    kind   = 83;    // 81, 82, 83, 84, or 85
    double rtp    = 5.0;   // retract plane (safe Z)
    double rfp    = 0.0;   // reference plane (top of workpiece)
    double sdis   = 2.0;   // safety clearance distance
    double dp     = -10.0; // final drill depth (absolute)
    double dtb    = 0.0;   // dwell at bottom (seconds)
    double mdep   = 5.0;   // max peck depth (CYCLE83 only)
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

static double normalizeSweep(double sweep, bool clockwise)
{
    if (clockwise) {
        while (sweep >= 0.0) sweep -= 2.0 * kPi;
    } else {
        while (sweep <= 0.0) sweep += 2.0 * kPi;
    }
    return sweep;
}

static void addMove(QVector<ToolMove> &moves, QVector3D &cur, QVector3D to,
                    bool isRapid, double toolDiameter, const QString &toolModelPath, int sourceLine)
{
    if ((to - cur).length() < 1e-4f) { cur = to; return; }
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
static void addRapid(QVector<ToolMove> &moves, QVector3D &cur, QVector3D to,
                     double toolDiameter, const QString &toolModelPath, int sourceLine)
{
    addMove(moves, cur, to, true, toolDiameter, toolModelPath, sourceLine);
}

// Append a feed move helper.
static void addFeed(QVector<ToolMove> &moves, QVector3D &cur, QVector3D to,
                    double toolDiameter, const QString &toolModelPath, int sourceLine)
{
    addMove(moves, cur, to, false, toolDiameter, toolModelPath, sourceLine);
}

static void addArc(QVector<ToolMove> &moves, QVector3D &cur,
                   QVector3D end, double iOffset, double jOffset,
                   bool clockwise, double toolDiameter, const QString &toolModelPath, int sourceLine)
{
    const double cx = cur.x() + iOffset;
    const double cy = cur.y() + jOffset;
    const double radius = std::hypot(cur.x() - cx, cur.y() - cy);
    if (radius <= 1.0e-6) {
        addFeed(moves, cur, end, toolDiameter, toolModelPath, sourceLine);
        return;
    }

    const double startAngle = std::atan2(cur.y() - cy, cur.x() - cx);
    const double endAngle = std::atan2(end.y() - cy, end.x() - cx);
    const bool fullCircle = std::hypot(double(end.x() - cur.x()), double(end.y() - cur.y())) < 1.0e-4;
    const double sweep = fullCircle
                             ? (clockwise ? -2.0 * kPi : 2.0 * kPi)
                             : normalizeSweep(endAngle - startAngle, clockwise);
    const int segments = qBound(12, int(std::ceil(std::abs(sweep) * radius / 1.0)), 256);
    const QVector3D start = cur;
    for (int s = 1; s <= segments; ++s) {
        const double t = double(s) / double(segments);
        const double a = startAngle + sweep * t;
        QVector3D p(float(cx + std::cos(a) * radius),
                    float(cy + std::sin(a) * radius),
                    float(start.z() + (end.z() - start.z()) * t));
        if (s == segments && !fullCircle) {
            p = end;
        }
        addFeed(moves, cur, p, toolDiameter, toolModelPath, sourceLine);
    }
}

// Expand one hole position (hx, hy) for the active drill cycle.
static void expandDrillHole(QVector<ToolMove> &moves, QVector3D &cur,
                             double hx, double hy, const DrillCycle &c,
                             double toolDiameter, const QString &toolModelPath, int sourceLine)
{
    const float safeZ  = float(c.rtp);
    const float clearZ = float(c.rfp + c.sdis);
    const float botZ   = float(c.dp);

    // 1. Rapid to XY at safe height
    addRapid(moves, cur, {float(hx), float(hy), safeZ}, toolDiameter, toolModelPath, sourceLine);
    // 2. Rapid down to clearance plane
    addRapid(moves, cur, {float(hx), float(hy), clearZ}, toolDiameter, toolModelPath, sourceLine);

    if (c.kind == 83) {
        // Peck drilling: feed down mdep at a time, retract to clearance, repeat
        double z = c.rfp + c.sdis;
        while (z > c.dp + 1e-6) {
            double next = z - c.mdep;
            if (next < c.dp) next = c.dp;
            addFeed(moves, cur, {float(hx), float(hy), float(next)}, toolDiameter, toolModelPath, sourceLine);
            if (next > c.dp + 1e-6) {
                addRapid(moves, cur, {float(hx), float(hy), clearZ}, toolDiameter, toolModelPath, sourceLine);
            }
            z = next;
        }
    } else {
        // CYCLE81/82/84/85: single feed to bottom.
        addFeed(moves, cur, {float(hx), float(hy), botZ}, toolDiameter, toolModelPath, sourceLine);
    }
    if (c.kind == 84 || c.kind == 85) {
        addFeed(moves, cur, {float(hx), float(hy), clearZ}, toolDiameter, toolModelPath, sourceLine);
    }
    // 3. Retract to safe height
    addRapid(moves, cur, {float(hx), float(hy), safeZ}, toolDiameter, toolModelPath, sourceLine);
}

} // namespace

QVector<ToolMove> SimulationController::parseGCode(const QString &gcode)
{
    QVector<ToolMove> moves;

    // Match word-address tokens like G0, X12.3, Z-5.0
    static const QRegularExpression wordRe(
        QStringLiteral("([A-Za-z])\\s*([+-]?\\d*\\.?\\d+)"),
        QRegularExpression::CaseInsensitiveOption);

    // Detect MCALL CYCLE81/82/83/84/85(...) lines
    static const QRegularExpression cycleRe(
        QStringLiteral("MCALL\\s+CYCLE(8[12345])\\s*\\("),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression fanucCycleRe(
        QStringLiteral("(?:^|\\s)G(8[12345])(?:\\s|$)"),
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

    QVector3D cur(0, 0, 0);
    int gModal = 0;
    double currentToolDiameter = 0.0;
    QString currentToolModelPath;
    DrillCycle cycle;
    bool fanucReturnToInitial = true;

    const QStringList lines = gcode.split('\n');
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString &rawLine = lines[lineIndex];
        const QString line = rawLine.section(';', 0, 0).trimmed();
        if (line.isEmpty()) continue;

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

        if (fanucCycleCancelRe.match(line).hasMatch()) {
            cycle.active = false;
            continue;
        }

        // --- MCALL CYCLE83/82/81(...) opens a cycle ---
        auto cm = cycleRe.match(line);
        if (cm.hasMatch()) {
            const int kind = cm.captured(1).toInt();
            const QVector<double> a = parseCycleArgs(line);
            // Siemens CYCLE83 arg order: RTP,RFP,SDIS,DP,,DTB,,FRF,VARI,,MDEP,...
            // CYCLE84:                  RTP,RFP,SDIS,DP,,DTB,SDAC,MPIT,PIT,POSS,SST,SST1
            // CYCLE85:                  RTP,RFP,SDIS,DP,,DTB,FFR,RFF
            // CYCLE82:                  RTP,RFP,SDIS,DP,,DTB
            // CYCLE81:                  RTP,RFP,SDIS,DP
            cycle.active = true;
            cycle.kind   = kind;
            cycle.rtp    = a.value(0, 5.0);
            cycle.rfp    = a.value(1, 0.0);
            cycle.sdis   = a.value(2, 2.0);
            cycle.dp     = a.value(3, -10.0);
            cycle.dtb    = a.value(5, 0.0);
            if (kind == 83) {
                cycle.mdep = a.value(10, 5.0); // MDEP is arg index 10
                if (cycle.mdep <= 0) cycle.mdep = std::abs(cycle.dp - cycle.rfp);
            }
            continue;
        }

        // --- Fanuc/CQ8 G81-G85 opens a modal canned cycle and drills its first hole ---
        auto fm = fanucCycleRe.match(line);
        if (fm.hasMatch()) {
            double hx = cur.x(), hy = cur.y();
            double bottomZ = cycle.dp, returnZ = cycle.rfp, peckDepth = cycle.mdep;
            bool hasX = false, hasY = false, hasZ = false, hasR = false;
            auto it = wordRe.globalMatch(line);
            while (it.hasNext()) {
                const auto m = it.next();
                const QChar letter = m.captured(1).at(0).toUpper();
                const double val = m.captured(2).toDouble();
                if (letter == 'T') {
                    const ToolEntry tool = ToolLibrary::instance().tool(int(val));
                    if (tool.diameter > 0.0) currentToolDiameter = tool.diameter;
                    currentToolModelPath = tool.modelPath;
                } else if (letter == 'X') { hx = val; hasX = true; }
                else if (letter == 'Y') { hy = val; hasY = true; }
                else if (letter == 'Z') { bottomZ = val; hasZ = true; }
                else if (letter == 'R') { returnZ = val; hasR = true; }
                else if (letter == 'Q') { peckDepth = val; }
            }

            cycle.active = true;
            cycle.kind = fm.captured(1).toInt();
            cycle.rtp = fanucReturnToInitial ? cur.z() : returnZ;
            cycle.rfp = returnZ;
            cycle.sdis = 0.0;
            if (hasZ) cycle.dp = bottomZ;
            if (hasR) cycle.rfp = returnZ;
            if (cycle.kind == 83 && peckDepth > 0.0) cycle.mdep = peckDepth;
            if (hasX || hasY) {
                expandDrillHole(moves, cur, hx, hy, cycle, currentToolDiameter, currentToolModelPath, lineIndex);
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
                if (letter == 'T') {
                    const ToolEntry tool = ToolLibrary::instance().tool(int(val));
                    if (tool.diameter > 0.0) currentToolDiameter = tool.diameter;
                    currentToolModelPath = tool.modelPath;
                }
                if      (letter == 'X') { hx = val; hasX = true; }
                else if (letter == 'Y') { hy = val; hasY = true; }
            }
            if (hasX || hasY) {
                expandDrillHole(moves, cur, hx, hy, cycle, currentToolDiameter, currentToolModelPath, lineIndex);
                continue;
            }
        }

        // --- Standard G0/G1/G2/G3 word-address parsing ---
        double x = cur.x(), y = cur.y(), z = cur.z();
        double iOffset = 0.0, jOffset = 0.0;
        bool hasX = false, hasY = false, hasZ = false;
        bool hasI = false, hasJ = false;
        int gThisLine = -1;

        auto it = wordRe.globalMatch(line);
        while (it.hasNext()) {
            const auto m = it.next();
            const QChar letter = m.captured(1).at(0).toUpper();
            const double val = m.captured(2).toDouble();
            if (letter == 'G') {
                int g = int(val);
                if (g == 0 || g == 1 || g == 2 || g == 3) gThisLine = g;
            } else if (letter == 'T') {
                const ToolEntry tool = ToolLibrary::instance().tool(int(val));
                if (tool.diameter > 0.0) currentToolDiameter = tool.diameter;
                currentToolModelPath = tool.modelPath;
            } else if (letter == 'X') { x = val; hasX = true; }
            else if (letter == 'Y') { y = val; hasY = true; }
            else if (letter == 'Z') { z = val; hasZ = true; }
            else if (letter == 'I') { iOffset = val; hasI = true; }
            else if (letter == 'J') { jOffset = val; hasJ = true; }
        }

        if (gThisLine >= 0) gModal = gThisLine;

        const bool hasMotion = hasX || hasY || hasZ;
        if (!hasMotion) continue;
        if (gModal != 0 && gModal != 1 && gModal != 2 && gModal != 3) continue;

        QVector3D next{float(x), float(y), float(z)};
        if ((gModal == 2 || gModal == 3) && (hasI || hasJ)) {
            addArc(moves, cur, next, iOffset, jOffset, gModal == 2, currentToolDiameter, currentToolModelPath, lineIndex);
        } else if (gModal == 0) {
            addRapid(moves, cur, next, currentToolDiameter, currentToolModelPath, lineIndex);
        } else {
            addFeed(moves, cur, next, currentToolDiameter, currentToolModelPath, lineIndex);
        }
    }
    return moves;
}

QString SimulationController::detectCollisionMessage(const MeshData &mesh, const QVector<ToolMove> &moves)
{
    if (mesh.isEmpty() || moves.isEmpty()) {
        return QString();
    }

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
        const double dx = double(mv.to.x() - mv.from.x());
        const double dy = double(mv.to.y() - mv.from.y());
        const double dz = double(mv.to.z() - mv.from.z());
        const double horizontalLength = std::hypot(dx, dy);
        const bool hasHorizontalMove = horizontalLength > xyMoveTol;
        const double moveLength = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double lowZ = std::min(double(mv.from.z()), double(mv.to.z()));
        const int line = mv.sourceLine >= 0 ? mv.sourceLine + 1 : 0;

        if (mv.isRapid && moveLength > xyMoveTol) {
            const int samples = qBound(2, int(std::ceil(moveLength / 2.0)), 100);
            QVector<QVector3D> footprintOffsets;
            footprintOffsets.append(QVector3D(0.0f, 0.0f, 0.0f));
            const double toolRadius = mv.toolDiameter > 0.0 ? mv.toolDiameter * 0.5 : 0.0;
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
                return QString();
            }

            for (int i = 0; i <= samples; ++i) {
                const double t = double(i) / double(samples);
                const double x = double(mv.from.x()) + dx * t;
                const double y = double(mv.from.y()) + dy * t;
                const double z = double(mv.from.z()) + dz * t;
                for (const QVector3D &offset : footprintOffsets) {
                    double localTopZ = stockTopZ;
                    if (!meshTopZAtXY(x + offset.x(), y + offset.y(), localTopZ)) {
                        continue;
                    }
                    if (z < localTopZ + rapidClearance) {
                        if (hasHorizontalMove) {
                            return QObject::tr("Collision warning: rapid move brings the tool envelope too close to stock near G-code line %1.").arg(line);
                        }
                        return QObject::tr("Collision warning: rapid Z move enters the stock envelope near G-code line %1.").arg(line);
                    }
                }
            }
        }

        if (lowZ < stockBottomZ - bottomTol) {
            return QObject::tr("Collision warning: tool path goes far below stock bottom near G-code line %1.").arg(line);
        }
    }

    return QString();
}
