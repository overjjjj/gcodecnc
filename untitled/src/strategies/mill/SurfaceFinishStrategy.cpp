#include "SurfaceFinishStrategy.h"
#include <QObject>
#include <algorithm>
#include <cmath>

namespace {

static double planarSpan(double explicitValue, double fallbackRadius, double fallbackToolDiameter)
{
    if (explicitValue > 0.0) {
        return explicitValue;
    }
    if (fallbackRadius > 0.0) {
        return fallbackRadius * 2.0;
    }
    return fallbackToolDiameter * 6.0;
}

}

StrategyParams SurfaceFinishStrategy::defaultParams() const
{
    StrategyParams p;
    p.set("safeHeight",    50.0);
    p.set("feedHeight",     3.0);
    p.set("stepover",       0.5);  // very fine lateral step for surface quality
    p.set("spindleSpeed", 4000.0);
    p.set("feedRate",      500.0);
    p.set("plungeRate",    150.0);
    p.set("stockToLeave",    0.0);
    return p;
}

ToolpathResult SurfaceFinishStrategy::generate(const HoleFeature &,
                                                const ToolEntry &,
                                                const StrategyParams &) const
{
    return {QString(), false, QObject::tr("精面策略需要轮廓特征。"), 0.0};
}

// Generates a fine-pitch raster finish pass at a single Z depth (surface milling).
// Suitable for flat surfaces after roughing; analogous to face milling but with
// much smaller stepover for better surface finish (Ra).
ToolpathResult SurfaceFinishStrategy::generate(const ContourFeature &feature,
                                                const ToolEntry      &tool,
                                                const StrategyParams &params) const
{
    ToolpathResult res;
    if (tool.diameter <= 0.0) {
        res.errorMsg = QObject::tr("刀具直径无效。");
        return res;
    }
    if (feature.depth <= 0.0) {
        res.errorMsg = QObject::tr("加工深度必须大于零。");
        return res;
    }

    const double cx = feature.center.x();
    const double cy = feature.center.y();
    const double ztop = feature.center.z();
    const double zcut = ztop - feature.depth;
    const double safe = params.get("safeHeight", 50.0);
    const double feedH = params.get("feedHeight", 3.0);
    const double step = params.get("stepover", 0.5);
    const double S = params.get("spindleSpeed", 4000.0);
    const double F = params.get("feedRate", 500.0);
    const double Fp = params.get("plungeRate", 150.0);
    const double stock = params.get("stockToLeave", 0.0);

    const double spanX = planarSpan(feature.length, feature.radius, tool.diameter);
    const double spanY = planarSpan(feature.width, feature.radius, tool.diameter);
    const double halfX = spanX * 0.5;
    const double halfY = spanY * 0.5;
    const double toolRadius = tool.diameter * 0.5;
    const double lead = toolRadius;
    const double startX = cx - halfX - lead;
    const double endX = cx + halfX + lead;
    const double startY = cy - halfY - lead;
    const double endY = cy + halfY + lead;
    const double effectiveStep = step > 0.0 ? step : tool.diameter * 0.1;
    const int passes = std::max(2, static_cast<int>(std::ceil((endY - startY) / effectiveStep)) + 1);

    QString gc;
    gc += QString("T%1 M6\n").arg(tool.id);
    gc += QString("S%1 M3\n").arg((int)S);
    gc += QString("G0 Z%1\n").arg(safe, 0, 'f', 3);
    gc += QString("G0 X%1 Y%2\n").arg(startX, 0, 'f', 3).arg(startY, 0, 'f', 3);
    gc += QString("G0 Z%1\n").arg(ztop + feedH, 0, 'f', 3);
    gc += QString("G1 Z%1 F%2\n").arg(zcut + stock, 0, 'f', 3).arg((int)Fp);

    double totalLen = 0.0;
    for (int i = 0; i < passes; ++i) {
        const double y = std::min(startY + i * effectiveStep, endY);
        const double x0 = (i % 2 == 0) ? startX : endX;
        const double x1 = (i % 2 == 0) ? endX : startX;
        gc += QString("G1 X%1 Y%2 F%3\n").arg(x0, 0, 'f', 3).arg(y, 0, 'f', 3).arg((int)F);
        gc += QString("G1 X%1\n").arg(x1, 0, 'f', 3);
        totalLen += std::abs(x1 - x0);
    }
    gc += QString("G0 Z%1\n").arg(safe, 0, 'f', 3);

    res.gcode = gc;
    res.ok    = true;
    res.estimatedTimeS = (totalLen / F * 60.0) + (feature.depth / Fp * 60.0);
    return res;
}
