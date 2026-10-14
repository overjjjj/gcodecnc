#include "HoleCircularMillingStrategy.h"
#include "HoleStrategyUtils.h"
#include "../../core/GCodeCoordinate.h"

#include <QObject>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// 函数说明：defaultParams负责当前模块的核心处理。
StrategyParams HoleCircularMillingStrategy::defaultParams() const
{
    StrategyParams p;
    p.set("safeHeight",      50.0);
    p.set("feedHeight",       3.0);
    p.set("depth",            20.0);
    p.set("stepDown",         1.0);
    p.set("spindleSpeed",  2500.0);
    p.set("feedRate",       800.0);
    p.set("plungeRate",     150.0);
    p.set("stockToLeave",     0.0);
    p.set("bottomAllowance",  0.0);
    p.set("springPasses",     1.0);
    p.set("coolantMode",       1.0);
    return p;
}

// 函数说明：generate负责当前模块的核心处理。
ToolpathResult HoleCircularMillingStrategy::generate(const HoleFeature &feature,
                                                     const ToolEntry &tool,
                                                     const StrategyParams &params) const
{
    return generate(QVector<HoleFeature>{feature}, tool, params);
}

// 函数说明：generate负责当前模块的核心处理。
ToolpathResult HoleCircularMillingStrategy::generate(const QVector<HoleFeature> &features,
                                                     const ToolEntry &tool,
                                                     const StrategyParams &params) const
{
    ToolpathResult res;
    res.errorMsg = holeSpindleOutputError(id(), params);
    if (!res.errorMsg.isEmpty()) return res;
    res.errorMsg = holeAxisOutputError(features, id(), params);
    if (!res.errorMsg.isEmpty()) return res;
    if (features.isEmpty()) {
        res.errorMsg = QObject::tr("未选择孔位。");
        return res;
    }
    if (!std::isfinite(tool.diameter) || tool.diameter <= 0.0) {
        res.errorMsg = QObject::tr("刀具直径无效。");
        return res;
    }

    const double safe = params.get("safeHeight", 50.0);
    const double feedH = params.get("feedHeight", 3.0);
    const double stepDown = params.get("stepDown", 1.0);
    const double spindle = params.get("spindleSpeed", 2500.0);
    const double feedRate = params.get("feedRate", 800.0);
    const double plungeRate = params.get("plungeRate", 150.0);
    const double stock = params.get("stockToLeave", 0.0);
    const double bottomAllowance = params.get("bottomAllowance", 0.0);
    const double passes = params.get("springPasses", 1.0);
    const double coolantValue = params.get("coolantMode", 1.0);

    // Speeds are emitted as integers; validate before conversion, including values that would become zero.
    const double maxInt = std::numeric_limits<int>::max();
    if (!std::isfinite(safe) || !std::isfinite(feedH) || !std::isfinite(stock)
        || !std::isfinite(spindle) || !std::isfinite(stepDown) || !std::isfinite(feedRate) || !std::isfinite(plungeRate)
        || !std::isfinite(bottomAllowance) || !std::isfinite(passes) || !std::isfinite(coolantValue)
        || feedH < 0.0 || stock < 0.0 || stepDown <= 0.0
        || spindle < 1.0 || spindle > maxInt || feedRate < 1.0 || feedRate > maxInt
        || plungeRate < 1.0 || plungeRate > maxInt
        || bottomAllowance < 0.0 || passes < 0.0 || passes > maxInt || std::floor(passes) != passes
        || coolantValue < 0.0 || coolantValue > 2.0) {
        res.errorMsg = QObject::tr("圆插补铣孔参数无效，请检查高度、余量、整数光刀次数及转速和进给速度。");
        return res;
    }
    const int springPasses = int(passes);

    for (const HoleFeature &feature : features) {
        const double targetDia = holeDiameter(feature) - stock * 2.0;
        if (!std::isfinite(feature.radius) || !std::isfinite(targetDia) || targetDia <= 0.0) {
            res.errorMsg = QObject::tr("孔径无效，无法圆插补铣孔。");
            return res;
        }
        if (tool.diameter >= targetDia) {
            res.errorMsg = QObject::tr("圆插补铣孔需要刀具直径小于目标孔径。当前刀具 D%1，目标孔径 D%2。")
                .arg(tool.diameter, 0, 'f', 3)
                .arg(targetDia, 0, 'f', 3);
            return res;
        }
        const double baseDepth = params.values.contains(QStringLiteral("depth"))
            ? params.get("depth", 20.0)
            : feature.depth;
        const double depth = baseDepth + bottomAllowance;
        const HoleZRange zRange = holeZRange(feature, depth, feedH);
        const double layers = std::ceil(depth / stepDown);
        const double segments = std::ceil((targetDia - tool.diameter) * 4.0);
        // These bounds protect the existing int loops, not a machining-policy limit.
        // The inclusive segment loop also needs room for its final increment.
        if (!std::isfinite(feature.center.x()) || !std::isfinite(feature.center.y())
            || !std::isfinite(feature.center.z()) || !std::isfinite(feature.depth)
            || !std::isfinite(baseDepth) || baseDepth <= 0.0 || !std::isfinite(depth) || depth <= 0.0
            || !std::isfinite(zRange.entryZ) || !std::isfinite(zRange.retractZ) || !std::isfinite(zRange.bottomZ)
            || safe < zRange.retractZ || !std::isfinite(layers) || layers > maxInt
            || !std::isfinite(segments) || segments >= maxInt) {
            res.errorMsg = QObject::tr("圆插补铣孔几何或切深无效，或安全高度低于孔口加进给高度，或分层数量无法表示。");
            return res;
        }
    }
    const QVector<HoleFeature> orderedFeatures = sortHolesByNearestNeighbor(features);

    QString gc;
    gc += QStringLiteral("T%1 M6\n").arg(tool.id);
    gc += QStringLiteral("S%1 M3\n").arg(int(spindle));
    gc += QStringLiteral("G0 Z%1\n").arg(formatGCodeCoordinate(safe));
    const int coolantMode = int(std::round(coolantValue));
    if (coolantMode > 0) gc += coolantMode == 2 ? QStringLiteral("M7\n") : QStringLiteral("M8\n");

    double totalLen = 0.0;
    for (const HoleFeature &feature : orderedFeatures) {
        const double targetDia = holeDiameter(feature) - stock * 2.0;
        const double toolpathRadius = (targetDia - tool.diameter) * 0.5;
        const double baseDepth = params.values.contains(QStringLiteral("depth"))
            ? params.get("depth", 20.0)
            : feature.depth;
        const double depth = baseDepth + bottomAllowance;
        const HoleZRange zRange = holeZRange(feature, depth, feedH);
        const int depthLayers = std::max(1, int(std::ceil(depth / stepDown)));
        const int spiralSegments = std::max(48, int(std::ceil(toolpathRadius * 8.0)));
        const double cx = feature.center.x();
        const double cy = feature.center.y();

        gc += QStringLiteral("; Center-start spiral mill target D%1 with tool D%2\n")
                  .arg(targetDia, 0, 'f', 3)
                  .arg(tool.diameter, 0, 'f', 3);

        for (int layer = 0; layer < depthLayers; ++layer) {
            const double layerTop = std::min(layer * stepDown, depth);
            const double layerBottom = std::min((layer + 1) * stepDown, depth);
            const double zStart = zRange.entryZ - layerTop;
            const double zEnd = zRange.entryZ - layerBottom;
            const double turns = std::max(1.0, std::ceil(toolpathRadius / std::max(tool.diameter * 0.5, 1.0)));

            gc += QStringLiteral("G0 X%1 Y%2\n").arg(formatGCodeCoordinate(cx)).arg(formatGCodeCoordinate(cy));
            gc += QStringLiteral("G0 Z%1\n").arg(formatGCodeCoordinate(zRange.retractZ));
            gc += QStringLiteral("G1 Z%1 F%2\n").arg(formatGCodeCoordinate(zStart)).arg(int(plungeRate));

            const double startLen = gc.size();
            for (int s = 0; s <= spiralSegments; ++s) {
                const double t = double(s) / double(spiralSegments);
                const double angle = 2.0 * kPi * turns * t;
                const double radius = toolpathRadius * t;
                const double z = zStart + (zEnd - zStart) * t;
                const double x = cx + std::cos(angle) * radius;
                const double y = cy + std::sin(angle) * radius;
                gc += QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
                          .arg(formatGCodeCoordinate(x))
                          .arg(formatGCodeCoordinate(y))
                          .arg(formatGCodeCoordinate(z))
                          .arg(int(feedRate));
            }

            if (springPasses > 0) {
                gc += QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
                          .arg(formatGCodeCoordinate(cx + toolpathRadius))
                          .arg(formatGCodeCoordinate(cy))
                          .arg(formatGCodeCoordinate(zEnd))
                          .arg(int(feedRate));
                for (int pass = 0; pass < springPasses; ++pass) {
                    gc += QStringLiteral("G2 X%1 Y%2 I%3 J0.000 F%4\n")
                              .arg(formatGCodeCoordinate(cx + toolpathRadius))
                              .arg(formatGCodeCoordinate(cy))
                              .arg(formatGCodeCoordinate(-toolpathRadius))
                              .arg(int(feedRate));
                }
            }

            gc += QStringLiteral("G0 Z%1\n").arg(formatGCodeCoordinate(safe));

            const double axial = std::abs(zEnd - zStart);
            const double spiralLen = std::hypot(2.0 * kPi * turns * toolpathRadius, axial);
            totalLen += spiralLen + axial;
            Q_UNUSED(startLen);
        }
    }

    res.gcode = gc;
    res.ok = true;
    res.estimatedTimeS = totalLen / std::max(feedRate, 1.0) * 60.0;
    return res;
}
