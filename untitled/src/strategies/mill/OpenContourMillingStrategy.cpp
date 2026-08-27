#include "OpenContourMillingStrategy.h"
#include "ContourMillingContract.h"

#include <QObject>
#include <algorithm>
#include <cmath>

namespace {

static double dist2D(double x0, double y0, double x1, double y1)
{
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    return std::sqrt(dx * dx + dy * dy);
}

static QVector3D dir2D(const QVector3D &a, const QVector3D &b, const QVector3D &fallback)
{
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len <= 1e-9)
        return fallback;
    return QVector3D(float(dx / len), float(dy / len), 0.0f);
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

// Offset an open polyline by `offset` along the left-side normals (CAM-side compensation).
static QVector<QVector3D> offsetPolyline2D(const QVector<QVector3D> &pts, double offset)
{
    const int n = pts.size();
    QVector<QVector3D> result(n);
    for (int i = 0; i < n; ++i) {
        double nx, ny;
        if (i == 0) {
            // Start: use only the first edge normal
            double bx = pts[1].x() - pts[0].x(), by = pts[1].y() - pts[0].y();
            double lb = std::sqrt(bx * bx + by * by);
            if (lb > 1e-12) { bx /= lb; by /= lb; }
            nx = -by; ny = bx;
        } else if (i == n - 1) {
            // End: use only the last edge normal
            double ax = pts[n-1].x() - pts[n-2].x(), ay = pts[n-1].y() - pts[n-2].y();
            double la = std::sqrt(ax * ax + ay * ay);
            if (la > 1e-12) { ax /= la; ay /= la; }
            nx = -ay; ny = ax;
        } else {
            // Interior: average normals of adjacent edges
            double ax = pts[i].x() - pts[i-1].x(), ay = pts[i].y() - pts[i-1].y();
            double la = std::sqrt(ax * ax + ay * ay);
            if (la > 1e-12) { ax /= la; ay /= la; }
            const double n1x = -ay, n1y = ax;

            double bx = pts[i+1].x() - pts[i].x(), by = pts[i+1].y() - pts[i].y();
            double lb = std::sqrt(bx * bx + by * by);
            if (lb > 1e-12) { bx /= lb; by /= lb; }
            const double n2x = -by, n2y = bx;

            nx = n1x + n2x; ny = n1y + n2y;
            const double nl = std::sqrt(nx * nx + ny * ny);
            if (nl > 1e-12) { nx /= nl; ny /= nl; }

            // Scale by 1/cos(half-angle) to keep offset distance consistent at corners
            const double cosHalf = n1x * nx + n1y * ny;
            if (cosHalf > 1e-6) { const double s = 1.0 / cosHalf; nx *= s; ny *= s; }
        }
        result[i] = QVector3D(float(pts[i].x() + nx * offset),
                              float(pts[i].y() + ny * offset),
                              pts[i].z());
    }
    return result;
}

} // namespace

StrategyParams OpenContourMillingStrategy::defaultParams() const
{
    return contourMillingDefaultParams(false);
}

ProcessParameterSchema OpenContourMillingStrategy::parameterSchema() const
{
    return contourMillingParameterSchema(false);
}

ToolpathResult OpenContourMillingStrategy::generate(const HoleFeature &,
                                                     const ToolEntry &,
                                                     const StrategyParams &) const
{
    return {QString(), false, QObject::tr("开放轮廓铣边策略需要轮廓特征。"), 0.0};
}

