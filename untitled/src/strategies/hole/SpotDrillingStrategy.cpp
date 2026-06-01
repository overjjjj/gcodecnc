#include "SpotDrillingStrategy.h"
#include "HoleStrategyUtils.h"
#include <QObject>
#include <cmath>

StrategyParams SpotDrillingStrategy::defaultParams() const
{
    StrategyParams p;
    p.set("safeHeight",   50.0);
    p.set("feedHeight",    3.0);
    p.set("depth",         2.0);   // spot depth
    p.set("spindleSpeed", 1000.0); // rpm
    p.set("feedRate",      80.0);  // mm/min
    p.set("dwellTime",     0.0);   // ms dwell at bottom
    return p;
}

ToolpathResult SpotDrillingStrategy::generate(const HoleFeature &feature,
                                               const ToolEntry   &tool,
                                               const StrategyParams &params) const
{
    return generate(QVector<HoleFeature>{feature}, tool, params);
}

ToolpathResult SpotDrillingStrategy::generate(const QVector<HoleFeature> &features,
                                               const ToolEntry            &tool,
                                               const StrategyParams       &params) const
{
    ToolpathResult res;
    if (features.isEmpty()) {
        res.errorMsg = QObject::tr("未选择孔位，无法生成孔定点代码。");
        return res;
    }

    const double safe  = params.get("safeHeight",   50.0);
    const double feed  = params.get("feedHeight",    3.0);
    const double depth = params.get("depth",         2.0);
    const double S     = params.get("spindleSpeed", 1000.0);
    const double F     = params.get("feedRate",      80.0);
    const double dwell = params.get("dwellTime",     0.0);
    if (depth <= 0.0 || F <= 0.0) {
        res.errorMsg = QObject::tr("孔定点参数无效，请检查定点深度和进给速度。");
        return res;
    }

    const QVector<HoleFeature> orderedFeatures = sortHolesByNearestNeighbor(features);
    const QString cycleCode = dwell > 0.0 ? QStringLiteral("G82") : QStringLiteral("G81");

    QString gc;
    gc += QString("T%1 M6\n").arg(tool.id);
    gc += QString("S%1 M3\n").arg((int)S);
    gc += QString("G0 Z%1\n").arg(safe, 0, 'f', 3);

    for (int i = 0; i < orderedFeatures.size(); ++i) {
        const HoleFeature &feature = orderedFeatures[i];
        const double x = feature.center.x();
        const double y = feature.center.y();
        const HoleZRange zRange = holeZRange(feature, depth, feed);

        gc += QString(";CNEXT_HOLE_CYCLE code=%1 rtp=%2 rfp=%3 sdis=%4 x=%5 y=%6 z=%7 q=0 p=%8 f=%9 vari=0\n")
                  .arg(cycleCode)
                  .arg(safe, 0, 'f', 3)
                  .arg(zRange.entryZ, 0, 'f', 3)
                  .arg(feed, 0, 'f', 3)
                  .arg(x, 0, 'f', 3)
                  .arg(y, 0, 'f', 3)
                  .arg(zRange.bottomZ, 0, 'f', 3)
                  .arg(dwell, 0, 'f', 3)
                  .arg(F, 0, 'f', 3);
    }
    gc += QString("G0 Z%1\n").arg(safe, 0, 'f', 3);

    res.gcode = gc;
    res.ok    = true;
    res.estimatedTimeS = ((depth / F * 60.0) + 2.0) * orderedFeatures.size();
    return res;
}
