#include "BoringG86Strategy.h"

#include "HoleStrategyUtils.h"

ProcessParameterSchema BoringG86Strategy::parameterSchema() const
{
    ProcessParameterSchema schema = ProcessParameterSchema::CommonOperation();
    ProcessParameterDefinition dwell;
    dwell.id = QStringLiteral("dwellTime");
    dwell.defaultValue = 0.0;
    dwell.minimum = 0.0;
    dwell.hasMinimum = true;
    dwell.required = true;
    schema.addDefinition(dwell);
    return schema;
}

StrategyParams BoringG86Strategy::defaultParams() const
{
    StrategyParams params;
    params.set(QStringLiteral("safeHeight"), 50.0);
    params.set(QStringLiteral("feedHeight"), 3.0);
    params.set(QStringLiteral("depth"), 20.0);
    params.set(QStringLiteral("spindleSpeed"), 300.0);
    params.set(QStringLiteral("feedRate"), 30.0);
    params.set(QStringLiteral("dwellTime"), 0.0);
    return params;
}

ToolpathResult BoringG86Strategy::generate(
    const HoleFeature &feature,
    const ToolEntry &tool,
    const StrategyParams &params) const
{
    return generate(QVector<HoleFeature>{feature}, tool, params);
}

ToolpathResult BoringG86Strategy::generate(
    const QVector<HoleFeature> &features,
    const ToolEntry &tool,
    const StrategyParams &params) const
{
    ToolpathResult result;
    if (features.isEmpty()) {
        result.errorMsg = QObject::tr("未选择孔位。");
        return result;
    }
    if (tool.id <= 0 || tool.type != QStringLiteral("boring_bar") ||
        tool.diameter <= 0.0) {
        result.errorMsg = QObject::tr("G86 必须使用已定义几何的镗刀杆。");
        return result;
    }
    const double feedRate = params.get(QStringLiteral("feedRate"), 30.0);
    const double dwellTime = params.get(QStringLiteral("dwellTime"), 0.0);
    if (feedRate <= 0.0 || dwellTime < 0.0) {
        result.errorMsg = QObject::tr("G86 进给必须大于零，孔底停留不能为负数。");
        return result;
    }

    const double safeHeight = params.get(QStringLiteral("safeHeight"), 50.0);
    const double feedHeight = params.get(QStringLiteral("feedHeight"), 3.0);
    const double spindleSpeed = params.get(QStringLiteral("spindleSpeed"), 300.0);
    const QVector<HoleFeature> ordered = sortHolesByNearestNeighbor(features);
    QString gcode;
    gcode += QStringLiteral("T%1 M6\n").arg(tool.id);
    gcode += QStringLiteral("S%1 M3\n").arg(static_cast<int>(spindleSpeed));
    gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
    double totalDepth = 0.0;
    for (const HoleFeature &feature : ordered) {
        if (feature.radius <= 0.0 || feature.depth <= 0.0 ||
            tool.diameter >= feature.radius * 2.0) {
            result.errorMsg = QObject::tr("G86 镗刀必须小于目标孔径，且孔深必须可确定。");
            return result;
        }
        const double depth = effectiveDrillDepth(
            feature, params.get(QStringLiteral("depth"), feature.depth));
        const HoleZRange range = holeZRange(feature, depth, feedHeight);
        gcode += QStringLiteral(
                     ";CNEXT_HOLE_CYCLE code=G86 rtp=%1 rfp=%2 sdis=%3 x=%4 y=%5 "
                     "z=%6 q=0 p=%7 f=%8 vari=0\n")
                     .arg(safeHeight, 0, 'f', 3)
                     .arg(range.entryZ, 0, 'f', 3)
                     .arg(feedHeight, 0, 'f', 3)
                     .arg(feature.center.x(), 0, 'f', 3)
                     .arg(feature.center.y(), 0, 'f', 3)
                     .arg(range.bottomZ, 0, 'f', 3)
                     .arg(dwellTime, 0, 'f', 3)
                     .arg(feedRate, 0, 'f', 3);
        totalDepth += depth;
    }
    gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
    result.gcode = gcode;
    result.ok = true;
    result.estimatedTimeS = totalDepth / feedRate * 60.0;
    return result;
}
