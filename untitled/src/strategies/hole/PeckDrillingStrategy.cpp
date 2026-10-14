#include "PeckDrillingStrategy.h"
#include "HoleStrategyUtils.h"
#include "../../core/GCodeCoordinate.h"
#include <QObject>
#include <cmath>

// 函数说明：defaultParams负责当前模块的核心处理。
StrategyParams PeckDrillingStrategy::defaultParams() const
{
    StrategyParams p;
    p.set("safeHeight",   50.0);
    p.set("feedHeight",    3.0);
    p.set("peckDepth",     3.0);    // depth per peck (mm)
    p.set("retractHeight", 3.0);    // retract above hole top between pecks
    p.set("spindleSpeed", 800.0);
    p.set("feedRate",      60.0);
    p.set("dwellTime",     0.0);
    p.set("coolantMode",    1.0);
    return p;
}

// 函数说明：generate负责当前模块的核心处理。
ToolpathResult PeckDrillingStrategy::generate(const HoleFeature &feature,
                                               const ToolEntry   &tool,
                                               const StrategyParams &params) const
{
    return generate(QVector<HoleFeature>{feature}, tool, params);
}

// 函数说明：generate负责当前模块的核心处理。
ToolpathResult PeckDrillingStrategy::generate(const QVector<HoleFeature> &features,
                                               const ToolEntry            &tool,
                                               const StrategyParams       &params) const
{
    ToolpathResult res;
    if (features.isEmpty()) {
        res.errorMsg = QObject::tr("未选择孔位。");
        return res;
    }

    const double safe  = params.get("safeHeight",    50.0);
    const double feed  = params.get("feedHeight",     3.0);
    const double peck  = params.get("peckDepth",      3.0);
    const double S     = params.get("spindleSpeed",  800.0);
    const double F     = params.get("feedRate",       60.0);
    const double dwell = params.get("dwellTime",      0.0);
    const double coolantValue = params.get("coolantMode", 1.0);
    res.errorMsg = holeSpindleOutputError(id(), params);
    if (!res.errorMsg.isEmpty()) return res;
    res.errorMsg = holeAxisOutputError(features, id(), params);
    if (!res.errorMsg.isEmpty()) return res;
    if (!std::isfinite(coolantValue) || coolantValue < 0.0 || coolantValue > 2.0 ||
        peck <= 0.0 || F <= 0.0) {
        res.errorMsg = QObject::tr("排屑钻参数无效，请检查每刀钻深和进给速度。");
        return res;
    }

    const QVector<HoleFeature> orderedFeatures = sortHolesByNearestNeighbor(features);

    QString gc;
    gc += QString("T%1 M6\n").arg(tool.id);
    gc += QString("S%1 M3\n").arg((int)S);
    gc += QString("G0 Z%1\n").arg(formatGCodeCoordinate(safe));
    const int coolantMode = int(std::round(coolantValue));
    if (coolantMode > 0) gc += coolantMode == 2 ? QStringLiteral("M7\n") : QStringLiteral("M8\n");

    double estimatedDepth = 0.0;
    QStringList emittedNotes;
    for (int i = 0; i < orderedFeatures.size(); ++i) {
        const HoleFeature &feature = orderedFeatures[i];
        const QString note = drillUndersizeComment(feature, tool);
        if (!note.isEmpty() && !emittedNotes.contains(note)) {
            gc += note;
            emittedNotes.append(note);
        }
        const double total = effectiveDrillDepth(
            feature, params.get("depth", 20.0),
            params.values.contains(QStringLiteral("depth")));
        const double adaptedPeck = peckDepthForParameters(feature, params);
        const QString peckNote = QString("; Peck Q upper limit=%1, effective Q=%2, cutting depth=%3 mm\n")
            .arg(peck, 0, 'f', 3).arg(adaptedPeck, 0, 'f', 3).arg(total, 0, 'f', 3);
        if (!emittedNotes.contains(peckNote)) { gc += peckNote; emittedNotes.append(peckNote); }
        const HoleZRange zRange = holeZRange(feature, total, feed);
        const double x = feature.center.x();
        const double y = feature.center.y();
        estimatedDepth += total;

        gc += QString(";CNEXT_HOLE_CYCLE code=G83 rtp=%1 rfp=%2 sdis=%3 x=%4 y=%5 z=%6 q=%7 p=%8 f=%9 vari=1\n")
                  .arg(safe, 0, 'g', 17)
                  .arg(zRange.entryZ, 0, 'g', 17)
                  .arg(feed, 0, 'g', 17)
                  .arg(x, 0, 'g', 17)
                  .arg(y, 0, 'g', 17)
                  .arg(zRange.bottomZ, 0, 'g', 17)
                  .arg(adaptedPeck, 0, 'g', 17)
                  .arg(dwell, 0, 'f', 3)
                  .arg(F, 0, 'f', 3);
    }
    gc += QString("G0 Z%1\n").arg(formatGCodeCoordinate(safe));

    res.gcode = gc;
    res.ok    = true;
    res.estimatedTimeS = (estimatedDepth / F * 60.0) * 1.5;
    return res;
}
