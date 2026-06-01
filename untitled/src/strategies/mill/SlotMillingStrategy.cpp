#include "SlotMillingStrategy.h"
#include "SlotMachiningGeometry.h"

#include <QObject>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

static QString rapidXY(double x, double y)
{
    return QStringLiteral("G0 X%1 Y%2\n").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3);
}

static QString feedXY(double x, double y, double feed)
{
    return QStringLiteral("G1 X%1 Y%2 F%3\n")
        .arg(x, 0, 'f', 3)
        .arg(y, 0, 'f', 3)
        .arg(int(feed));
}

static QString feedXYZ(double x, double y, double z, double feed)
{
    return QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
        .arg(x, 0, 'f', 3)
        .arg(y, 0, 'f', 3)
        .arg(z, 0, 'f', 3)
        .arg(int(feed));
}

static std::vector<double> roughWidthOffsets(double centerLimit, double stepover, double toolDiameter)
{
    std::vector<double> offsets;
    if (centerLimit <= 1.0e-6) {
        offsets.push_back(0.0);
        return offsets;
    }

    const double effectiveStep = std::max(0.1, std::min(stepover, toolDiameter * 0.45));
    offsets.push_back(0.0);
    for (double v = effectiveStep; v < centerLimit + 1.0e-6; v += effectiveStep) {
        const double clamped = std::min(v, centerLimit);
        offsets.push_back(-clamped);
        offsets.push_back(clamped);
    }
    offsets.push_back(-centerLimit);
    offsets.push_back(centerLimit);

    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end(), [](double a, double b) {
        return std::abs(a - b) <= 1.0e-6;
    }), offsets.end());
    return offsets;
}

static std::vector<double> finishBottomOffsets(double centerLimit, double stepover, double toolDiameter)
{
    std::vector<double> offsets;
    if (centerLimit <= 1.0e-6) {
        offsets.push_back(0.0);
        return offsets;
    }

    const double effectiveStep = std::max(0.1, std::min(stepover * 0.5, toolDiameter * 0.35));
    offsets.push_back(-centerLimit);
    offsets.push_back(0.0);
    offsets.push_back(centerLimit);
    for (double v = -centerLimit + effectiveStep; v < centerLimit - 1.0e-6; v += effectiveStep) {
        offsets.push_back(v);
    }

    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end(), [](double a, double b) {
        return std::abs(a - b) <= 1.0e-6;
    }), offsets.end());
    return offsets;
}

static double localDistance(double u0, double v0, double u1, double v1)
{
    const double du = u1 - u0;
    const double dv = v1 - v0;
    return std::sqrt(du * du + dv * dv);
}

static void openSlotLineRange(const SlotMachiningGeometry &geometry,
                              double layerDepth,
                              double &startU,
                              double &endU)
{
    const bool forward = geometry.openSign < 0.0;
    const double dir = forward ? 1.0 : -1.0;
    const double rawStartU = forward ? geometry.roughMinU : geometry.roughMaxU;
    const double rawEndU = forward ? geometry.roughMaxU : geometry.roughMinU;
    const double startSlopeLen = forward ? geometry.slopeStartLength : geometry.slopeEndLength;
    const double endSlopeLen = forward ? geometry.slopeEndLength : geometry.slopeStartLength;
    const double depthRatio = geometry.depth > 1.0e-6
                                  ? std::max(0.0, std::min(layerDepth / geometry.depth, 1.0))
                                  : 1.0;

    startU = rawStartU + dir * startSlopeLen * depthRatio;
    endU = rawEndU - dir * endSlopeLen * depthRatio;
    if (dir * (endU - startU) < 0.0) {
        const double midU = (rawStartU + rawEndU) * 0.5;
        startU = midU;
        endU = midU;
    }
}

