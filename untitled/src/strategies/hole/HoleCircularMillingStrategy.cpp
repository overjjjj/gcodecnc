#include "HoleCircularMillingStrategy.h"
#include "HoleStrategyUtils.h"

#include <QObject>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

StrategyParams HoleCircularMillingStrategy::defaultParams() const
{
    StrategyParams p;
    p.set("safeHeight",      50.0);
    p.set("feedHeight",       3.0);
    p.set("stepDown",         1.0);
    p.set("spindleSpeed",  2500.0);
    p.set("feedRate",       800.0);
    p.set("plungeRate",     150.0);
    p.set("stockToLeave",     0.0);
    p.set("bottomAllowance",  0.0);
    p.set("springPasses",     1.0);
    return p;
}

ToolpathResult HoleCircularMillingStrategy::generate(const HoleFeature &feature,
                                                     const ToolEntry &tool,
                                                     const StrategyParams &params) const
{
    return generate(QVector<HoleFeature>{feature}, tool, params);
}

ToolpathResult HoleCircularMillingStrategy::generate(const QVector<HoleFeature> &features,
                                                     const ToolEntry &tool,
                                                     const StrategyParams &params) const
{
    ToolpathResult res;
    if (features.isEmpty()) {
        res.errorMsg = QObject::tr("未选择孔位。");
        return res;
    }
    if (tool.diameter <= 0.0) {
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
    const int springPasses = std::max(0, int(params.get("springPasses", 1.0)));

    if (stepDown <= 0.0 || feedRate <= 0.0 || plungeRate <= 0.0) {
        res.errorMsg = QObject::tr("圆插补铣孔参数无效，请检查螺旋节距、进给速度和下刀速度。");
        return res;
    }

    const QVector<HoleFeature> orderedFeatures = sortHolesByNearestNeighbor(features);
    for (const HoleFeature &feature : orderedFeatures) {
        const double targetDia = holeDiameter(feature) - stock * 2.0;
        if (targetDia <= 0.0) {
            res.errorMsg = QObject::tr("孔径无效，无法圆插补铣孔。");
            return res;
        }
        if (tool.diameter >= targetDia) {
            res.errorMsg = QObject::tr("圆插补铣孔需要刀具直径小于目标孔径。当前刀具 D%1，目标孔径 D%2。")
                .arg(tool.diameter, 0, 'f', 3)
                .arg(targetDia, 0, 'f', 3);
            return res;
        }
    }

    QString gc;
    gc += QStringLiteral("T%1 M6\n").arg(tool.id);
    gc += QStringLiteral("S%1 M3\n").arg(int(spindle));
    gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);

    double totalLen = 0.0;
    for (const HoleFeature &feature : orderedFeatures) {
        const double targetDia = holeDiameter(feature) - stock * 2.0;
        const double toolpathRadius = (targetDia - tool.diameter) * 0.5;
        const double baseDepth = feature.depth > 0.0 ? feature.depth : params.get("depth", 0.0);
        const double depth = baseDepth + std::max(0.0, bottomAllowance);
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

            gc += QStringLiteral("G0 X%1 Y%2\n").arg(cx, 0, 'f', 3).arg(cy, 0, 'f', 3);
            gc += QStringLiteral("G0 Z%1\n").arg(zStart, 0, 'f', 3);
            gc += QStringLiteral("G1 Z%1 F%2\n").arg(zStart, 0, 'f', 3).arg(int(plungeRate));

            const double startLen = gc.size();
            for (int s = 0; s <= spiralSegments; ++s) {
                const double t = double(s) / double(spiralSegments);
                const double angle = 2.0 * kPi * turns * t;
                const double radius = toolpathRadius * t;
                const double z = zStart + (zEnd - zStart) * t;
                const double x = cx + std::cos(angle) * radius;
                const double y = cy + std::sin(angle) * radius;
                gc += QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
                          .arg(x, 0, 'f', 3)
                          .arg(y, 0, 'f', 3)
                          .arg(z, 0, 'f', 3)
                          .arg(int(feedRate));
            }

            if (springPasses > 0) {
                gc += QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
                          .arg(cx + toolpathRadius, 0, 'f', 3)
                          .arg(cy, 0, 'f', 3)
                          .arg(zEnd, 0, 'f', 3)
                          .arg(int(feedRate));
                for (int pass = 0; pass < springPasses; ++pass) {
                    gc += QStringLiteral("G2 X%1 Y%2 I%3 J0.000 F%4\n")
                              .arg(cx + toolpathRadius, 0, 'f', 3)
                              .arg(cy, 0, 'f', 3)
                              .arg(-toolpathRadius, 0, 'f', 3)
                              .arg(int(feedRate));
                }
            }

            gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);

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
