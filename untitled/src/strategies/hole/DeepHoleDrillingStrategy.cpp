#include "DeepHoleDrillingStrategy.h"
#include "HoleStrategyUtils.h"
#include <QObject>
#include <cmath>

StrategyParams DeepHoleDrillingStrategy::defaultParams() const
{
    StrategyParams p;
    p.set("safeHeight",      50.0);
    p.set("feedHeight",       3.0);
    p.set("peckDepth",        2.0);    // small peck for deep hole (chip-breaking)
    p.set("spindleSpeed",   600.0);
    p.set("feedRate",        40.0);
    p.set("dwellTime",        0.3);    // dwell at bottom (s)
    return p;
}

ToolpathResult DeepHoleDrillingStrategy::generate(const HoleFeature &feature,
                                                   const ToolEntry   &tool,
                                                   const StrategyParams &params) const
{
    return generate(QVector<HoleFeature>{feature}, tool, params);
}

ToolpathResult DeepHoleDrillingStrategy::generate(const QVector<HoleFeature> &features,
                                                   const ToolEntry            &tool,
                                                   const StrategyParams       &params) const
{
    ToolpathResult res;
    if (features.isEmpty()) {
        res.errorMsg = QObject::tr("未选择孔位。");
        return res;
    }

    const double safe   = params.get("safeHeight",      50.0);
    const double feed   = params.get("feedHeight",       3.0);
    const double peck   = params.get("peckDepth",        2.0);
    const double S      = params.get("spindleSpeed",   600.0);
    const double F      = params.get("feedRate",        40.0);
    const double dwell  = params.get("dwellTime",        0.3);
    if (peck <= 0.0 || F <= 0.0) {
        res.errorMsg = QObject::tr("深孔钻参数无效，请检查每刀钻深和进给速度。");
        return res;
    }

    const QVector<HoleFeature> orderedFeatures = sortHolesByNearestNeighbor(features);

    QString gc;
    gc += QString("T%1 M6\n").arg(tool.id);
    gc += QString("S%1 M3\n").arg((int)S);
    gc += QString("G0 Z%1\n").arg(safe, 0, 'f', 3);

    double estimatedDepth = 0.0;
    QStringList emittedNotes;
    for (int i = 0; i < orderedFeatures.size(); ++i) {
        const HoleFeature &feature = orderedFeatures[i];
        const QString note = drillUndersizeComment(feature, tool);
        if (!note.isEmpty() && !emittedNotes.contains(note)) {
            gc += note;
            emittedNotes.append(note);
        }
        const double total = effectiveDrillDepth(feature, params.get("depth", 50.0));
        const double adaptedPeck = effectivePeckDepth(feature, peck);
        const HoleZRange zRange = holeZRange(feature, total, feed);
        const double x = feature.center.x();
        const double y = feature.center.y();
        estimatedDepth += total;

        gc += QString(";CNEXT_HOLE_CYCLE code=G83 rtp=%1 rfp=%2 sdis=%3 x=%4 y=%5 z=%6 q=%7 p=%8 f=%9 vari=1\n")
                  .arg(safe, 0, 'f', 3)
                  .arg(zRange.entryZ, 0, 'f', 3)
                  .arg(feed, 0, 'f', 3)
                  .arg(x, 0, 'f', 3)
                  .arg(y, 0, 'f', 3)
                  .arg(zRange.bottomZ, 0, 'f', 3)
                  .arg(adaptedPeck, 0, 'f', 3)
                  .arg(dwell, 0, 'f', 3)
                  .arg(F, 0, 'f', 3);
    }
    gc += QString("G0 Z%1\n").arg(safe, 0, 'f', 3);

    res.gcode = gc;
    res.ok    = true;
    res.estimatedTimeS = (estimatedDepth / F * 60.0) * 2.0;
    return res;
}