static bool validateOpenSlotRoughOffsets(const SlotMachiningGeometry &geometry,
                                         const std::vector<double> &offsets,
                                         double layerDepth,
                                         QString &errorMsg)
{
    const double tolerance = 0.05;
    double startU = 0.0;
    double endU = 0.0;
    openSlotLineRange(geometry, layerDepth, startU, endU);
    const double maxV = geometry.roughHalfWidth + tolerance;

    for (double v : offsets) {
        if (std::abs(v) > maxV) {
            errorMsg = QObject::tr("Open-slot roughing offset exceeds the effective slot half-width.");
            return false;
        }
    }
    if (startU < geometry.roughMinU - tolerance || startU > geometry.roughMaxU + tolerance ||
        endU < geometry.roughMinU - tolerance || endU > geometry.roughMaxU + tolerance) {
        errorMsg = QObject::tr("Open-slot roughing line range exceeds the effective slot length.");
        return false;
    }

    return true;
}

static bool appendOpenSlotRoughLayer(QString &gcode,
                                     const SlotMachiningGeometry &geometry,
                                     const std::vector<double> &offsets,
                                     double zLayer,
                                     double layerDepth,
                                     double safe,
                                     double feedHeight,
                                     double feedRate,
                                     double plungeRate,
                                     double &totalLength,
                                     QString &errorMsg)
{
    if (offsets.empty()) {
        return true;
    }

    if (!validateOpenSlotRoughOffsets(geometry, offsets, layerDepth, errorMsg)) {
        return false;
    }

    double startU = 0.0;
    double endU = 0.0;
    openSlotLineRange(geometry, layerDepth, startU, endU);
    if (std::abs(endU - startU) <= 1.0e-6) {
        return true;
    }

    gcode += QStringLiteral("; Open slot rough layer Z=%1 U[%2,%3] halfW=%4\n")
                 .arg(zLayer, 0, 'f', 3)
                 .arg(std::min(startU, endU), 0, 'f', 3)
                 .arg(std::max(startU, endU), 0, 'f', 3)
                 .arg(geometry.roughHalfWidth, 0, 'f', 3);

    bool firstLine = true;
    bool forward = true;
    for (double v : offsets) {
        const double lineStartU = forward ? startU : endU;
        const double lineEndU = forward ? endU : startU;

        double x0 = 0.0;
        double y0 = 0.0;
        double x1 = 0.0;
        double y1 = 0.0;
        slotLocalToWorld(geometry, lineStartU, v, x0, y0);
        slotLocalToWorld(geometry, lineEndU, v, x1, y1);

        if (firstLine) {
            gcode += rapidXY(x0, y0);
            gcode += QStringLiteral("G0 Z%1\n").arg(geometry.zTop + feedHeight, 0, 'f', 3);
            gcode += QStringLiteral("G1 Z%1 F%2\n").arg(zLayer, 0, 'f', 3).arg(int(plungeRate));
            firstLine = false;
        } else {
            gcode += feedXY(x0, y0, feedRate);
        }

        gcode += feedXY(x1, y1, feedRate);
        totalLength += localDistance(lineStartU, v, lineEndU, v);
        forward = !forward;
    }

    gcode += QStringLiteral("G1 Z%1 F%2\n").arg(geometry.zTop, 0, 'f', 3).arg(int(plungeRate));
    gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
    return true;
}

static void appendOpenSlotBottomFinish(QString &gcode,
                                       const SlotMachiningGeometry &geometry,
                                       double zLayer,
                                       double safe,
                                       double feedHeight,
                                       double stepover,
                                       double feedRate,
                                       double plungeRate,
                                       double toolDiameter,
                                       double &totalLength)
{
    const double startU = geometry.bottomStartU;
    const double endU = geometry.bottomEndU;
    const double centerLimit = geometry.halfWidth - toolDiameter * 0.5;
    if (endU <= startU || centerLimit <= 1.0e-6) {
        return;
    }

    const std::vector<double> offsets = finishBottomOffsets(centerLimit, stepover, toolDiameter);
    bool firstLine = true;
    bool forward = true;
    for (double v : offsets) {
        const double lineStartU = forward ? startU : endU;
        const double lineEndU = forward ? endU : startU;
        double x0 = 0.0;
        double y0 = 0.0;
        double x1 = 0.0;
        double y1 = 0.0;
        slotLocalToWorld(geometry, lineStartU, v, x0, y0);
        slotLocalToWorld(geometry, lineEndU, v, x1, y1);

        if (firstLine) {
            gcode += rapidXY(x0, y0);
            gcode += QStringLiteral("G0 Z%1\n").arg(geometry.zTop + feedHeight, 0, 'f', 3);
            gcode += QStringLiteral("G1 Z%1 F%2\n").arg(zLayer, 0, 'f', 3).arg(int(plungeRate));
            firstLine = false;
        } else {
            gcode += feedXY(x0, y0, feedRate);
        }

        gcode += feedXY(x1, y1, feedRate);
        totalLength += localDistance(lineStartU, v, lineEndU, v);
        forward = !forward;
    }

    gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
}

