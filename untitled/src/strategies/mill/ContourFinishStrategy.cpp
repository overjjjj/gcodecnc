#include "ContourFinishStrategy.h"

#include <QObject>
#include <algorithm>
#include <cmath>

namespace {

static double distance2D(double x0, double y0, double x1, double y1)
{
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    return std::sqrt(dx * dx + dy * dy);
}

static QVector3D normalized2D(const QVector3D &v, const QVector3D &fallback)
{
    const double len = std::sqrt(double(v.x()) * v.x() + double(v.y()) * v.y());
    if (len <= 1e-6) {
        return fallback;
    }
    return QVector3D(float(v.x() / len), float(v.y() / len), 0.0f);
}

static QString rapidXY(double x, double y)
{
    return QStringLiteral("G0 X%1 Y%2\n").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3);
}

static QString feedXY(double x, double y, double feed)
{
    return QStringLiteral("G1 X%1 Y%2 F%3\n")
        .arg(x, 0, 'f', 3)
        .arg(y, 0, 'f', 3)
        .arg(int(feed));
}

} // namespace

StrategyParams ContourFinishStrategy::defaultParams() const
{
    StrategyParams p;
    p.set("safeHeight",    50.0);
    p.set("feedHeight",     3.0);
    p.set("stepDown",       1.0);
    p.set("spindleSpeed", 3000.0);
    p.set("feedRate",     1000.0);
    p.set("plungeRate",    200.0);
    p.set("stockToLeave",    0.0);
    p.set("compensation",    1.0);
    p.set("leadLength",      5.0);
    return p;
}

ToolpathResult ContourFinishStrategy::generate(const HoleFeature &,
                                                const ToolEntry &,
                                                const StrategyParams &) const
{
    return {QString(), false, QObject::tr("轮廓精加工策略需要轮廓特征。"), 0.0};
}

ToolpathResult ContourFinishStrategy::generate(const ContourFeature &feature,
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

    const double axial = params.get("stepDown", 1.0);
    if (axial <= 0.0) {
        res.errorMsg = QObject::tr("每层切深必须大于零。");
        return res;
    }

    const double cx    = feature.center.x();
    const double cy    = feature.center.y();
    const double ztop  = feature.center.z();
    const double safe  = params.get("safeHeight",    50.0);
    const double feedH = params.get("feedHeight",     3.0);
    const double S     = params.get("spindleSpeed", 3000.0);
    const double F     = params.get("feedRate",     1000.0);
    const double Fp    = params.get("plungeRate",    200.0);
    const double stock = params.get("stockToLeave",    0.0);
    const double comp  = params.get("compensation",    1.0);
    const double lead  = std::max(params.get("leadLength", 5.0), tool.diameter);

    if (F <= 0.0 || Fp <= 0.0) {
        res.errorMsg = QObject::tr("进给速度和下刀速度必须大于零。");
        return res;
    }

    const bool isCircle = (feature.subType == QStringLiteral("circle") || feature.radius > 0.0);
    const double r = feature.radius - stock;
    if (isCircle && r <= 0.0) {
        res.errorMsg = QObject::tr("精加工余量大于圆半径。");
        return res;
    }
    if (!isCircle && feature.points.size() < 2) {
        res.errorMsg = QObject::tr("点列轮廓至少需要两个点。");
        return res;
    }

    const QString compCode = comp >= 0.0 ? QStringLiteral("G41") : QStringLiteral("G42");
    const int zLayers = static_cast<int>(std::ceil(feature.depth / axial));

    QString gc;
    gc += QStringLiteral("T%1 M6\n").arg(tool.id);
    gc += QStringLiteral("S%1 M3\n").arg(int(S));
    gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);

    double totalLen = 0.0;
    for (int layer = 1; layer <= zLayers; ++layer) {
        const double zLayer = ztop - std::min(layer * axial, feature.depth);

        if (isCircle) {
            const double entryX = cx + r;
            const double entryY = cy;
            const double leadX = entryX + lead;
            const double leadY = entryY;

            gc += rapidXY(leadX, leadY);
            gc += QStringLiteral("G0 Z%1\n").arg(ztop + feedH, 0, 'f', 3);
            gc += QStringLiteral("G1 Z%1 F%2\n").arg(zLayer, 0, 'f', 3).arg(int(Fp));
            gc += QStringLiteral("G1 %1 D%2 X%3 Y%4 F%5\n")
                      .arg(compCode)
                      .arg(tool.id)
                      .arg(entryX, 0, 'f', 3)
                      .arg(entryY, 0, 'f', 3)
                      .arg(int(F));
            gc += QStringLiteral("G2 X%1 Y%2 I%3 J0.000 F%4\n")
                      .arg(entryX, 0, 'f', 3)
                      .arg(entryY, 0, 'f', 3)
                      .arg(cx - entryX, 0, 'f', 3)
                      .arg(int(F));
            gc += QStringLiteral("G1 G40 X%1 Y%2 F%3\n")
                      .arg(leadX, 0, 'f', 3)
                      .arg(leadY, 0, 'f', 3)
                      .arg(int(F));
            totalLen += 2.0 * std::acos(-1.0) * r + lead * 2.0;
        } else {
            const QVector3D first = feature.points.first();
            const QVector3D second = feature.points.at(1);
            const QVector3D dir = normalized2D(second - first, QVector3D(1, 0, 0));
            const double leadX = first.x() - dir.x() * lead;
            const double leadY = first.y() - dir.y() * lead;

            gc += rapidXY(leadX, leadY);
            gc += QStringLiteral("G0 Z%1\n").arg(ztop + feedH, 0, 'f', 3);
            gc += QStringLiteral("G1 Z%1 F%2\n").arg(zLayer, 0, 'f', 3).arg(int(Fp));
            gc += QStringLiteral("G1 %1 D%2 X%3 Y%4 F%5\n")
                      .arg(compCode)
                      .arg(tool.id)
                      .arg(first.x(), 0, 'f', 3)
                      .arg(first.y(), 0, 'f', 3)
                      .arg(int(F));
            totalLen += lead;

            QVector3D prev = first;
            for (int i = 1; i < feature.points.size(); ++i) {
                const QVector3D pt = feature.points.at(i);
                gc += feedXY(pt.x(), pt.y(), F);
                totalLen += distance2D(prev.x(), prev.y(), pt.x(), pt.y());
                prev = pt;
            }
            if (feature.subType == QStringLiteral("closed_contour")) {
                gc += feedXY(first.x(), first.y(), F);
                totalLen += distance2D(prev.x(), prev.y(), first.x(), first.y());
            }
            gc += QStringLiteral("G1 G40 X%1 Y%2 F%3\n")
                      .arg(leadX, 0, 'f', 3)
                      .arg(leadY, 0, 'f', 3)
                      .arg(int(F));
            totalLen += lead;
        }

        gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
    }

    res.gcode = gc;
    res.ok = true;
    res.estimatedTimeS = (totalLen / F * 60.0) + (feature.depth / Fp * 60.0);
    return res;
}
