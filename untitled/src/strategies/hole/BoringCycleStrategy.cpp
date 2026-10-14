#include "BoringCycleStrategy.h"

#include "HoleStrategyUtils.h"
#include "../../core/GCodeCoordinate.h"

#include <QObject>
#include <cmath>
#include <utility>

BoringCycleStrategy::BoringCycleStrategy(QString strategyId,
                                         QString cycleCode,
                                         bool stopSpindleBeforeRetract)
    : m_strategyId(std::move(strategyId))
    , m_cycleCode(std::move(cycleCode))
    , m_stopSpindleBeforeRetract(stopSpindleBeforeRetract)
{
}

StrategyParams BoringCycleStrategy::defaultParams() const
{
    StrategyParams params;
    params.set(QStringLiteral("safeHeight"), 50.0);
    params.set(QStringLiteral("feedHeight"), 3.0);
    params.set(QStringLiteral("depth"), 20.0);
    params.set(QStringLiteral("spindleSpeed"), 400.0);
    params.set(QStringLiteral("feedRate"), 40.0);
    params.set(QStringLiteral("boringOffset"), m_cycleCode == QStringLiteral("G76") ? 0.1 : 0.0);
    params.set(QStringLiteral("coolantMode"), 1.0);
    return params;
}

ToolpathResult BoringCycleStrategy::generate(const HoleFeature &feature,
                                             const ToolEntry &tool,
                                             const StrategyParams &params) const
{
    return generate(QVector<HoleFeature>{feature}, tool, params);
}

ToolpathResult BoringCycleStrategy::generate(const QVector<HoleFeature> &features,
                                             const ToolEntry &tool,
                                             const StrategyParams &params) const
{
    ToolpathResult result;
    result.errorMsg = holeSpindleOutputError(id(), params);
    if (!result.errorMsg.isEmpty()) return result;
    result.errorMsg = holeAxisOutputError(features, id(), params);
    if (!result.errorMsg.isEmpty()) return result;
    if (features.isEmpty()) {
        result.errorMsg = QObject::tr("未选择孔位，无法生成镗孔循环。");
        return result;
    }
    if (tool.id <= 0 || tool.type != QStringLiteral("boring_bar") ||
        !std::isfinite(tool.diameter) || tool.diameter <= 0.0) {
        result.errorMsg = QObject::tr("G76/G86 必须使用有效的镗刀杆。");
        return result;
    }

    const double safe = params.get(QStringLiteral("safeHeight"), 50.0);
    const double feedHeight = params.get(QStringLiteral("feedHeight"), 3.0);
    const double depth = params.get(QStringLiteral("depth"), 20.0);
    const double spindle = params.get(QStringLiteral("spindleSpeed"), 400.0);
    const double feed = params.get(QStringLiteral("feedRate"), 40.0);
    const double offset = params.get(QStringLiteral("boringOffset"), 0.0);
    const double coolantValue = params.get(QStringLiteral("coolantMode"), 1.0);
    if (!std::isfinite(safe) || !std::isfinite(feedHeight) || !std::isfinite(depth) ||
        !std::isfinite(spindle) || !std::isfinite(feed) || !std::isfinite(offset) ||
        !std::isfinite(coolantValue) ||
        safe <= 0.0 || feedHeight <= 0.0 || depth <= 0.0 || spindle <= 0.0 ||
        feed <= 0.0 || offset < 0.0 || coolantValue < 0.0 || coolantValue > 2.0 ||
        (m_cycleCode == QStringLiteral("G76") && offset <= 0.0)) {
        result.errorMsg = QObject::tr("G76/G86 参数无效，请检查深度、偏移、主轴和进给。");
        return result;
    }
    if (!std::isfinite(tool.fluteLen) || !std::isfinite(tool.totalLen) ||
        tool.fluteLen <= 0.0 || tool.totalLen <= 0.0) {
        result.errorMsg = QObject::tr("镗刀杆必须填写正数刃长和总长，才能校验伸出量。");
        return result;
    }
    if (depth > tool.fluteLen + 0.01 || depth >= tool.totalLen - 0.01) {
        result.errorMsg = QObject::tr("镗孔深度超过镗刀杆刃长或安全总长。");
        return result;
    }

    const QVector<HoleFeature> ordered = sortHolesByNearestNeighbor(features);
    QString gcode;
    gcode += QStringLiteral("T%1 M6\nS%2 M3\nG0 Z%3\n")
        .arg(tool.id).arg(static_cast<int>(std::round(spindle))).arg(formatGCodeCoordinate(safe));
    const int coolantMode = static_cast<int>(std::round(coolantValue));
    if (coolantMode > 0) {
        gcode += coolantMode == 2 ? QStringLiteral("M7\n") : QStringLiteral("M8\n");
    }
    for (const HoleFeature &feature : ordered) {
        const HoleZRange range = holeZRange(feature, depth, feedHeight);
        if (safe <= range.retractZ) {
            result.errorMsg = QObject::tr("安全高度必须高于镗孔返回平面。");
            return result;
        }
        gcode += QStringLiteral(";CNEXT_HOLE_CYCLE code=%1 rtp=%2 rfp=%3 sdis=%4 x=%5 y=%6 z=%7 q=%8 p=%9 f=%10 vari=0\n")
            .arg(m_cycleCode)
            .arg(safe, 0, 'g', 17)
            .arg(range.entryZ, 0, 'g', 17)
            .arg(feedHeight, 0, 'g', 17)
            .arg(feature.center.x(), 0, 'g', 17)
            .arg(feature.center.y(), 0, 'g', 17)
            .arg(range.bottomZ, 0, 'g', 17)
            .arg(offset, 0, 'g', 17)
            .arg(m_stopSpindleBeforeRetract ? 1.0 : 0.0, 0, 'f', 0)
            .arg(feed, 0, 'f', 3);
    }
    gcode += QStringLiteral("G0 Z%1\n").arg(formatGCodeCoordinate(safe));
    result.ok = true;
    result.gcode = gcode;
    result.estimatedTimeS = (depth / feed * 60.0 + 2.0) * ordered.size();
    return result;
}