static void appendOpenSlotSlopeSection(QString &gcode,
                                       const SlotMachiningGeometry &geometry,
                                       double fromU,
                                       double toU,
                                       double fromZ,
                                       double toZ,
                                       double safe,
                                       double feedHeight,
                                       double stepover,
                                       double feedRate,
                                       double plungeRate,
                                       double toolDiameter,
                                       double &totalLength)
{
    const double centerLimit = geometry.halfWidth - toolDiameter * 0.5;
    if (std::abs(toU - fromU) <= 1.0e-6 || centerLimit <= 1.0e-6) {
        return;
    }

    const std::vector<double> offsets = finishBottomOffsets(centerLimit, stepover, toolDiameter);
    bool forward = true;
    for (double v : offsets) {
        const double u0 = forward ? fromU : toU;
        const double u1 = forward ? toU : fromU;
        const double z0 = forward ? fromZ : toZ;
        const double z1 = forward ? toZ : fromZ;

        double x0 = 0.0;
        double y0 = 0.0;
        double x1 = 0.0;
        double y1 = 0.0;
        slotLocalToWorld(geometry, u0, v, x0, y0);
        slotLocalToWorld(geometry, u1, v, x1, y1);

        gcode += rapidXY(x0, y0);
        gcode += QStringLiteral("G0 Z%1\n").arg(geometry.zTop + feedHeight, 0, 'f', 3);
        gcode += QStringLiteral("G1 Z%1 F%2\n").arg(z0, 0, 'f', 3).arg(int(plungeRate));
        gcode += feedXYZ(x1, y1, z1, feedRate);
        gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);

        const double du = u1 - u0;
        const double dz = z1 - z0;
        totalLength += std::sqrt(du * du + dz * dz);
        forward = !forward;
    }
}

static void appendOpenSlotSlopeFinish(QString &gcode,
                                      const SlotMachiningGeometry &geometry,
                                      double bottomZ,
                                      double safe,
                                      double feedHeight,
                                      double stepover,
                                      double feedRate,
                                      double plungeRate,
                                      double toolDiameter,
                                      double &totalLength)
{
    const double minSlopeLength = std::max(0.05, toolDiameter * 0.05);

    if (geometry.slopeStartLength > minSlopeLength &&
        geometry.bottomStartU > geometry.roughMinU + minSlopeLength) {
        gcode += QStringLiteral("; Open slot start slope finish\n");
        appendOpenSlotSlopeSection(gcode,
                                   geometry,
                                   geometry.roughMinU,
                                   geometry.bottomStartU,
                                   geometry.zTop,
                                   bottomZ,
                                   safe,
                                   feedHeight,
                                   stepover,
                                   feedRate,
                                   plungeRate,
                                   toolDiameter,
                                   totalLength);
    }

    if (geometry.slopeEndLength > minSlopeLength &&
        geometry.roughMaxU > geometry.bottomEndU + minSlopeLength) {
        gcode += QStringLiteral("; Open slot end slope finish\n");
        appendOpenSlotSlopeSection(gcode,
                                   geometry,
                                   geometry.bottomEndU,
                                   geometry.roughMaxU,
                                   bottomZ,
                                   geometry.zTop,
                                   safe,
                                   feedHeight,
                                   stepover,
                                   feedRate,
                                   plungeRate,
                                   toolDiameter,
                                   totalLength);
    }
}