ToolpathResult OpenContourMillingStrategy::generate(const ContourFeature &feature,
                                                     const ToolEntry      &tool,
                                                     const StrategyParams &params) const
{
    ToolpathResult res;

    const QString contractError =
        validateContourMillingContract(feature, tool, params, false);
    if (!contractError.isEmpty()) {
        res.errorMsg = contractError;
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
    if (feature.points.size() < 2) {
        res.errorMsg = QObject::tr("开放轮廓至少需要两个点。");
        return res;
    }

    const double axial = params.get("stepDown", 1.0);
    if (axial <= 0.0) {
        res.errorMsg = QObject::tr("每层切深必须大于零。");
        return res;
    }

    const double ztop  = feature.center.z();
    const double safe  = params.get("safeHeight",    50.0);
    const double feedH = params.get("feedHeight",     3.0);
    const double S     = params.get("spindleSpeed", 3000.0);
    const double F     = params.get("feedRate",     1000.0);
    const double Fp    = params.get("plungeRate",    200.0);
    const double comp  = params.get("compensation",   1.0);
    const double lead  = std::max(params.get("leadLength", 5.0), tool.diameter);
    const double effectiveDepth = contourMillingEffectiveDepth(feature, params);

    if (F <= 0.0 || Fp <= 0.0) {
        res.errorMsg = QObject::tr("进给速度和下刀速度必须大于零。");
        return res;
    }

    // comp == 0: CAM-side offset, output G40. Otherwise G41/G42 machine compensation.
    const bool camOffset = std::abs(comp) < 1e-9;
    const double stock = params.get("stockToLeave", 0.0);

    QVector<QVector3D> pts = feature.points;
    if (camOffset) {
        pts = offsetPolyline2D(pts, tool.diameter / 2.0 + stock);
    }

    const int zLayers = static_cast<int>(std::ceil(effectiveDepth / axial));

    // Lead-in: approach along the direction from second→first point (extended behind start).
    const QVector3D first  = pts.first();
    const QVector3D second = pts.at(1);
    const QVector3D inDir  = dir2D(first, second, QVector3D(1, 0, 0));
    const double leadInX = first.x() - inDir.x() * lead;
    const double leadInY = first.y() - inDir.y() * lead;

    // Lead-out: continue along the direction from second-to-last→last point.
    const QVector3D last     = pts.last();
    const QVector3D prevLast = pts.at(pts.size() - 2);
    const QVector3D outDir   = dir2D(prevLast, last, QVector3D(1, 0, 0));
    const double leadOutX = last.x() + outDir.x() * lead;
    const double leadOutY = last.y() + outDir.y() * lead;

    QString gc;
    gc += QStringLiteral("T%1 M6\n").arg(tool.id);
    gc += QStringLiteral("S%1 M3\n").arg(int(S));
    gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);

    double totalLen = 0.0;

    for (int layer = 1; layer <= zLayers; ++layer) {
        const double zLayer = ztop - std::min(layer * axial, effectiveDepth);

        gc += rapidXY(leadInX, leadInY);
        gc += QStringLiteral("G0 Z%1\n").arg(ztop + feedH, 0, 'f', 3);
        gc += QStringLiteral("G1 Z%1 F%2\n").arg(zLayer, 0, 'f', 3).arg(int(Fp));

        if (camOffset) {
            gc += QStringLiteral("G40\n");
            gc += feedXY(first.x(), first.y(), F);
        } else {
            const QString compCode = comp > 0.0 ? QStringLiteral("G41") : QStringLiteral("G42");
            gc += QStringLiteral("G1 %1 D%2 X%3 Y%4 F%5\n")
                      .arg(compCode)
                      .arg(tool.id)
                      .arg(first.x(), 0, 'f', 3)
                      .arg(first.y(), 0, 'f', 3)
                      .arg(int(F));
        }
        totalLen += lead;

        QVector3D prev = first;
        for (int i = 1; i < pts.size(); ++i) {
            const QVector3D pt = pts.at(i);
            gc += feedXY(pt.x(), pt.y(), F);
            totalLen += dist2D(prev.x(), prev.y(), pt.x(), pt.y());
            prev = pt;
        }

        // Lead-out along end tangent direction, then cancel compensation.
        if (camOffset) {
            gc += feedXY(leadOutX, leadOutY, F);
            gc += QStringLiteral("G40\n");
        } else {
            gc += QStringLiteral("G1 G40 X%1 Y%2 F%3\n")
                      .arg(leadOutX, 0, 'f', 3)
                      .arg(leadOutY, 0, 'f', 3)
                      .arg(int(F));
        }
        totalLen += lead;

        gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
    }

    res.gcode = gc;
    res.ok    = true;
    res.estimatedTimeS = (totalLen / F * 60.0) + (effectiveDepth / Fp * 60.0);
    return res;
}
