#include "CircleMillingStrategy.h"

#include <QObject>
#include <cmath>

StrategyParams CircleMillingStrategy::defaultParams() const
{
    StrategyParams p;
    p.set(QStringLiteral("safeHeight"), 50.0);
    p.set(QStringLiteral("feedHeight"), 3.0);
    p.set(QStringLiteral("stepDown"), 1.0);
    p.set(QStringLiteral("spindleSpeed"), 3000.0);
    p.set(QStringLiteral("feedRate"), 1500.0);
    p.set(QStringLiteral("plungeRate"), 300.0);
    p.set(QStringLiteral("compensation"), 1.0);
    p.set(QStringLiteral("stockToLeave"), 0.0);
    return p;
}

ToolpathResult CircleMillingStrategy::generate(const HoleFeature &/*feature*/,
                                               const ToolEntry &/*tool*/,
                                               const StrategyParams &/*params*/) const
{
    return {QString(), false, QObject::tr("铣边策略需要轮廓特征。"), 0.0};
}

ToolpathResult CircleMillingStrategy::generate(const ContourFeature &feature,
                                               const ToolEntry &tool,
                                               const StrategyParams &params) const
{
    ToolpathResult res;

    if (feature.radius <= 0.0) {
        res.errorMsg = QObject::tr("圆半径必须大于零。");
        return res;
    }
    if (tool.diameter <= 0.0) {
        res.errorMsg = QObject::tr("刀具直径无效。");
        return res;
    }
    if (feature.depth <= 0.0) {
        res.errorMsg = QObject::tr("加工深度必须大于零。");
        return res;
    }

    const double stepDown = params.get(QStringLiteral("stepDown"), 1.0);
    if (stepDown <= 0.0) {
        res.errorMsg = QObject::tr("每层切深必须大于零。");
        return res;
    }

    const double cx = feature.center.x();
    const double cy = feature.center.y();
    const double ztop = feature.center.z();

    const double safeH = params.get(QStringLiteral("safeHeight"), 50.0);
    const double feedH = params.get(QStringLiteral("feedHeight"), 3.0);
    const double spindleSpeed = params.get(QStringLiteral("spindleSpeed"), 3000.0);
    const double feedRate = params.get(QStringLiteral("feedRate"), 1500.0);
    const double plungeRate = params.get(QStringLiteral("plungeRate"), 300.0);
    const double comp = params.get(QStringLiteral("compensation"), 1.0);
    const double stock = params.get(QStringLiteral("stockToLeave"), 0.0);

    if (feedRate <= 0.0 || plungeRate <= 0.0) {
        res.errorMsg = QObject::tr("进给速率必须大于零。");
        return res;
    }

    const double entryX = cx + feature.radius + stock;
    const double entryY = cy;
    const double leadLength = std::max(tool.diameter, 1.0);
    const double leadX = entryX + leadLength;
    const double leadY = entryY;
    const int passes = static_cast<int>(std::ceil(feature.depth / stepDown));
    const QString compCode = comp > 0.0 ? QStringLiteral("G41") : QStringLiteral("G42");

    QString gc;
    gc.reserve(256 * passes);

    gc += QStringLiteral("T%1 M6\n").arg(tool.id);
    gc += QStringLiteral("S%1 M3\n").arg(static_cast<int>(spindleSpeed));
    gc += QStringLiteral("G0 Z%1\n").arg(safeH, 0, 'f', 3);
    gc += QStringLiteral("G0 X%1 Y%2\n").arg(leadX, 0, 'f', 3).arg(leadY, 0, 'f', 3);

    double totalTime = 0.0;
    for (int i = 1; i <= passes; ++i) {
        const double zLayer = ztop - std::min(i * stepDown, feature.depth);

        gc += QStringLiteral("G1 Z%1 F%2\n").arg(zLayer, 0, 'f', 3).arg(static_cast<int>(plungeRate));
        gc += QStringLiteral("G1 %1 D%2 X%3 Y%4 F%5\n")
                  .arg(compCode)
                  .arg(tool.id)
                  .arg(entryX, 0, 'f', 3)
                  .arg(entryY, 0, 'f', 3)
                  .arg(static_cast<int>(feedRate));

        const double iOffset = cx - entryX;
        gc += QStringLiteral("G2 X%1 Y%2 I%3 J0.000 F%4\n")
                  .arg(entryX, 0, 'f', 3)
                  .arg(entryY, 0, 'f', 3)
                  .arg(iOffset, 0, 'f', 3)
                  .arg(static_cast<int>(feedRate));

        gc += QStringLiteral("G1 G40 X%1 Y%2 F%3\n")
                  .arg(leadX, 0, 'f', 3)
                  .arg(leadY, 0, 'f', 3)
                  .arg(static_cast<int>(feedRate));
        if (i < passes) {
            gc += QStringLiteral("G0 Z%1\n").arg(ztop + feedH, 0, 'f', 3);
        }

        totalTime += ((2.0 * std::acos(-1.0) * feature.radius + 2.0 * leadLength) /
                      feedRate * 60.0)
                   + (std::min(stepDown, feature.depth) / plungeRate * 60.0);
    }

    gc += QStringLiteral("G0 Z%1\n").arg(safeH, 0, 'f', 3);

    res.gcode = gc;
    res.ok = true;
    res.estimatedTimeS = totalTime;
    return res;
}
