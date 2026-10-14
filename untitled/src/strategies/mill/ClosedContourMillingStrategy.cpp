#include "ClosedContourMillingStrategy.h"
#include "ContourSideGeometry.h"
#include "../../workspaces/program_verification/simulation/SketchClearance.h"

#include <QObject>
#include <algorithm>
#include <cmath>

namespace {

// 函数说明：dist2D负责当前模块的核心处理。
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

// 函数说明：rapidXY负责当前模块的核心处理。
static QString rapidXY(double x, double y)
{
    return QStringLiteral("G0 X%1 Y%2\n").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3);
}

// 函数说明：feedXY负责当前模块的核心处理。
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

static ToolpathResult preciseSketchContour(const ContourFeature &feature, const ToolEntry &tool,
                                          const StrategyParams &params)
{
    ToolpathResult result;
    auto fail=[&](const QString &error){result.errorMsg=error;return result;};
    for(double value:{params.get("feedRate",1000),params.get("plungeRate",200),params.get("spindleSpeed",3000)})
        if(!std::isfinite(value) || value<1 || value>std::numeric_limits<int>::max() || value!=std::floor(value))
            return fail(QObject::tr("当前草图输出要求进给、下刀速度和主轴转速为可表示的正整数。"));
    QVector<QPointF> boundary;
    for(const auto &point:feature.precisePoints) {
        if(point.z()!=feature.center.z()) return fail(QObject::tr("草图轮廓必须位于同一 XY 平面。"));
        boundary.append({point.x(),point.y()});
    }
    const int orientation=SketchClearance::convexOrientation(boundary);
    if(!orientation || params.get("compensation",1)!=0 || params.get("camOffsetSide",1)!=-orientation)
        return fail(QObject::tr("草图加工仅支持凸闭合轮廓的外侧 G40 CAM 偏置。"));
    // Additional retained stock absorbs three-decimal XY output rounding.
    const double guard=0.002;
    const double distance=tool.diameter/2+params.get("stockToLeave",0)+guard;
    QVector<QPointF> points;
    const auto rounded=[](double value){return QString::number(value,'f',3).toDouble();};
    for(int i=0;i<boundary.size();++i) {
        const auto &previous=boundary[(i+boundary.size()-1)%boundary.size()];
        const auto &current=boundary[i], &next=boundary[(i+1)%boundary.size()];
        const double ax=current.x()-previous.x(), ay=current.y()-previous.y();
        const double bx=next.x()-current.x(), by=next.y()-current.y();
        const double al=std::hypot(ax,ay), bl=std::hypot(bx,by);
        if(!std::isfinite(al) || !std::isfinite(bl) || al<=0 || bl<=0)
            return fail(QObject::tr("草图边长无效。"));
        const double n1x=-ay/al, n1y=ax/al, n2x=-by/bl, n2y=bx/bl;
        const double divisor=1+n1x*n2x+n1y*n2y;
        if(!std::isfinite(divisor) || divisor<=0)
            return fail(QObject::tr("草图偏置角度无法解析。"));
        points.append({rounded(current.x()-orientation*distance*(n1x+n2x)/divisor),
                       rounded(current.y()-orientation*distance*(n1y+n2y)/divisor)});
    }
    if(SketchClearance::convexOrientation(points)!=orientation)
        return fail(QObject::tr("输出精度下偏置轮廓退化。"));
    const auto &first=points.first(), &second=points[1];
    const double dx=second.x()-first.x(), dy=second.y()-first.y(), length=std::hypot(dx,dy);
    const double lead=std::max(params.get("leadLength",5),tool.diameter);
    if(!std::isfinite(length) || length<=0) return fail(QObject::tr("引入方向无法解析。"));
    const QPointF entry(rounded(first.x()-dx/length*lead),rounded(first.y()-dy/length*lead));
    const auto required=SketchClearance::add(SketchClearance::Bound::exact(tool.diameter/2),
                                            SketchClearance::Bound::exact(params.get("stockToLeave",0)));
    if(!required.valid()) return fail(QObject::tr("刀具间隙无法解析。"));
    for(int i=0;i<points.size();++i)
        if(SketchClearance::segmentClearsPolygon(points[i],points[(i+1)%points.size()],boundary,required.hi)
           !=SketchClearance::Verdict::Clear)
            return fail(QObject::tr("输出轮廓未能证明保留刀具半径与余量。"));
    if(SketchClearance::segmentClearsPolygon(entry,first,boundary,required.hi)!=SketchClearance::Verdict::Clear)
        return fail(QObject::tr("引入线未能证明保留轮廓间隙，请调整起点或引入长度。"));
    const double layers=std::ceil(feature.depth/params.get("stepDown",1));
    if(!std::isfinite(layers) || layers<1 || layers>100000)
        return fail(QObject::tr("分层数量超出当前草图生成范围。"));
    const double safe=params.get("safeHeight",50), feedHeight=params.get("feedHeight",3);
    const double feed=params.get("feedRate",1000), plunge=params.get("plungeRate",200);
    QString code=QStringLiteral("(Additional outward stock for output precision: 0.002 mm)\n"
                                "(Layer Z rounded upward; residual depth below 0.0011 mm)\n"
                                "T%1 M6\nS%2 M3\nG0 Z%3\n")
        .arg(tool.id).arg(int(params.get("spindleSpeed",3000))).arg(safe,0,'f',3);
    double previousZ=feature.center.z();
    for(int layer=1;layer<=int(layers);++layer) {
        const double target=feature.center.z()-std::min(layer*params.get("stepDown",1),feature.depth);
        const double z=std::ceil(std::nextafter(target*1000,std::numeric_limits<double>::infinity()))/1000;
        if(!std::isfinite(z) || z>=previousZ || z<=target || z-target>.0011)
            return fail(QObject::tr("三位输出精度无法形成有效且不超深的分层，请调整切深。"));
        previousZ=z;
        code+=rapidXY(entry.x(),entry.y());
        code+=QStringLiteral("G0 Z%1\nG1 Z%2 F%3\nG40\n")
            .arg(feature.center.z()+feedHeight,0,'f',3).arg(z,0,'f',3).arg(int(plunge));
        for(const auto &point:points) code+=feedXY(point.x(),point.y(),feed);
        code+=feedXY(first.x(),first.y(),feed);
        code+=feedXY(entry.x(),entry.y(),feed);
        code+=QStringLiteral("G0 Z%1\n").arg(safe,0,'f',3);
    }
    result.ok=true;
    result.gcode=code;
    return result;
}

} // namespace

