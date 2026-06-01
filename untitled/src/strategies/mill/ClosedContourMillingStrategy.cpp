#include "ClosedContourMillingStrategy.h"

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

// Returns a unit 2D direction vector from a to b; uses fallback if a==b.
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

// Build a rectangle (4 vertices, CCW) from center + halfW + halfH.
static QVector<QVector3D> rectanglePoints(const QVector3D &center, double halfW, double halfH)
{
    return {
        center + QVector3D(float(-halfW), float(-halfH), 0),
        center + QVector3D(float( halfW), float(-halfH), 0),
        center + QVector3D(float( halfW), float( halfH), 0),
        center + QVector3D(float(-halfW), float( halfH), 0),
    };
}

// Offset a closed polygon by `offset` along inward vertex normals (positive = shrink for CCW).
// Uses averaged perpendicular normals at each vertex.
static QVector<QVector3D> offsetPolygon2D(const QVector<QVector3D> &pts, double offset)
{
    const int n = pts.size();
    QVector<QVector3D> result(n);
    for (int i = 0; i < n; ++i) {
        const QVector3D &prev = pts[(i + n - 1) % n];
        const QVector3D &cur  = pts[i];
        const QVector3D &next = pts[(i + 1) % n];

        // Inward normal of edge prev→cur: rotate direction 90° CCW
        double ax = cur.x() - prev.x(), ay = cur.y() - prev.y();
        double la = std::sqrt(ax * ax + ay * ay);
        if (la > 1e-12) { ax /= la; ay /= la; }
        const double n1x = -ay, n1y = ax;

        // Inward normal of edge cur→next
        double bx = next.x() - cur.x(), by = next.y() - cur.y();
        double lb = std::sqrt(bx * bx + by * by);
        if (lb > 1e-12) { bx /= lb; by /= lb; }
        const double n2x = -by, n2y = bx;

        // Average normal
        double nx = n1x + n2x, ny = n1y + n2y;
        const double nl = std::sqrt(nx * nx + ny * ny);
        if (nl > 1e-12) { nx /= nl; ny /= nl; }

        // Scale by 1/cos(half-angle) to keep offset distance consistent at corners
        const double cosHalf = n1x * nx + n1y * ny;
        const double scale = (cosHalf > 1e-6) ? 1.0 / cosHalf : 1.0;

        result[i] = QVector3D(float(cur.x() + nx * offset * scale),
                              float(cur.y() + ny * offset * scale),
                              cur.z());
    }
    return result;
}

} // namespace

StrategyParams ClosedContourMillingStrategy::defaultParams() const
{
    StrategyParams p;
    p.set("safeHeight",    50.0);
    p.set("feedHeight",     3.0);
    p.set("stepDown",       1.0);
    p.set("spindleSpeed", 3000.0);
    p.set("feedRate",     1000.0);
    p.set("plungeRate",    200.0);
    p.set("stockToLeave",   0.0);
    p.set("compensation",   1.0);  // 1=G41 left, -1=G42 right
    p.set("leadLength",     5.0);
    return p;
}

ToolpathResult ClosedContourMillingStrategy::generate(const HoleFeature &,
                                                       const ToolEntry &,
                                                       const StrategyParams &) const
{
    return {QString(), false, QObject::tr("封闭轮廓铣边策略需要轮廓特征。"), 0.0};
}

ToolpathResult ClosedContourMillingStrategy::generate(const ContourFeature &feature,
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

    const double ztop  = feature.center.z();
    const double safe  = params.get("safeHeight",    50.0);
    const double feedH = params.get("feedHeight",     3.0);
    const double S     = params.get("spindleSpeed", 3000.0);
    const double F     = params.get("feedRate",     1000.0);
    const double Fp    = params.get("plungeRate",    200.0);
    const double stock = params.get("stockToLeave",   0.0);
    const double comp  = params.get("compensation",   1.0);
    const double lead  = std::max(params.get("leadLength", 5.0), tool.diameter);

    if (F <= 0.0 || Fp <= 0.0) {
        res.errorMsg = QObject::tr("进给速度和下刀速度必须大于零。");
        return res;
    }

    // Build contour point list: use feature.points if provided, otherwise build
    // a rectangle from center + radius (radius is interpreted as half-width/half-height).
    QVector<QVector3D> pts;
    if (feature.points.size() >= 3) {
        pts = feature.points;
    } else {
        const double halfW = feature.radius - stock;
        const double halfH = feature.radius - stock;
        if (halfW <= 0.0) {
            res.errorMsg = QObject::tr("余量大于轮廓尺寸。");
            return res;
        }
        pts = rectanglePoints(feature.center, halfW, halfH);
    }

    if (pts.size() < 3) {
        res.errorMsg = QObject::tr("封闭轮廓至少需要三个点。");
        return res;
    }

    // comp == 0: CAM-side offset, output G40. Otherwise G41/G42 machine compensation.
    const bool camOffset = std::abs(comp) < 1e-9;
    if (camOffset) {
        pts = offsetPolygon2D(pts, tool.diameter / 2.0 + stock);
    }

    const int zLayers = static_cast<int>(std::ceil(feature.depth / axial));

    // Lead-in point: approach from outside along the direction first→second edge.
    const QVector3D first  = pts.first();
    const QVector3D second = pts.at(1);
    const QVector3D inDir  = dir2D(first, second, QVector3D(1, 0, 0));
    const double leadX = first.x() - inDir.x() * lead;
    const double leadY = first.y() - inDir.y() * lead;

    QString gc;
    gc += QStringLiteral("T%1 M6\n").arg(tool.id);
    gc += QStringLiteral("S%1 M3\n").arg(int(S));
    gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);

    double totalLen = 0.0;

    for (int layer = 1; layer <= zLayers; ++layer) {
        const double zLayer = ztop - std::min(layer * axial, feature.depth);

        // Rapid to lead-in XY at safe height, then drop to feed height, plunge to layer Z.
        gc += rapidXY(leadX, leadY);
        gc += QStringLiteral("G0 Z%1\n").arg(ztop + feedH, 0, 'f', 3);
        gc += QStringLiteral("G1 Z%1 F%2\n").arg(zLayer, 0, 'f', 3).arg(int(Fp));

        if (camOffset) {
            // CAM-side offset: path already shifted, no machine compensation.
            gc += QStringLiteral("G40\n");
        } else {
            // Machine-side compensation: G41 (left) or G42 (right).
            const QString compCode = comp > 0.0 ? QStringLiteral("G41") : QStringLiteral("G42");
            gc += QStringLiteral("%1 D%2\n").arg(compCode).arg(tool.id);
        }
        gc += feedXY(first.x(), first.y(), F);
        totalLen += lead;

        // Traverse polygon vertices.
        QVector3D prev = first;
        for (int i = 1; i < pts.size(); ++i) {
            const QVector3D pt = pts.at(i);
            gc += feedXY(pt.x(), pt.y(), F);
            totalLen += dist2D(prev.x(), prev.y(), pt.x(), pt.y());
            prev = pt;
        }

        // Close the polygon back to the first point.
        gc += feedXY(first.x(), first.y(), F);
        totalLen += dist2D(prev.x(), prev.y(), first.x(), first.y());

        // Lead-out along same approach direction, then cancel compensation.
        gc += feedXY(leadX, leadY, F);
        totalLen += lead;

        gc += QStringLiteral("G40\n");
        gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
    }

    res.gcode = gc;
    res.ok    = true;
    res.estimatedTimeS = (totalLen / F * 60.0) + (feature.depth / Fp * 60.0);
    return res;
}
