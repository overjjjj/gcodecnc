#include "PocketRoughingStrategy.h"

#include <QObject>
#include <algorithm>
#include <cmath>

namespace {

struct ScanLine {
    double y = 0.0;
    double xMin = 0.0;
    double xMax = 0.0;
};

static QString cutMove(double x, double y, double feed)
{
    return QStringLiteral("G1 X%1 Y%2 F%3\n")
        .arg(x, 0, 'f', 3)
        .arg(y, 0, 'f', 3)
        .arg(int(feed));
}

static double distance2D(double x0, double y0, double x1, double y1)
{
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    return std::sqrt(dx * dx + dy * dy);
}

static bool isCircularPocket(const ContourFeature &feature)
{
    const QString sub = feature.subType.toLower();
    return sub.contains(QStringLiteral("circle"))
        || sub.contains(QStringLiteral("circular"));
}

static void appendRectangularRows(QVector<ScanLine> &rows,
                                  double cx,
                                  double cy,
                                  double halfSize,
                                  double step)
{
    const double yMin = cy - halfSize;
    const double yMax = cy + halfSize;
    for (double y = yMin; y <= yMax + 0.001; y += step) {
        rows.push_back({y, cx - halfSize, cx + halfSize});
    }
    if (rows.isEmpty() || rows.last().y < yMax - 0.001) {
        rows.push_back({yMax, cx - halfSize, cx + halfSize});
    }
}

static void appendCircularRows(QVector<ScanLine> &rows,
                               double cx,
                               double cy,
                               double radius,
                               double step)
{
    const double yMin = cy - radius;
    const double yMax = cy + radius;
    for (double y = yMin; y <= yMax + 0.001; y += step) {
        const double dy = y - cy;
        const double xSpan = std::sqrt(std::max(0.0, radius * radius - dy * dy));
        rows.push_back({y, cx - xSpan, cx + xSpan});
    }
    if (rows.isEmpty() || rows.last().y < yMax - 0.001) {
        rows.push_back({yMax, cx, cx});
    }
}

} // namespace

StrategyParams PocketRoughingStrategy::defaultParams() const
{
    StrategyParams p;
    p.set("safeHeight",    50.0);
    p.set("feedHeight",     3.0);
    p.set("stepDown",       2.0);
    p.set("stepover",       6.0);
    p.set("spindleSpeed", 1800.0);
    p.set("feedRate",      600.0);
    p.set("plungeRate",    200.0);
    p.set("stockToLeave",    0.3);
    p.set("helixRadius",     2.0);
    p.set("helixPitch",      0.0);
    return p;
}

ToolpathResult PocketRoughingStrategy::generate(const HoleFeature &,
                                                 const ToolEntry &,
                                                 const StrategyParams &) const
{
    return {QString(), false, QObject::tr("型腔开粗策略需要轮廓或区域特征。"), 0.0};
}

