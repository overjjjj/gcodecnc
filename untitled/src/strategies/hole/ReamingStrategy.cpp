#include "ReamingStrategy.h"
#include "HoleStrategyUtils.h"
#include <QObject>

StrategyParams ReamingStrategy::defaultParams() const
{
    StrategyParams p;
    p.set("safeHeight",   50.0);
    p.set("feedHeight",    3.0);
    p.set("spindleSpeed", 300.0);
    p.set("feedRate",      30.0);
    p.set("dwellTime",      0.0);
    return p;
}

ToolpathResult ReamingStrategy::generate(const HoleFeature &feature,
                                          const ToolEntry   &tool,
                                          const StrategyParams &params) const
{
    return generate(QVector<HoleFeature>{feature}, tool, params);
}

ToolpathResult ReamingStrategy::generate(const QVector<HoleFeature> &features,
                                          const ToolEntry            &tool,
                                          const StrategyParams       &params) const
{
    ToolpathResult res;
    if (features.isEmpty()) {
        res.errorMsg = QObject::tr("未选择孔位。");
        return res;
    }

    const double safe  = params.get("safeHeight",   50.0);
    const double feed  = params.get("feedHeight",    3.0);
    const double S     = params.get("spindleSpeed", 300.0);
    const double F     = params.get("feedRate",      30.0);
    const double dwell = params.get("dwellTime",      0.0);
    if (F <= 0.0) {
        res.errorMsg = QObject::tr("铰孔进给速度必须大于零。");
        return res;
    }

    const QVector<HoleFeature> orderedFeatures = sortHolesByNearestNeighbor(features);
    for (const HoleFeature &feature : orderedFeatures) {
        const QString validationError = validateReamerForHole(feature, tool);
        if (!validationError.isEmpty()) {
            res.errorMsg = validationError;
            return res;
        }
    }

    QString gc;
    gc += QString("T%1 M6\n").arg(tool.id);
    gc += QString("S%1 M3\n").arg((int)S);
    gc += QString("G0 Z%1\n").arg(safe, 0, 'f', 3);

    double estimatedDepth = 0.0;
    for (const HoleFeature &feature : orderedFeatures) {
        const double depth = feature.depth > 0 ? feature.depth : 20.0;
        const HoleZRange zRange = holeZRange(feature, depth, feed);
        estimatedDepth += depth;
        gc += QString(";CNEXT_HOLE_CYCLE code=G85 rtp=%1 rfp=%2 sdis=%3 x=%4 y=%5 z=%6 q=0 p=%7 f=%8 vari=0\n")
                  .arg(safe, 0, 'f', 3)
                  .arg(zRange.entryZ, 0, 'f', 3)
                  .arg(feed, 0, 'f', 3)
                  .arg(feature.center.x(), 0, 'f', 3)
                  .arg(feature.center.y(), 0, 'f', 3)
                  .arg(zRange.bottomZ, 0, 'f', 3)
                  .arg(dwell, 0, 'f', 3)
                  .arg(F, 0, 'f', 3);
    }
    gc += QString("G0 Z%1\n").arg(safe, 0, 'f', 3);

    res.gcode = gc;
    res.ok    = true;
    res.estimatedTimeS = (estimatedDepth / F * 60.0) * 2.0;
    return res;
}
