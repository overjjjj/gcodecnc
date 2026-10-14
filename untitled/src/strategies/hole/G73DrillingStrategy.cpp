#include "G73DrillingStrategy.h"

#include "HoleStrategyUtils.h"
#include "../../core/GCodeCoordinate.h"

#include <QObject>
#include <cmath>

namespace {

QString validateTool(const HoleFeature &feature, const ToolEntry &tool, double depth)
{
    if (tool.id <= 0 || tool.type != QStringLiteral("drill") ||
        !std::isfinite(tool.diameter) || tool.diameter <= 0.0 ||
        !std::isfinite(tool.fluteLen) || !std::isfinite(tool.totalLen)) {
        return QObject::tr("G73 必须使用编号和直径有效的钻头。");
    }

    const double targetDiameter = holeDiameter(feature);
    const double tolerance = std::max(0.05, targetDiameter * 0.01);
    if (targetDiameter <= 0.0 || tool.diameter > targetDiameter + tolerance) {
        return QObject::tr("G73 钻头直径不能大于目标孔径。");
    }
    if (tool.fluteLen > 0.0 && depth > tool.fluteLen + 0.01) {
        return QObject::tr("G73 加工深度超过刀具有效刃长。");
    }
    if (tool.totalLen > 0.0 && depth >= tool.totalLen - 0.01) {
        return QObject::tr("G73 加工深度达到刀具总长，无法形成安全伸出。");
    }
    return QString();
}

} // namespace

StrategyParams G73DrillingStrategy::defaultParams() const
{
    StrategyParams params;
    params.set(QStringLiteral("safeHeight"), 50.0);
    params.set(QStringLiteral("feedHeight"), 3.0);
    params.set(QStringLiteral("peckDepth"), 3.0);
    params.set(QStringLiteral("spindleSpeed"), 800.0);
    params.set(QStringLiteral("feedRate"), 60.0);
    params.set(QStringLiteral("dwellTime"), 0.0);
    params.set(QStringLiteral("coolantMode"), 1.0);
    return params;
}

ToolpathResult G73DrillingStrategy::generate(const HoleFeature &feature,
                                             const ToolEntry &tool,
                                             const StrategyParams &params) const
{
    return generate(QVector<HoleFeature>{feature}, tool, params);
}

