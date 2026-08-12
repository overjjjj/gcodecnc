#include "PocketFloorFinishStrategy.h"

#include <QObject>
#include <algorithm>
#include <cmath>

namespace {

static QString linearMove(double x, double y, double feed)
{
    return QStringLiteral("G1 X%1 Y%2 F%3\n")
        .arg(x, 0, 'f', 3).arg(y, 0, 'f', 3).arg(int(feed));
}

} // namespace

StrategyParams PocketFloorFinishStrategy::defaultParams() const
{
    StrategyParams params;
    params.set("safeHeight", 50.0);
    params.set("feedHeight", 3.0);
    params.set("stepover", 0.5);
    params.set("spindleSpeed", 4000.0);
    params.set("feedRate", 500.0);
    params.set("plungeRate", 150.0);
    params.set("sideStockToLeave", 0.0);
    params.set("floorStockToLeave", 0.0);
    return params;
}

ToolpathResult PocketFloorFinishStrategy::generate(const HoleFeature &,
                                                    const ToolEntry &,
                                                    const StrategyParams &) const
{
    ToolpathResult result;
    result.errorMsg = QObject::tr("Pocket floor finishing requires a pocket feature.");
    return result;
}

ToolpathResult PocketFloorFinishStrategy::generate(const ContourFeature &feature,
                                                    const ToolEntry &tool,
                                                    const StrategyParams &params) const
{
    ToolpathResult result;
    const bool rectangular = feature.subType == QStringLiteral("rectangular_pocket");
    const bool circular = feature.subType == QStringLiteral("circular_pocket");
    if (!rectangular && !circular) {
        result.errorMsg = QObject::tr("Pocket floor finishing currently supports rectangular and circular pockets only.");
        return result;
    }
    if (tool.diameter <= 0.0 || feature.depth <= 0.0) {
        result.errorMsg = QObject::tr("Tool diameter and pocket depth must be greater than zero.");
        return result;
    }

    const double stepover = params.get("stepover", 0.5);
    const double feed = params.get("feedRate", 500.0);
    const double plunge = params.get("plungeRate", 150.0);
    const double legacyStock = params.get("stockToLeave", 0.0);
    const double sideStock = params.values.contains(QStringLiteral("sideStockToLeave"))
        ? params.get("sideStockToLeave", 0.0)
        : legacyStock;
    const double floorStock = params.values.contains(QStringLiteral("floorStockToLeave"))
        ? params.get("floorStockToLeave", 0.0)
        : legacyStock;
    if (stepover <= 0.0 || feed <= 0.0 || plunge <= 0.0 ||
        sideStock < 0.0 || floorStock < 0.0) {
        result.errorMsg = QObject::tr("Stepover, feed, and plunge must be positive; side and floor stock cannot be negative.");
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
    const double halfX = rectangular ? feature.length * 0.5 - toolRadius - sideStock : 0.0;
    const double halfY = rectangular ? feature.width * 0.5 - toolRadius - sideStock : 0.0;
    const double radius = circular ? feature.radius - toolRadius - sideStock : 0.0;
    if ((rectangular && (halfX <= 0.0 || halfY <= 0.0)) ||
        (circular && radius <= 0.0)) {
        result.errorMsg = QObject::tr("Tool diameter plus stock to leave does not fit inside the pocket.");
        return result;
    }

    const double safeZ = params.get("safeHeight", 50.0);
    const double topZ = feature.center.z();
    const double feedZ = topZ + params.get("feedHeight", 3.0);
    const double cutZ = topZ - effectiveDepth;
    const double yLimit = rectangular ? halfY : radius;
    const int passes = std::max(1, static_cast<int>(std::ceil(2.0 * yLimit / stepover)) + 1);
    QString gcode;
    gcode += QStringLiteral("T%1 M6\nS%2 M3\nG0 Z%3\n; Pocket floor finish Z=%4\n")
                 .arg(tool.id)
                 .arg(int(params.get("spindleSpeed", 4000.0)))
                 .arg(safeZ, 0, 'f', 3)
                 .arg(cutZ, 0, 'f', 3);

    double totalLength = 0.0;
    bool started = false;
    bool forward = true;
    for (int index = 0; index < passes; ++index) {
        const double localY = -yLimit + (2.0 * yLimit * index / std::max(1, passes - 1));
        const double xLimit = rectangular
            ? halfX
            : std::sqrt(std::max(0.0, radius * radius - localY * localY));
        if (xLimit <= 1.0e-6) {
            continue;
        }
        const double u0 = forward ? -xLimit : xLimit;
        const double u1 = forward ? xLimit : -xLimit;
        const QVector3D startPoint = mapPocketPoint(u0, localY);
        const QVector3D endPoint = mapPocketPoint(u1, localY);
        const double x0 = startPoint.x();
        const double y0 = startPoint.y();
        const double x1 = endPoint.x();
        const double y1 = endPoint.y();
        if (!started) {
            gcode += QStringLiteral("G0 X%1 Y%2\nG0 Z%3\nG1 Z%4 F%5\nG40\n")
                         .arg(cx, 0, 'f', 3).arg(cy, 0, 'f', 3)
                         .arg(feedZ, 0, 'f', 3).arg(cutZ, 0, 'f', 3).arg(int(plunge));
            started = true;
        }
        gcode += linearMove(x0, y0, feed);
        gcode += linearMove(x1, y1, feed);
        totalLength += std::sqrt(u0 * u0 + localY * localY) + 2.0 * xLimit;
        forward = !forward;
    }
    if (!started) {
        result.errorMsg = QObject::tr("Pocket floor finish produced no valid cutting passes.");
        return result;
    }
    gcode += QStringLiteral("G0 Z%1\n").arg(safeZ, 0, 'f', 3);
    result.gcode = gcode;
    result.ok = true;
    result.estimatedTimeS = totalLength / feed * 60.0 + effectiveDepth / plunge * 60.0;
    return result;
}
