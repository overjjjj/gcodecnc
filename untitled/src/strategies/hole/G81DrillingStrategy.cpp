#include "G81DrillingStrategy.h"

#include "HoleStrategyUtils.h"
#include "../../core/GCodeCoordinate.h"

#include <QObject>

#include <cmath>

namespace {

QString validateG81Tool(const HoleFeature &feature, const ToolEntry &tool, double depth)
{
    if (tool.id <= 0 || tool.type != QStringLiteral("drill") ||
        !std::isfinite(tool.diameter) || tool.diameter <= 0.0 ||
        !std::isfinite(tool.fluteLen) || !std::isfinite(tool.totalLen)) {
        return QObject::tr("G81 必须使用编号和直径有效的钻头。");
    }
    const double targetDiameter = holeDiameter(feature);
    const double tolerance = std::max(0.05, targetDiameter * 0.01);
    if (targetDiameter <= 0.0 || tool.diameter > targetDiameter + tolerance) {
        return QObject::tr("G81 钻头直径不能大于目标孔径。");
    }
    if (tool.fluteLen > 0.0 && depth > tool.fluteLen + 0.01) {
        return QObject::tr("G81 加工深度超过刀具有效刃长。");
    }
    if (tool.totalLen > 0.0 && depth >= tool.totalLen - 0.01) {
        return QObject::tr("G81 加工深度达到刀具总长，无法形成安全伸出。");
    }
    return QString();
}

} // namespace

StrategyParams G81DrillingStrategy::defaultParams() const
{
    StrategyParams params;
    params.set(QStringLiteral("safeHeight"), 50.0);
    params.set(QStringLiteral("feedHeight"), 3.0);
    params.set(QStringLiteral("depth"), 2.0);
    params.set(QStringLiteral("spindleSpeed"), 800.0);
    params.set(QStringLiteral("feedRate"), 60.0);
    params.set(QStringLiteral("coolantMode"), 1.0);
    return params;
}

ToolpathResult G81DrillingStrategy::generate(const HoleFeature &feature,
                                             const ToolEntry &tool,
                                             const StrategyParams &params) const
{
    return generate(QVector<HoleFeature>{feature}, tool, params);
}

ToolpathResult G81DrillingStrategy::generate(const QVector<HoleFeature> &features,
                                             const ToolEntry &tool,
                                             const StrategyParams &params) const
{
    ToolpathResult result;
    result.errorMsg = holeSpindleOutputError(id(), params);
    if (!result.errorMsg.isEmpty()) return result;
    result.errorMsg = holeAxisOutputError(features, id(), params);
    if (!result.errorMsg.isEmpty()) return result;
    if (features.isEmpty()) {
        result.errorMsg = QObject::tr("未选择孔位，无法生成 G81 钻孔代码。");
        return result;
    }
    const double safeHeight = params.get(QStringLiteral("safeHeight"), 50.0);
    const double feedHeight = params.get(QStringLiteral("feedHeight"), 3.0);
    const double depth = params.get(QStringLiteral("depth"), 2.0);
    const double spindleSpeed = params.get(QStringLiteral("spindleSpeed"), 800.0);
    const double feedRate = params.get(QStringLiteral("feedRate"), 60.0);
    const double coolantValue = params.get(QStringLiteral("coolantMode"), 1.0);
    if (!std::isfinite(safeHeight) || !std::isfinite(feedHeight) ||
        !std::isfinite(depth) || !std::isfinite(spindleSpeed) ||
        !std::isfinite(feedRate) || !std::isfinite(coolantValue) ||
        safeHeight <= 0.0 || feedHeight <= 0.0 || depth <= 0.0 ||
        spindleSpeed <= 0.0 || feedRate <= 0.0 || coolantValue < 0.0 ||
        coolantValue > 2.0) {
        result.errorMsg = QObject::tr("G81 参数无效，请检查安全高度、深度、主轴和进给速度。");
        return result;
    }

    const int coolantMode = static_cast<int>(std::round(coolantValue));
    const QVector<HoleFeature> orderedFeatures = sortHolesByNearestNeighbor(features);
    QString gcode;
    gcode += QStringLiteral("T%1 M6\n").arg(tool.id);
    gcode += QStringLiteral("S%1 M3\n").arg(static_cast<int>(std::round(spindleSpeed)));
    gcode += QStringLiteral("G0 G43 H%1 Z%2\n")
                 .arg(tool.id).arg(formatGCodeCoordinate(safeHeight));
    if (coolantMode > 0) {
        gcode += coolantMode == 2 ? QStringLiteral("M7\n") : QStringLiteral("M8\n");
    }

    for (const HoleFeature &feature : orderedFeatures) {
        const QString toolError = validateG81Tool(feature, tool, depth);
        if (!toolError.isEmpty()) {
            result.errorMsg = toolError;
            return result;
        }
        const HoleZRange zRange = holeZRange(feature, depth, feedHeight);
        if (safeHeight <= zRange.retractZ) {
            result.errorMsg = QObject::tr("G81 安全高度必须高于循环返回平面。");
            return result;
        }
        gcode += QStringLiteral(
            ";CNEXT_HOLE_CYCLE code=G81 rtp=%1 rfp=%2 sdis=%3 x=%4 y=%5 z=%6 q=0 p=0 f=%7 vari=0\n")
                     .arg(safeHeight, 0, 'g', 17)
                     .arg(zRange.entryZ, 0, 'g', 17)
                     .arg(feedHeight, 0, 'g', 17)
                     .arg(feature.center.x(), 0, 'g', 17)
                     .arg(feature.center.y(), 0, 'g', 17)
                     .arg(zRange.bottomZ, 0, 'g', 17)
                     .arg(feedRate, 0, 'f', 3);
    }
    gcode += QStringLiteral("G0 Z%1\n").arg(formatGCodeCoordinate(safeHeight));
    result.ok = true;
    result.gcode = gcode;
    result.estimatedTimeS = (depth / feedRate * 60.0 + 2.0) * orderedFeatures.size();
    return result;
}
