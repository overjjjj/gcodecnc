#include "HighSpeedPeckDrillingStrategy.h"

#include "HoleStrategyUtils.h"

namespace {

ProcessParameterDefinition PositiveMillimeter(const QString &id, double value)
{
    ProcessParameterDefinition definition;
    definition.id = id;
    definition.unit = ProcessParameterUnit::Millimeter;
    definition.defaultValue = value;
    definition.minimum = 0.0;
    definition.hasMinimum = true;
    definition.required = true;
    return definition;
}

} // namespace

ProcessParameterSchema HighSpeedPeckDrillingStrategy::parameterSchema() const
{
    ProcessParameterSchema schema = ProcessParameterSchema::CommonOperation();
    schema.addDefinition(PositiveMillimeter(QStringLiteral("peckDepth"), 3.0));
    schema.addDefinition(PositiveMillimeter(QStringLiteral("retractDelta"), 0.5));
    return schema;
}

StrategyParams HighSpeedPeckDrillingStrategy::defaultParams() const
{
    StrategyParams params;
    params.set(QStringLiteral("safeHeight"), 50.0);
    params.set(QStringLiteral("feedHeight"), 3.0);
    params.set(QStringLiteral("depth"), 20.0);
    params.set(QStringLiteral("peckDepth"), 3.0);
    params.set(QStringLiteral("retractDelta"), 0.5);
    params.set(QStringLiteral("spindleSpeed"), 800.0);
    params.set(QStringLiteral("feedRate"), 60.0);
    return params;
}

ToolpathResult HighSpeedPeckDrillingStrategy::generate(
    const HoleFeature &feature,
    const ToolEntry &tool,
    const StrategyParams &params) const
{
    return generate(QVector<HoleFeature>{feature}, tool, params);
}

ToolpathResult HighSpeedPeckDrillingStrategy::generate(
    const QVector<HoleFeature> &features,
    const ToolEntry &tool,
    const StrategyParams &params) const
{
    ToolpathResult result;
    const double peckDepth = params.get(QStringLiteral("peckDepth"), 3.0);
    const double retractDelta = params.get(QStringLiteral("retractDelta"), 0.5);
    const double feedRate = params.get(QStringLiteral("feedRate"), 60.0);
    if (features.isEmpty()) {
        result.errorMsg = QObject::tr("未选择孔位。");
        return result;
    }
    if (tool.id <= 0 || tool.type != QStringLiteral("drill") || tool.diameter <= 0.0) {
        result.errorMsg = QObject::tr("G73 必须使用几何有效的钻头。");
        return result;
    }
    if (peckDepth <= 0.0 || retractDelta <= 0.0 || feedRate <= 0.0) {
        result.errorMsg = QObject::tr("G73 单次深度、循环退刀量和进给必须大于零。");
        return result;
    }

    const double safeHeight = params.get(QStringLiteral("safeHeight"), 50.0);
    const double feedHeight = params.get(QStringLiteral("feedHeight"), 3.0);
    const double spindleSpeed = params.get(QStringLiteral("spindleSpeed"), 800.0);
    const QVector<HoleFeature> ordered = sortHolesByNearestNeighbor(features);
    QString gcode;
    gcode += QStringLiteral("T%1 M6\n").arg(tool.id);
    gcode += QStringLiteral("S%1 M3\n").arg(static_cast<int>(spindleSpeed));
    gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
    double totalDepth = 0.0;
    for (const HoleFeature &feature : ordered) {
        if (feature.radius <= 0.0 || feature.depth <= 0.0) {
            result.errorMsg = QObject::tr("G73 孔几何无效，无法确定安全深度。");
            return result;
        }
        const double depth = effectiveDrillDepth(
            feature, params.get(QStringLiteral("depth"), feature.depth));
        const HoleZRange range = holeZRange(feature, depth, feedHeight);
        gcode += QStringLiteral(
                     ";CNEXT_HOLE_CYCLE code=G73 rtp=%1 rfp=%2 sdis=%3 x=%4 y=%5 "
                     "z=%6 q=%7 retract=%8 p=0 f=%9 vari=0\n")
                     .arg(safeHeight, 0, 'f', 3)
                     .arg(range.entryZ, 0, 'f', 3)
                     .arg(feedHeight, 0, 'f', 3)
                     .arg(feature.center.x(), 0, 'f', 3)
                     .arg(feature.center.y(), 0, 'f', 3)
                     .arg(range.bottomZ, 0, 'f', 3)
                     .arg(effectivePeckDepth(feature, peckDepth), 0, 'f', 3)
                     .arg(retractDelta, 0, 'f', 3)
                     .arg(feedRate, 0, 'f', 3);
        totalDepth += depth;
    }
    gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
    result.gcode = gcode;
    result.ok = true;
    result.estimatedTimeS = totalDepth / feedRate * 75.0;
    return result;
}