static bool validateOpenSlotGeneratedGCode(const QString &gcode,
                                           const SlotMachiningGeometry &geometry,
                                           double feedHeight,
                                           double leadLength,
                                           QString &errorMsg)
{
    static const QRegularExpression wordRe(
        QStringLiteral("([A-Za-z])\\s*([+-]?\\d*\\.?\\d+)"),
        QRegularExpression::CaseInsensitiveOption);

    const double tolerance = 0.05;
    const double clearanceZ = geometry.zTop + feedHeight - tolerance;
    const double uMin = geometry.openSign < 0.0
                            ? geometry.roughMinU - leadLength - tolerance
                            : geometry.roughMinU - tolerance;
    const double uMax = geometry.openSign < 0.0
                            ? geometry.roughMaxU + tolerance
                            : geometry.roughMaxU + leadLength + tolerance;
    const double vMax = geometry.halfWidth + tolerance;
    const double zMin = geometry.zTop - geometry.depth - tolerance;

    double x = geometry.cx;
    double y = geometry.cy;
    double z = geometry.zTop;
    int modalG = 0;

    const QStringList lines = gcode.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].section(QLatin1Char(';'), 0, 0).trimmed();
        if (line.isEmpty()) {
            continue;
        }

        double nextX = x;
        double nextY = y;
        double nextZ = z;
        bool hasX = false;
        bool hasY = false;
        bool hasZ = false;
        int lineG = -1;

        QRegularExpressionMatchIterator it = wordRe.globalMatch(line);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const QChar letter = match.captured(1).at(0).toUpper();
            const double value = match.captured(2).toDouble();
            if (letter == QLatin1Char('G')) {
                const int g = int(value);
                if (g == 0 || g == 1) {
                    lineG = g;
                }
            } else if (letter == QLatin1Char('X')) {
                nextX = value;
                hasX = true;
            } else if (letter == QLatin1Char('Y')) {
                nextY = value;
                hasY = true;
            } else if (letter == QLatin1Char('Z')) {
                nextZ = value;
                hasZ = true;
            }
        }

        if (lineG >= 0) {
            modalG = lineG;
        }
        if (!hasX && !hasY && !hasZ) {
            continue;
        }

        const bool hasXY = hasX || hasY;
        if (modalG == 0 && hasXY && std::max(z, nextZ) < clearanceZ) {
            errorMsg = QObject::tr("Open-slot rapid XY move is below the feed start height near G-code line %1.")
                           .arg(i + 1);
            return false;
        }

        if (hasXY || hasZ) {
            const double relX = nextX - geometry.cx;
            const double relY = nextY - geometry.cy;
            const double u = relX * geometry.cosA + relY * geometry.sinA;
            const double v = -relX * geometry.sinA + relY * geometry.cosA;

            if (std::abs(v) > vMax || u < uMin || u > uMax) {
                errorMsg = QObject::tr("Open-slot tool center is outside the allowed slot range near G-code line %1.")
                               .arg(i + 1);
                return false;
            }
            if (nextZ < zMin) {
                errorMsg = QObject::tr("Open-slot Z position is outside the allowed slot depth near G-code line %1.")
                               .arg(i + 1);
                return false;
            }
        }

        x = nextX;
        y = nextY;
        z = nextZ;
    }

    return true;
}

} // namespace

StrategyParams SlotMillingStrategy::defaultParams() const
{
    StrategyParams p;
    p.set(QStringLiteral("safeHeight"),   50.0);
    p.set(QStringLiteral("feedHeight"),    3.0);
    p.set(QStringLiteral("depth"),        10.0);
    p.set(QStringLiteral("stepDown"),      2.0);
    p.set(QStringLiteral("stepover"),      5.0);
    p.set(QStringLiteral("spindleSpeed"), 2000.0);
    p.set(QStringLiteral("feedRate"),      800.0);
    p.set(QStringLiteral("plungeRate"),    200.0);
    p.set(QStringLiteral("stockToLeave"),  0.1);
    p.set(QStringLiteral("compensation"),  0.0);
    p.set(QStringLiteral("slotLength"),   50.0);
    p.set(QStringLiteral("slotWidth"),    10.0);
    p.set(QStringLiteral("angle"),         0.0);
    p.set(QStringLiteral("helixRadius"),   0.0);
    p.set(QStringLiteral("helixPitch"),    0.0);
    return p;
}

