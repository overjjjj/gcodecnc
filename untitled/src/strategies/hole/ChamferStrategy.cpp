#include "ChamferStrategy.h"
#include "HoleStrategyUtils.h"
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

StrategyParams ChamferStrategy::defaultParams() const
{
    StrategyParams p;
    p.set("safeHeight",   50.0);
    p.set("feedHeight",    3.0);
    p.set("chamferWidth",  1.0);
    p.set("chamferAngle", 45.0);
    p.set("leadLength",    1.0);
    p.set("spindleSpeed", 600.0);
    p.set("feedRate",      80.0);
    return p;
}

ToolpathResult ChamferStrategy::generate(const HoleFeature &feature,
                                          const ToolEntry   &tool,
                                          const StrategyParams &params) const
{
    ToolpathResult res;
    if (tool.type != QStringLiteral("chamfer_mill")) {
        res.errorMsg = QStringLiteral("孔倒角必须使用倒角刀，不能使用麻花钻、铰刀或立铣刀。");
        return res;
    }
    if (tool.id <= 0 || tool.diameter <= 0.0) {
        res.errorMsg = QStringLiteral("当前倒角刀无效，请先在刀具库中选择有效倒角刀。");
        return res;
    }
    if (feature.radius <= 0.0) {
        res.errorMsg = QStringLiteral("孔倒角缺少有效孔径，无法生成刀路。");
        return res;
    }

    const double x      = feature.center.x();
    const double y      = feature.center.y();
    const double safe   = params.get("safeHeight",   50.0);
    const double feed   = params.get("feedHeight",    3.0);
    const double cWidth = params.get("chamferWidth",  1.0);
    const double cAngle = params.get("chamferAngle", 45.0);
    const double leadParam = params.get("leadLength", 1.0);
    const double S      = params.get("spindleSpeed", 600.0);
    const double F      = params.get("feedRate",      80.0);

    if (cWidth <= 0.0 || cAngle <= 0.0 || cAngle >= 89.9 || F <= 0.0) {
        res.errorMsg = QStringLiteral("孔倒角参数无效，请检查倒角宽度、角度和进给速度。");
        return res;
    }

    const double zDrop = cWidth / std::tan(cAngle * kPi / 180.0);
    const double toolpathRadius = feature.radius + cWidth - tool.diameter * 0.5;
    if (toolpathRadius <= 0.0) {
        res.errorMsg = QStringLiteral("倒角刀直径过大或倒角宽度过小，无法生成孔口圆弧倒角刀路。");
        return res;
    }

    const HoleZRange zRange = holeZRange(feature, zDrop, feed);
    const double startX = x + toolpathRadius;
    const double leadLength = std::min(std::max(leadParam, 0.0), toolpathRadius * 0.5);
    const double leadInY = y - leadLength;
    const double leadOutY = y + leadLength;

    QString gc;
    gc += QStringLiteral("T%1 M6\n").arg(tool.id);
    gc += QStringLiteral("S%1 M3\n").arg(int(S));
    gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
    gc += QStringLiteral("; Chamfer hole D%1 width %2 with tool D%3\n")
              .arg(holeDiameter(feature), 0, 'f', 3)
              .arg(cWidth, 0, 'f', 3)
              .arg(tool.diameter, 0, 'f', 3);
    gc += QStringLiteral("G0 X%1 Y%2\n").arg(startX, 0, 'f', 3).arg(leadInY, 0, 'f', 3);
    gc += QStringLiteral("G0 Z%1\n").arg(zRange.retractZ, 0, 'f', 3);
    gc += QStringLiteral("; Chamfer tangential lead-in\n");
    gc += QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
              .arg(startX, 0, 'f', 3)
              .arg(y, 0, 'f', 3)
              .arg(zRange.bottomZ, 0, 'f', 3)
              .arg(int(F));
    gc += QStringLiteral("G2 X%1 Y%2 I%3 J0.000 F%4\n")
              .arg(startX, 0, 'f', 3)
              .arg(y, 0, 'f', 3)
              .arg(-toolpathRadius, 0, 'f', 3)
              .arg(int(F));
    gc += QStringLiteral("; Chamfer tangential lead-out\n");
    gc += QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
              .arg(startX, 0, 'f', 3)
              .arg(leadOutY, 0, 'f', 3)
              .arg(zRange.retractZ, 0, 'f', 3)
              .arg(int(F));
    gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);

    res.gcode = gc;
    res.ok    = true;
    res.estimatedTimeS = ((2.0 * kPi * toolpathRadius + 2.0 * leadLength) / F * 60.0) + 2.0;
    return res;
}
