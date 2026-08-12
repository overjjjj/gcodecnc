#include "PocketFinishStrategy.h"

#include <QObject>
#include <cmath>

StrategyParams PocketFinishStrategy::defaultParams() const
{
    StrategyParams params;
    params.set("safeHeight", 50.0);
    params.set("feedHeight", 3.0);
    params.set("stepDown", 1.0);
    params.set("spindleSpeed", 3000.0);
    params.set("feedRate", 800.0);
    params.set("plungeRate", 200.0);
    params.set("sideStockToLeave", 0.0);
    params.set("floorStockToLeave", 0.0);
    return params;
}

ToolpathResult PocketFinishStrategy::generate(const HoleFeature &,
                                               const ToolEntry &,
                                               const StrategyParams &) const
{
    ToolpathResult result;
    result.errorMsg = QObject::tr("Pocket wall finishing requires a pocket feature.");
    return result;
}

ToolpathResult PocketFinishStrategy::generate(const ContourFeature &feature,
                                               const ToolEntry &tool,
                                               const StrategyParams &params) const
{
    ToolpathResult result;
    const bool rectangular = feature.subType == QStringLiteral("rectangular_pocket");
    const bool circular = feature.subType == QStringLiteral("circular_pocket");
    if (!rectangular && !circular) {
        result.errorMsg = QObject::tr("Pocket wall finishing currently supports rectangular and circular pockets only.");
        return result;
    }
    if (tool.diameter <= 0.0 || feature.depth <= 0.0) {
        result.errorMsg = QObject::tr("Tool diameter and pocket depth must be greater than zero.");
        return result;
    }

    const double stepDown = params.get("stepDown", 1.0);
    const double feed = params.get("feedRate", 800.0);
    const double plunge = params.get("plungeRate", 200.0);
    const double legacyStock = params.get("stockToLeave", 0.0);
    const double sideStock = params.values.contains(QStringLiteral("sideStockToLeave"))
        ? params.get("sideStockToLeave", 0.0)
        : legacyStock;
    const double floorStock = params.values.contains(QStringLiteral("floorStockToLeave"))
        ? params.get("floorStockToLeave", 0.0)
        : legacyStock;
    if (stepDown <= 0.0 || feed <= 0.0 || plunge <= 0.0 ||
        sideStock < 0.0 || floorStock < 0.0) {
        result.errorMsg = QObject::tr("Stepdown, feed, and plunge must be positive; side and floor stock cannot be negative.");
        return result;
    }
    const double effectiveDepth = feature.depth - floorStock;
    if (effectiveDepth <= 0.0) {
        result.errorMsg = QObject::tr("Floor stock must be smaller than the pocket depth.");
        return result;
    }

    const double toolRadius = tool.diameter * 0.5;
    const double cx = feature.center.x();
    const double cy = feature.center.y();
    const double angleRad = rectangular
        ? feature.angle * std::acos(-1.0) / 180.0
        : 0.0;
    const double cosAngle = std::cos(angleRad);
    const double sinAngle = std::sin(angleRad);
    auto mapPocketPoint = [=](double u, double v) {
        return QVector3D(float(cx + u * cosAngle - v * sinAngle),
                         float(cy + u * sinAngle + v * cosAngle),
                         0.0f);
    };
    double halfLength = 0.0;
    double halfWidth = 0.0;
    double pathRadius = 0.0;
    if (rectangular) {
        halfLength = feature.length * 0.5 - toolRadius - sideStock;
        halfWidth = feature.width * 0.5 - toolRadius - sideStock;
        if (halfLength <= 0.0 || halfWidth <= 0.0) {
            result.errorMsg = QObject::tr("Tool diameter plus stock to leave does not fit inside the pocket.");
            return result;
        }
    } else {
        pathRadius = feature.radius - toolRadius - sideStock;
        if (pathRadius <= 0.0) {
            result.errorMsg = QObject::tr("Tool diameter plus stock to leave does not fit inside the pocket.");
            return result;
        }
    }

    const double topZ = feature.center.z();
    const double safeZ = params.get("safeHeight", 50.0);
    const double feedZ = topZ + params.get("feedHeight", 3.0);
    const int layers = static_cast<int>(std::ceil(effectiveDepth / stepDown));
    QString gcode;
    gcode += QStringLiteral("T%1 M6\nS%2 M3\nG0 Z%3\n")
                 .arg(tool.id)
                 .arg(int(params.get("spindleSpeed", 3000.0)))
                 .arg(safeZ, 0, 'f', 3);

    double pathLength = 0.0;
    for (int layer = 1; layer <= layers; ++layer) {
        const double z = topZ - std::min(layer * stepDown, effectiveDepth);
        const QVector3D startPoint = rectangular
            ? mapPocketPoint(-halfLength, -halfWidth)
            : QVector3D(float(cx + pathRadius), float(cy), 0.0f);
        const double startX = startPoint.x();
        const double startY = startPoint.y();
        gcode += QStringLiteral("; Pocket wall finish Z=%1\nG0 X%2 Y%3\nG0 Z%4\nG1 Z%5 F%6\nG40\nG1 X%7 Y%8 F%9\n")
                     .arg(z, 0, 'f', 3)
                     .arg(cx, 0, 'f', 3)
                     .arg(cy, 0, 'f', 3)
                     .arg(feedZ, 0, 'f', 3)
                     .arg(z, 0, 'f', 3)
                     .arg(int(plunge))
                     .arg(startX, 0, 'f', 3)
                     .arg(startY, 0, 'f', 3)
                     .arg(int(feed));
        pathLength += rectangular
            ? std::sqrt(halfLength * halfLength + halfWidth * halfWidth)
            : pathRadius;
        if (rectangular) {
            const QVector3D lowerRight = mapPocketPoint(halfLength, -halfWidth);
            const QVector3D upperRight = mapPocketPoint(halfLength, halfWidth);
            const QVector3D upperLeft = mapPocketPoint(-halfLength, halfWidth);
            gcode += QStringLiteral("G1 X%1 Y%2 F%3\nG1 X%4 Y%5\nG1 X%6 Y%7\nG1 X%8 Y%9\n")
                         .arg(lowerRight.x(), 0, 'f', 3).arg(lowerRight.y(), 0, 'f', 3).arg(int(feed))
                         .arg(upperRight.x(), 0, 'f', 3).arg(upperRight.y(), 0, 'f', 3)
                         .arg(upperLeft.x(), 0, 'f', 3).arg(upperLeft.y(), 0, 'f', 3)
                         .arg(startX, 0, 'f', 3).arg(startY, 0, 'f', 3);
            pathLength += 4.0 * (halfLength + halfWidth);
        } else {
            gcode += QStringLiteral("G2 X%1 Y%2 I%3 J0.000 F%4\n")
                         .arg(startX, 0, 'f', 3).arg(startY, 0, 'f', 3)
                         .arg(-pathRadius, 0, 'f', 3).arg(int(feed));
            pathLength += 2.0 * std::acos(-1.0) * pathRadius;
        }
        gcode += QStringLiteral("G0 Z%1\n").arg(safeZ, 0, 'f', 3);
    }

    result.gcode = gcode;
    result.ok = true;
    result.estimatedTimeS = pathLength / feed * 60.0 + effectiveDepth / plunge * 60.0;
    return result;
}