ToolpathResult SlotMillingStrategy::generate(const HoleFeature &,
                                             const ToolEntry &,
                                             const StrategyParams &) const
{
    return {QString(), false, QObject::tr("Slot milling requires a contour feature."), 0.0};
}

ToolpathResult SlotMillingStrategy::generate(const ContourFeature &feature,
                                             const ToolEntry      &tool,
                                             const StrategyParams &params) const
{
    ToolpathResult res;

    if (tool.diameter <= 0.0) {
        res.errorMsg = QObject::tr("The selected tool diameter is invalid.");
        return res;
    }
    if (feature.depth <= 0.0) {
        res.errorMsg = QObject::tr("Slot depth must be greater than 0.");
        return res;
    }
    if (feature.subType != QStringLiteral("open_slot")) {
        res.errorMsg = QObject::tr("Open-slot milling can only be used for slots without a bottom face. Use blind-slot milling for bottomed slots.");
        return res;
    }

    const SlotMachiningGeometry geometry = buildSlotMachiningGeometry(feature, tool, params);
    const QString geometryError = validateSlotMachiningGeometry(geometry, tool, params);
    if (!geometryError.isEmpty()) {
        res.errorMsg = geometryError;
        return res;
    }

    const double safe = params.get(QStringLiteral("safeHeight"), 50.0);
    const double feedH = params.get(QStringLiteral("feedHeight"), 3.0);
    const double stepDown = params.get(QStringLiteral("stepDown"), 2.0);
    const double stepover = params.get(QStringLiteral("stepover"), 5.0);
    const double spindleSpeed = params.get(QStringLiteral("spindleSpeed"), 2000.0);
    const double feedRate = params.get(QStringLiteral("feedRate"), 800.0);
    const double plungeRate = params.get(QStringLiteral("plungeRate"), 200.0);
    const double comp = params.get(QStringLiteral("compensation"), 0.0);

    const bool camOffset = std::abs(comp) < 1.0e-9;
    const int layerCount = static_cast<int>(std::ceil(feature.depth / stepDown));
    const double leadLength = std::max(tool.diameter, 5.0);

    if ((camOffset ? geometry.roughHalfWidth : geometry.halfWidth) <= 0.0) {
        res.errorMsg = QObject::tr("The tool diameter or stock to leave is too large for slot finishing.");
        return res;
    }

    QString gcode;
    gcode += QStringLiteral("T%1 M6\n").arg(tool.id);
    gcode += QStringLiteral("S%1 M3\n").arg(int(spindleSpeed));
    gcode += QStringLiteral("; Open slot geometry: L=%1 W=%2 D=%3 A=%4\n")
                 .arg(geometry.fullLength, 0, 'f', 3)
                 .arg(geometry.fullWidth, 0, 'f', 3)
                 .arg(geometry.depth, 0, 'f', 3)
                 .arg(geometry.angleRad * 180.0 / std::acos(-1.0), 0, 'f', 3);
    gcode += QStringLiteral("; Open slot rough range: U[%1,%2] halfW=%3 openSign=%4\n")
                 .arg(geometry.roughMinU, 0, 'f', 3)
                 .arg(geometry.roughMaxU, 0, 'f', 3)
                 .arg(geometry.roughHalfWidth, 0, 'f', 3)
                 .arg(geometry.openSign, 0, 'f', 1);
    gcode += QStringLiteral("; Open slot slopes: startU=%1 endU=%2 bottomU[%3,%4]\n")
                 .arg(geometry.slopeStartLength, 0, 'f', 3)
                 .arg(geometry.slopeEndLength, 0, 'f', 3)
                 .arg(geometry.bottomStartU, 0, 'f', 3)
                 .arg(geometry.bottomEndU, 0, 'f', 3);
    gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);

    double totalLength = 0.0;

    for (int layer = 1; layer <= layerCount; ++layer) {
        const double zLayer = geometry.zTop - std::min(layer * stepDown, feature.depth);

        if (geometry.roughMaxU > geometry.roughMinU && geometry.roughHalfWidth > 0.0) {
            const std::vector<double> offsets = roughWidthOffsets(geometry.roughHalfWidth, stepover, tool.diameter);
            const double layerDepth = geometry.zTop - zLayer;
            QString roughError;
            if (!appendOpenSlotRoughLayer(gcode,
                                          geometry,
                                          offsets,
                                          zLayer,
                                          layerDepth,
                                          safe,
                                          feedH,
                                          feedRate,
                                          plungeRate,
                                          totalLength,
                                          roughError)) {
                res.errorMsg = roughError.isEmpty()
                                   ? QObject::tr("Open-slot roughing range validation failed.")
                                   : roughError;
                return res;
            }
        }

        if (layer < layerCount) {
            continue;
        }

        gcode += QStringLiteral("; Open slot bottom finish\n");
        appendOpenSlotBottomFinish(gcode,
                                   geometry,
                                   zLayer,
                                   safe,
                                   feedH,
                                   stepover,
                                   feedRate,
                                   plungeRate,
                                   tool.diameter,
                                   totalLength);

        appendOpenSlotSlopeFinish(gcode,
                                  geometry,
                                  zLayer,
                                  safe,
                                  feedH,
                                  stepover,
                                  feedRate,
                                  plungeRate,
                                  tool.diameter,
                                  totalLength);

        const bool forward = geometry.openSign < 0.0;
        const double cutDir = forward ? 1.0 : -1.0;
        const double wallStartU = geometry.bottomStartU;
        const double wallEndU = geometry.bottomEndU;
        const double openU = forward ? wallStartU : wallEndU;
        const double closedU = forward ? wallEndU : wallStartU;
        const double finishWallV = camOffset ? geometry.halfWidth - tool.diameter * 0.5
                                             : geometry.halfWidth;
        if (finishWallV <= 1.0e-6 || wallEndU <= wallStartU) {
            continue;
        }

        const double leadStartU = openU - cutDir * leadLength;
        double leadX = 0.0;
        double leadY = 0.0;
        slotLocalToWorld(geometry, leadStartU, -finishWallV, leadX, leadY);

        gcode += QStringLiteral("; Open slot side wall finish\n");
        gcode += rapidXY(leadX, leadY);
        gcode += QStringLiteral("G0 Z%1\n").arg(geometry.zTop + feedH, 0, 'f', 3);
        gcode += QStringLiteral("G1 Z%1 F%2\n").arg(zLayer, 0, 'f', 3).arg(int(plungeRate));
        if (camOffset) {
            gcode += QStringLiteral("G40\n");
        } else {
            const QString compCode = comp > 0.0 ? QStringLiteral("G41") : QStringLiteral("G42");
            gcode += QStringLiteral("%1 D%2\n").arg(compCode).arg(tool.id);
        }

        const struct { double u; double v; } points[5] = {
            {openU, -finishWallV},
            {closedU, -finishWallV},
            {closedU,  finishWallV},
            {openU,  finishWallV},
            {leadStartU, finishWallV}
        };
        for (const auto &pt : points) {
            double x = 0.0;
            double y = 0.0;
            slotLocalToWorld(geometry, pt.u, pt.v, x, y);
            gcode += feedXY(x, y, feedRate);
        }
        totalLength += 2.0 * std::abs(closedU - openU) +
                       2.0 * finishWallV +
                       2.0 * leadLength;

        gcode += QStringLiteral("G40\n");
        gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
    }

    QString toolpathError;
    if (!validateOpenSlotGeneratedGCode(gcode, geometry, feedH, leadLength, toolpathError)) {
        res.errorMsg = toolpathError;
        return res;
    }

    res.gcode = gcode;
    res.ok = true;
    res.estimatedTimeS = (totalLength / feedRate * 60.0) + (feature.depth / plungeRate * 60.0);
    return res;
}
