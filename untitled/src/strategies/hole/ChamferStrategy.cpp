#include "ChamferStrategy.h"
#include "HoleStrategyUtils.h"
#include "../../core/GCodeCoordinate.h"
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// 函数说明：defaultParams负责当前模块的核心处理。
StrategyParams ChamferStrategy::defaultParams() const
{
    StrategyParams p;
    p.set("safeHeight",   50.0);
    p.set("feedHeight",    3.0);
    p.set("chamferMode",   0.0);
    p.set("axialDepth",    1.0);
    p.set("chamferWidth",  1.0);
    p.set("chamferAngle", 45.0);
    p.set("spindleSpeed", 600.0);
    p.set("feedRate",      80.0);
    p.set("coolantMode",     1.0);
    return p;
}

// 函数说明：generate负责当前模块的核心处理。
ToolpathResult ChamferStrategy::generate(const HoleFeature &feature,
                                          const ToolEntry   &tool,
                                          const StrategyParams &params) const
{
    return generate(QVector<HoleFeature>{feature}, tool, params);
}

ToolpathResult ChamferStrategy::generate(const QVector<HoleFeature> &features,
                                          const ToolEntry &tool,
                                          const StrategyParams &params) const
{
    ToolpathResult res;
    res.errorMsg = holeSpindleOutputError(id(), params);
    if (!res.errorMsg.isEmpty()) return res;
    res.errorMsg = holeAxisOutputError(features, id(), params, tool);
    if (!res.errorMsg.isEmpty()) return res;
    if (features.isEmpty()) {
        res.errorMsg = QStringLiteral("未选择孔位。");
        return res;
    }
    if (tool.type != QStringLiteral("chamfer_mill")) {
        res.errorMsg = QStringLiteral("孔倒角必须使用倒角刀，不能使用麻花钻、铰刀或立铣刀。");
        return res;
    }
    if (tool.id <= 0 || !std::isfinite(tool.diameter) || tool.diameter <= 0.0) {
        res.errorMsg = QStringLiteral("当前倒角刀无效，请先在刀具库中选择有效倒角刀。");
        return res;
    }
    const double safe   = params.get("safeHeight",   50.0);
    const double feed   = params.get("feedHeight",    3.0);
    const double modeValue = params.get("chamferMode", 0.0);
    const double axialDepth = params.get("axialDepth", 1.0);
    const double cWidth = params.get("chamferWidth",  1.0);
    const double cAngle = params.get("chamferAngle", 45.0);
    const double S      = params.get("spindleSpeed", 600.0);
    const double F      = params.get("feedRate",      80.0);
    const double coolantValue = params.get("coolantMode", 1.0);

    const int mode = std::isfinite(modeValue) && modeValue >= 0 && modeValue <= 1
        ? int(std::round(modeValue)) : -1;
    if (!std::isfinite(safe) || !std::isfinite(feed) || !std::isfinite(modeValue) ||
        !std::isfinite(axialDepth) || !std::isfinite(cWidth) ||
        !std::isfinite(cAngle) || !std::isfinite(S) ||
        !std::isfinite(F) || !std::isfinite(coolantValue) || safe <= 0.0 || feed < 0.0 ||
        std::abs(modeValue - mode) > 1.0e-9 || mode < 0 || mode > 1 || axialDepth <= 0.0 ||
        cWidth <= 0.0 || cAngle <= 0.0 || cAngle >= 89.9 ||
        S <= 0.0 || F <= 0.0 || coolantValue < 0.0 || coolantValue > 2.0) {
        res.errorMsg = QStringLiteral("孔倒角参数无效，请检查加工方式、轴向深度、倒角宽度、角度和进给速度。");
        return res;
    }

    if (mode == 1) {
        bool angleOk = false;
        double includedAngle = tool.extra.value(QStringLiteral("angle")).toDouble(&angleOk);
        if (!angleOk) {
            includedAngle = tool.pointAngle;
            angleOk = true;
        }
        if (!angleOk || !std::isfinite(includedAngle) || includedAngle <= 0.0 ||
            includedAngle >= 180.0 || !std::isfinite(tool.fluteLen) || tool.fluteLen <= 0.0 ||
            !std::isfinite(tool.totalLen) || tool.totalLen <= 0.0 ||
            axialDepth > tool.fluteLen + 0.01 || axialDepth >= tool.totalLen - 0.01) {
            res.errorMsg = QStringLiteral("轴向锪倒角要求刀具直径、夹角、有效刃长和总长有效，并能覆盖确认的轴向深度。");
            return res;
        }
    }

    const double zDrop = mode == 1 ? axialDepth
                                   : chamferConeHeight(tool);
    const QVector<HoleFeature> orderedFeatures = sortHolesByNearestNeighbor(features);
    for (const HoleFeature &feature : orderedFeatures) {
        const double toolpathRadius = feature.radius + cWidth - tool.diameter * 0.5;
        const HoleZRange zRange = holeZRange(feature, zDrop, feed);
        if (!std::isfinite(feature.radius) || feature.radius <= 0.0 ||
            !std::isfinite(feature.center.x()) || !std::isfinite(feature.center.y()) ||
            !std::isfinite(feature.center.z()) ||
            (mode == 0 && (!std::isfinite(toolpathRadius) || toolpathRadius <= 0.0)) ||
            !std::isfinite(zRange.retractZ) ||
            !std::isfinite(zRange.bottomZ) || safe < zRange.retractZ) {
            res.errorMsg = QStringLiteral("孔倒角几何无效、刀具过大，或安全高度低于孔口返回平面。");
            return res;
        }
    }

    QString gc;
    gc += QStringLiteral("T%1 M6\n").arg(tool.id);
    gc += QStringLiteral("S%1 M3\n").arg(int(S));
    gc += QStringLiteral("G0 Z%1\n").arg(formatGCodeCoordinate(safe));
    const int coolantMode = int(std::round(coolantValue));
    if (coolantMode > 0) gc += coolantMode == 2 ? QStringLiteral("M7\n") : QStringLiteral("M8\n");

    if (mode == 1) {
        gc += QStringLiteral("; Axial G81 countersink: operator-confirmed axial depth %1 mm\n")
                  .arg(axialDepth, 0, 'f', 3);
        for (const HoleFeature &feature : orderedFeatures) {
            const HoleZRange zRange = holeZRange(feature, axialDepth, feed);
            gc += QStringLiteral(
                ";CNEXT_HOLE_CYCLE code=G81 rtp=%1 rfp=%2 sdis=%3 x=%4 y=%5 z=%6 q=0 p=0 f=%7 vari=0\n")
                      .arg(safe, 0, 'g', 17)
                      .arg(zRange.entryZ, 0, 'g', 17)
                      .arg(feed, 0, 'g', 17)
                      .arg(feature.center.x(), 0, 'g', 17)
                      .arg(feature.center.y(), 0, 'g', 17)
                      .arg(zRange.bottomZ, 0, 'g', 17)
                      .arg(F, 0, 'f', 3);
        }
        gc += QStringLiteral("G0 Z%1\n").arg(formatGCodeCoordinate(safe));
        res.gcode = gc;
        res.ok = true;
        res.estimatedTimeS = (axialDepth / F * 60.0 + 2.0) * orderedFeatures.size();
        return res;
    }

    double totalLength = 0.0;
    for (const HoleFeature &feature : orderedFeatures) {
        const double x = feature.center.x();
        const double y = feature.center.y();
        const double toolpathRadius = feature.radius + cWidth - tool.diameter * 0.5;
        const HoleZRange zRange = holeZRange(feature, zDrop, feed);
        const double startX = x + toolpathRadius;

        gc += QStringLiteral("; Chamfer hole D%1 width %2 with tool D%3\n")
                  .arg(holeDiameter(feature), 0, 'f', 3)
                  .arg(cWidth, 0, 'f', 3)
                  .arg(tool.diameter, 0, 'f', 3);
        gc += QStringLiteral("G0 X%1 Y%2\n").arg(formatGCodeCoordinate(x)).arg(formatGCodeCoordinate(y));
        gc += QStringLiteral("G0 Z%1\n").arg(formatGCodeCoordinate(zRange.retractZ));
        gc += QStringLiteral("; Chamfer center descent and radial entry\n");
        gc += QStringLiteral("G1 Z%1 F%2\n")
                  .arg(formatGCodeCoordinate(zRange.bottomZ)).arg(int(F));
        gc += QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
                  .arg(formatGCodeCoordinate(startX))
                  .arg(formatGCodeCoordinate(y))
                  .arg(formatGCodeCoordinate(zRange.bottomZ))
                  .arg(int(F));
        gc += QStringLiteral("G2 X%1 Y%2 I%3 J0.000 F%4\n")
                  .arg(formatGCodeCoordinate(startX))
                  .arg(formatGCodeCoordinate(y))
                  .arg(formatGCodeCoordinate(-toolpathRadius))
                  .arg(int(F));
        gc += QStringLiteral("; Chamfer radial return before center retract\n");
        gc += QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
                  .arg(formatGCodeCoordinate(x))
                  .arg(formatGCodeCoordinate(y))
                  .arg(formatGCodeCoordinate(zRange.bottomZ))
                  .arg(int(F));
        gc += QStringLiteral("G0 Z%1\n").arg(formatGCodeCoordinate(safe));
        totalLength += 2.0 * kPi * toolpathRadius + 2.0 * toolpathRadius + zDrop + feed;
    }

    res.gcode = gc;
    res.ok    = true;
    res.estimatedTimeS = totalLength / F * 60.0 + 2.0 * orderedFeatures.size();
    return res;
}