// 函数说明：defaultParams负责当前模块的核心处理。
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

// 函数说明：generate负责当前模块的核心处理。
ToolpathResult ClosedContourMillingStrategy::generate(const HoleFeature &,
                                                       const ToolEntry &,
                                                       const StrategyParams &) const
{
    return {QString(), false, QObject::tr("封闭轮廓铣边策略需要轮廓特征。"), 0.0};
}

// 函数说明：generate负责当前模块的核心处理。
ToolpathResult ClosedContourMillingStrategy::generate(const ContourFeature &feature,
                                                       const ToolEntry      &tool,
                                                       const StrategyParams &params) const
{
    ToolpathResult res;

    if (!feature.islands.isEmpty()) {
        res.errorMsg = QObject::tr("当前轮廓铣削尚未验证岛屿避让，请选择无岛区域。");
        return res;
    }

    if (!std::isfinite(tool.diameter) || tool.diameter <= 0.0) {
        res.errorMsg = QObject::tr("刀具直径无效。");
        return res;
    }
    if (!std::isfinite(feature.depth) || feature.depth <= 0.0) {
        res.errorMsg = QObject::tr("加工深度必须大于零。");
        return res;
    }

    const double axial = params.get("stepDown", 1.0);
    if (!std::isfinite(axial) || axial <= 0.0) {
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

    if (!std::isfinite(ztop) || !std::isfinite(safe) || !std::isfinite(feedH) ||
        !std::isfinite(S) || !std::isfinite(F) || !std::isfinite(Fp) ||
        !std::isfinite(lead) || (comp != -1 && comp != 0 && comp != 1)) {
        res.errorMsg = QObject::tr("加工高度、速度、进刀长度或刀补模式无效。");
        return res;
    }

    if (!std::isfinite(stock) || stock < 0 || (comp != 0 && stock != 0)) {
        res.errorMsg = QObject::tr("正余量请使用CAM几何偏置；机床刀补当前不自动加入余量。");
        return res;
    }

    if (F <= 0.0 || Fp <= 0.0) {
        res.errorMsg = QObject::tr("进给速度和下刀速度必须大于零。");
        return res;
    }

    if (!feature.precisePoints.isEmpty()) return preciseSketchContour(feature,tool,params);

    // Build contour point list: use feature.points if provided, otherwise build
    // a rectangle from center + radius (radius is interpreted as half-width/half-height).
    QVector<QVector3D> pts;
    if (feature.points.size() >= 3) {
        pts = feature.points;
    } else {
        const double halfW = feature.radius;
        const double halfH = feature.radius;
        if (!std::isfinite(halfW) || halfW <= 0.0) {
            res.errorMsg = QObject::tr("轮廓尺寸无效。");
            return res;
        }
        pts = rectanglePoints(feature.center.toVector3D(), halfW, halfH);
    }

    if (pts.size() < 3) {
        res.errorMsg = QObject::tr("封闭轮廓至少需要三个点。");
        return res;
    }

    // comp == 0: CAM-side offset, output G40. Otherwise G41/G42 machine compensation.
    const bool camOffset = std::abs(comp) < 1e-9;
    const QVector<QVector3D> boundary = pts;
    const double offsetSide = params.get("camOffsetSide", 1);
    const double clearance = tool.diameter / 2.0 + stock;
    bool inside = false;
    auto outputPoint = [](QVector3D point) {
        point.setX(QString::number(point.x(),'f',3).toFloat());
        point.setY(QString::number(point.y(),'f',3).toFloat());
        return point;
    };
    if (camOffset) {
        const int orientation = convexContourOrientation(boundary);
        if (!orientation || (offsetSide != -1 && offsetSide != 1) ||
            !std::isfinite(stock) || stock < 0 || !std::isfinite(lead) ||
            std::abs(double(boundary.first().z())-ztop)>1e-5) {
            res.errorMsg = QObject::tr("CAM侧偏置需要简单凸XY闭合轮廓、有效侧别及非负余量；凹域尚未支持。");
            return res;
        }
        inside = offsetSide == orientation;
        pts = offsetPolygon2D(boundary, clearance * offsetSide);
        if (!convexContourOrientation(pts)) {
            res.errorMsg = QObject::tr("CAM偏置产生无效坐标或退化边界。");
            return res;
        }
        for (auto &point : pts) point = outputPoint(point);
        bool valid = convexContourOrientation(pts) == orientation;
        if (inside) {
            for (const auto &point : pts) valid = valid && contourContainsWithClearance(boundary,point,clearance,orientation);
        } else {
            for (const auto &point : boundary) valid = valid && contourContainsWithClearance(pts,point,clearance,orientation);
        }
        if (!valid) {
            res.errorMsg = QObject::tr("刀具半径与余量导致偏置坍塌或输出精度下越界，请调整刀具、余量或轮廓。");
            return res;
        }
    }

    const int zLayers = static_cast<int>(std::ceil(feature.depth / axial));

    // Lead-in point: approach from outside along the direction first→second edge.
    const QVector3D first  = pts.first();
    const QVector3D second = pts.at(1);
    const QVector3D inDir  = dir2D(first, second, QVector3D(1, 0, 0));
    QVector3D leadPoint = first - inDir * float(lead);
    if (camOffset && inside)
        leadPoint = first + QVector3D(-inDir.y(), inDir.x(), 0) * float(offsetSide * lead);
    if (!std::isfinite(leadPoint.x()) || !std::isfinite(leadPoint.y())) {
        res.errorMsg = QObject::tr("进退刀坐标无效。");
        return res;
    }
    leadPoint = outputPoint(leadPoint);
    const double leadX = leadPoint.x();
    const double leadY = leadPoint.y();
    if (camOffset && ((inside && !contourContainsWithClearance(boundary,leadPoint,clearance)) ||
        !contourSegmentClearsBoundary(leadPoint,first,boundary,clearance))) {
        res.errorMsg = QObject::tr("指定CAM进退刀无法在当前加工侧保留刀具半径与余量，请调整进刀长度、起点或刀具。");
        return res;
    }

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
            gc += feedXY(first.x(), first.y(), F);
        } else {
            // Machine-side compensation: G41 (left) or G42 (right).
            const QString compCode = comp > 0.0 ? QStringLiteral("G41") : QStringLiteral("G42");
            gc += QStringLiteral("G1 %1 D%2 X%3 Y%4 F%5\n")
                      .arg(compCode)
                      .arg(tool.id)
                      .arg(first.x(), 0, 'f', 3)
                      .arg(first.y(), 0, 'f', 3)
                      .arg(int(F));
        }
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
        if (camOffset) {
            gc += feedXY(leadX, leadY, F);
            gc += QStringLiteral("G40\n");
        } else {
            gc += QStringLiteral("G1 G40 X%1 Y%2 F%3\n")
                      .arg(leadX, 0, 'f', 3)
                      .arg(leadY, 0, 'f', 3)
                      .arg(int(F));
        }
        totalLen += lead;

        gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
    }

    res.gcode = gc;
    res.ok    = true;
    res.estimatedTimeS = (totalLen / F * 60.0) + (feature.depth / Fp * 60.0);
    return res;
}