ToolpathResult G73DrillingStrategy::generate(const QVector<HoleFeature> &features,
                                             const ToolEntry &tool,
                                             const StrategyParams &params) const
{
    ToolpathResult result;
    result.errorMsg = holeSpindleOutputError(id(), params);
    if (!result.errorMsg.isEmpty()) return result;
    result.errorMsg = holeAxisOutputError(features, id(), params);
    if (!result.errorMsg.isEmpty()) return result;
    if (features.isEmpty()) {
        result.errorMsg = QObject::tr("未选择孔位。");
        return result;
    }

    const double safeHeight = params.get(QStringLiteral("safeHeight"), 50.0);
    const double feedHeight = params.get(QStringLiteral("feedHeight"), 3.0);
    const double peckDepth = params.get(QStringLiteral("peckDepth"), 3.0);
    const double spindleSpeed = params.get(QStringLiteral("spindleSpeed"), 800.0);
    const double feedRate = params.get(QStringLiteral("feedRate"), 60.0);
    const double dwellTime = params.get(QStringLiteral("dwellTime"), 0.0);
    const double coolantValue = params.get(QStringLiteral("coolantMode"), 1.0);
    if (!std::isfinite(safeHeight) || !std::isfinite(feedHeight) ||
        !std::isfinite(peckDepth) || !std::isfinite(spindleSpeed) ||
        !std::isfinite(feedRate) || !std::isfinite(dwellTime) ||
        !std::isfinite(coolantValue) || safeHeight <= 0.0 || feedHeight <= 0.0 ||
        peckDepth <= 0.0 || spindleSpeed <= 0.0 || feedRate <= 0.0 ||
        dwellTime < 0.0 || coolantValue < 0.0 || coolantValue > 2.0) {
        result.errorMsg = QObject::tr("G73 参数无效，请检查安全高度、Q、主轴和进给速度。");
        return result;
    }
    const int coolantMode = static_cast<int>(std::round(coolantValue));
    if (params.values.contains(QStringLiteral("depth")) &&
        params.get(QStringLiteral("depth"), 0.0) <= 0.0) {
        result.errorMsg = QObject::tr("G73 加工深度必须大于零。");
        return result;
    }

    const QVector<HoleFeature> orderedFeatures = sortHolesByNearestNeighbor(features);
    const bool explicitDepth = params.values.contains(QStringLiteral("depth"));
    QString gcode;
    gcode += QStringLiteral("T%1 M6\n").arg(tool.id);
    gcode += QStringLiteral("S%1 M3\n").arg(static_cast<int>(std::round(spindleSpeed)));
    gcode += QStringLiteral("G0 G43 H%1 Z%2\n")
                 .arg(tool.id)
                 .arg(formatGCodeCoordinate(safeHeight));
    if (coolantMode > 0) {
        gcode += coolantMode == 2 ? QStringLiteral("M7\n") : QStringLiteral("M8\n");
    }

    double estimatedDepth = 0.0;
    QStringList emittedNotes;
    for (const HoleFeature &feature : orderedFeatures) {
        const double requestedDepth = params.get(QStringLiteral("depth"), feature.depth);
        const double totalDepth = effectiveG73DrillDepth(feature, requestedDepth, explicitDepth);
        if (!std::isfinite(totalDepth) || totalDepth <= 0.0) {
            result.errorMsg = QObject::tr("G73 缺少有效孔深。");
            return result;
        }
        if (peckDepth > totalDepth + 0.01) {
            result.errorMsg = QObject::tr("G73 单次钻深 Q 不能大于加工深度。");
            return result;
        }

        const QString toolError = validateTool(feature, tool, totalDepth);
        if (!toolError.isEmpty()) {
            result.errorMsg = toolError;
            return result;
        }

        const HoleZRange zRange = holeZRange(feature, totalDepth, feedHeight);
        if (safeHeight <= zRange.retractZ) {
            result.errorMsg = QObject::tr("G73 安全高度必须高于循环返回平面。");
            return result;
        }
        const double adaptedPeck = effectivePeckDepth(feature, peckDepth);
        if (!std::isfinite(adaptedPeck) || adaptedPeck <= 0.0) {
            result.errorMsg = QObject::tr("G73 单次钻深 Q 无效。");
            return result;
        }

        const QString note = drillUndersizeComment(feature, tool);
        if (!note.isEmpty() && !emittedNotes.contains(note)) {
            gcode += note;
            emittedNotes.append(note);
        }
        gcode += QStringLiteral(
            ";CNEXT_HOLE_CYCLE code=G73 rtp=%1 rfp=%2 sdis=%3 x=%4 y=%5 z=%6 q=%7 p=%8 f=%9 vari=0\n")
                     .arg(safeHeight, 0, 'g', 17)
                     .arg(zRange.entryZ, 0, 'g', 17)
                     .arg(feedHeight, 0, 'g', 17)
                     .arg(feature.center.x(), 0, 'g', 17)
                     .arg(feature.center.y(), 0, 'g', 17)
                     .arg(zRange.bottomZ, 0, 'g', 17)
                     .arg(adaptedPeck, 0, 'g', 17)
                     .arg(dwellTime, 0, 'f', 3)
                     .arg(feedRate, 0, 'f', 3);
        estimatedDepth += totalDepth;
    }
    gcode += QStringLiteral("G0 Z%1\n").arg(formatGCodeCoordinate(safeHeight));

    result.ok = true;
    result.gcode = gcode;
    result.estimatedTimeS = (estimatedDepth / feedRate * 60.0) * 1.25;
    return result;
}