ToolpathResult PocketRoughingStrategy::generate(const ContourFeature &feature,
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

    const double cx     = feature.center.x();
    const double cy     = feature.center.y();
    const double ztop   = feature.center.z();
    const double safe   = params.get("safeHeight",    50.0);
    const double feedH  = params.get("feedHeight",     3.0);
    const double axial  = params.get("stepDown",       2.0);
    const double radial = params.get("stepover",       6.0);
    const double S      = params.get("spindleSpeed", 1800.0);
    const double F      = params.get("feedRate",      600.0);
    const double Fp     = params.get("plungeRate",    200.0);
    const double stock  = params.get("stockToLeave",    0.3);
    double helixRadius  = params.get("helixRadius",     2.0);
    double helixPitch   = params.get("helixPitch",      0.0);

    if (axial <= 0.0 || radial <= 0.0) {
        res.errorMsg = QObject::tr("每层切深和行距必须大于零。");
        return res;
    }

    const double usableSize = feature.radius - tool.diameter / 2.0 - stock;
    if (usableSize <= 0.0) {
        res.errorMsg = QObject::tr("型腔尺寸小于刀具直径和留量，无法开粗。");
        return res;
    }

    const double maxHelixRadius = std::max(0.0, usableSize - tool.diameter * 0.25);
    helixRadius = std::max(0.0, std::min(helixRadius, maxHelixRadius));
    if (helixRadius > 1.0e-6 && helixPitch <= 0.0) {
        helixPitch = std::min(axial * 0.5, tool.diameter * 0.15);
    }

    QVector<ScanLine> rows;
    const double step = std::min(radial, usableSize * 2.0);
    if (isCircularPocket(feature)) {
        appendCircularRows(rows, cx, cy, usableSize, step);
    } else {
        appendRectangularRows(rows, cx, cy, usableSize, step);
    }
    if (rows.isEmpty()) {
        res.errorMsg = QObject::tr("无法生成型腔行切路径。");
        return res;
    }

    const int zLayers = static_cast<int>(std::ceil(feature.depth / axial));

    QString gc;
    gc += QStringLiteral("T%1 M6\n").arg(tool.id);
    gc += QStringLiteral("S%1 M3\n").arg(int(S));
    gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);

    double totalLen = 0.0;
    for (int layer = 1; layer <= zLayers; ++layer) {
        const double zLayer = ztop - std::min(layer * axial, feature.depth);
        bool leftToRight = true;
        double currentX = rows.first().xMin;
        double currentY = rows.first().y;

        gc += QStringLiteral("G0 Z%1\n").arg(ztop + feedH, 0, 'f', 3);
        if (helixRadius > 1.0e-6 && helixPitch > 1.0e-6) {
            currentX = cx + helixRadius;
            currentY = cy;
            gc += QStringLiteral("G0 X%1 Y%2\n")
                      .arg(currentX, 0, 'f', 3)
                      .arg(currentY, 0, 'f', 3);
            const int helixLoops = static_cast<int>(std::ceil(((ztop + feedH) - zLayer) / helixPitch));
            for (int loop = 1; loop <= helixLoops; ++loop) {
                const double zNext = std::max(zLayer, (ztop + feedH) - loop * helixPitch);
                gc += QStringLiteral("G2 X%1 Y%2 I%3 J0.000 Z%4 F%5\n")
                          .arg(currentX, 0, 'f', 3)
                          .arg(currentY, 0, 'f', 3)
                          .arg(-helixRadius, 0, 'f', 3)
                          .arg(zNext, 0, 'f', 3)
                          .arg(int(Fp));
                totalLen += 2.0 * std::acos(-1.0) * helixRadius;
            }
            gc += cutMove(cx, cy, F);
            totalLen += helixRadius;
            currentX = cx;
            currentY = cy;
        } else {
            gc += QStringLiteral("G0 X%1 Y%2\n")
                      .arg(currentX, 0, 'f', 3)
                      .arg(currentY, 0, 'f', 3);
            gc += QStringLiteral("G1 Z%1 F%2\n").arg(zLayer, 0, 'f', 3).arg(int(Fp));
        }

        for (int i = 0; i < rows.size(); ++i) {
            const ScanLine &row = rows.at(i);
            const double startX = leftToRight ? row.xMin : row.xMax;
            const double endX   = leftToRight ? row.xMax : row.xMin;

            if (std::abs(currentX - startX) > 0.001 || std::abs(currentY - row.y) > 0.001) {
                gc += cutMove(startX, row.y, F);
                totalLen += distance2D(currentX, currentY, startX, row.y);
            }

            gc += cutMove(endX, row.y, F);
            totalLen += std::abs(endX - startX);
            currentX = endX;
            currentY = row.y;
            leftToRight = !leftToRight;
        }
    }

    gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);

    res.gcode = gc;
    res.ok = true;
    res.estimatedTimeS = (totalLen / F * 60.0) + (feature.depth / Fp * 60.0);
    return res;
}
