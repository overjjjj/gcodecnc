#include "PocketFinishStrategy.h"
#include "PocketAllowances.h"

#include <QObject>
#include <QPointF>
#include <cmath>

ToolpathResult PocketFinishStrategy::generateRegionPass(const ContourFeature &feature, const ToolEntry &tool,
                                 const StrategyParams &params, bool includeToolCall)
{
    auto reject=[](const char *reason){ToolpathResult r;r.errorMsg=QString::fromLatin1(reason);return r;};
    if(!feature.planarBoundaryVerified || feature.points.size()<3 || !feature.islands.isEmpty())
        return reject("POCKET_WALL_VERIFIED_SINGLE_REGION_REQUIRED");
    if(params.get("entryMode",-1)!=0)return reject("POCKET_FINISH_CONFIRMED_VERTICAL_ENTRY_REQUIRED");
    const PocketAllowances stock{params.get("sideStockToLeave",params.get("stockToLeave",0)),
                                 params.get("floorStockToLeave",params.get("stockToLeave",0))};
    const double step=params.get("stepDown",1),feed=params.get("feedRate",800),plunge=params.get("plungeRate",200);
    const double safe=params.get("safeHeight",50),top=feature.center.z(),feedZ=top+params.get("feedHeight",3);
    if(!stock.valid(feature.depth))return reject("POCKET_FINISH_ALLOWANCE_INVALID");
    for(double value:{tool.diameter,step,feed,plunge})if(!std::isfinite(value) || value<=0)
        return reject("POCKET_FINISH_POSITIVE_TOOL_AND_CUTTING_PARAMETERS_REQUIRED");
    if(!std::isfinite(safe) || !std::isfinite(feedZ) || !std::isfinite(top) || feedZ<=top || safe<feedZ)
        return reject("POCKET_FINISH_HEIGHT_INVALID");
    QVector<QPointF> points;
    for(const auto &p:feature.points) {
        if(!std::isfinite(p.x()) || !std::isfinite(p.y()) || !std::isfinite(p.z()) || p.z()!=top)
            return reject("POCKET_WALL_HORIZONTAL_REGION_REQUIRED");
        points.append(QPointF(p.x(),p.y()));
    }
    auto cross=[](QPointF a,QPointF b){return a.x()*b.y()-a.y()*b.x();};
    auto dot=[](QPointF a,QPointF b){return a.x()*b.x()+a.y()*b.y();};
    double area=0;for(int i=0;i<points.size();++i)area+=cross(points[i],points[(i+1)%points.size()]);
    if(!std::isfinite(area) || area==0)return reject("POCKET_WALL_CONVEX_REGION_REQUIRED");
    const double direction=area>0?1:-1;
    QVector<QPointF> normals;QVector<double> offsets;
    // Convex half-planes also prove the interior lead from the path centroid.
    // The 0.001 guard contains three-decimal XY rounding; final NC is checked again.
    const double clearance=tool.diameter*.5+stock.side+.001;
    for(int i=0;i<points.size();++i) {
        const auto edge=points[(i+1)%points.size()]-points[i];
        const double length=std::hypot(edge.x(),edge.y());
        if(!std::isfinite(length) || length<=0)return reject("POCKET_WALL_INVALID_EDGE");
        for(const auto &p:points)if(direction*cross(edge,p-points[i])<0)
            return reject("POCKET_WALL_CONVEX_REGION_REQUIRED");
        const QPointF normal=QPointF(-edge.y(),edge.x())*(direction/length);
        normals.append(normal);offsets.append(dot(normal,points[i])+clearance);
    }
    // Clip by every inset half-plane. Small rounded edges may become redundant
    // as the inset grows; intersecting adjacent lines would wrongly reject them.
    QVector<QPointF> path=points;
    for(int i=0;i<normals.size();++i) {
        QVector<QPointF> clipped;
        if(path.isEmpty())return reject("POCKET_WALL_TOOL_DOES_NOT_FIT");
        auto a=path.last();double da=dot(normals[i],a)-offsets[i];
        for(const auto &b:path) {
            const double db=dot(normals[i],b)-offsets[i];
            if((da>=0)!=(db>=0))clipped.append(a+(b-a)*(da/(da-db)));
            if(db>=0)clipped.append(b);
            a=b;da=db;
        }
        path=clipped;
    }
    if(path.size()<3)return reject("POCKET_WALL_TOOL_DOES_NOT_FIT");
    QPointF center;double pathArea=0;
    for(int i=0;i<path.size();++i) {
        const auto p=path[i];
        if(!std::isfinite(p.x()) || !std::isfinite(p.y()))return reject("POCKET_WALL_INVALID_OFFSET");
        for(int j=0;j<normals.size();++j)if(dot(normals[j],p)<offsets[j]-1e-9)
            return reject("POCKET_WALL_TOOL_DOES_NOT_FIT");
        center+=p;pathArea+=cross(p,path[(i+1)%path.size()]);
    }
    if(!std::isfinite(pathArea) || pathArea==0)return reject("POCKET_WALL_TOOL_DOES_NOT_FIT");
    center/=path.size();
    const double cuttingDepth=feature.depth-stock.floor;
    const int layers=int(std::ceil(cuttingDepth/step));
    QString code;
    if(includeToolCall)code=QString("T%1 M6\nS%2 M3\n")
        .arg(tool.id).arg(int(params.get("spindleSpeed",3000)));
    code+=QString("G40\nG0 Z%1\n; POCKET REGION: VERIFIED WALL FINISH\n; POCKET ENTRY: VERTICAL\n")
        .arg(safe,0,'f',3);
    double length=0;
    for(int layer=1;layer<=layers;++layer) {
        const double target=top-std::min(layer*step,cuttingDepth);
        double z=QString::number(target,'f',3).toDouble();if(z<target)z+=.001;
        code+=QString("G0 X%1 Y%2\nG0 Z%3\nG1 Z%4 F%5\n")
            .arg(center.x(),0,'f',3).arg(center.y(),0,'f',3).arg(feedZ,0,'f',3).arg(z,0,'f',3).arg(int(plunge));
        QPointF previous=center;
        for(int i=0;i<=path.size();++i) {
            const auto p=path[i%path.size()];
            code+=QString("G1 X%1 Y%2 F%3\n").arg(p.x(),0,'f',3).arg(p.y(),0,'f',3).arg(int(feed));
            length+=std::hypot(p.x()-previous.x(),p.y()-previous.y());previous=p;
        }
        code+=QString("G0 Z%1\n").arg(safe,0,'f',3);
    }
    ToolpathResult result;result.ok=true;result.gcode=code;
    result.estimatedTimeS=length/feed*60+layers*cuttingDepth/plunge*60;
    return result;
}

// 函数说明：defaultParams负责当前模块的核心处理。
StrategyParams PocketFinishStrategy::defaultParams() const
{
    StrategyParams params;
    params.set("safeHeight", 50.0);
    params.set("feedHeight", 3.0);
    params.set("stepDown", 1.0);
    params.set("spindleSpeed", 3000.0);
    params.set("feedRate", 800.0);
    params.set("plungeRate", 200.0);
    params.set("sideStockToLeave", 0.0);
    params.set("floorStockToLeave", 0.0);
    return params;
}

// 函数说明：generate负责当前模块的核心处理。
ToolpathResult PocketFinishStrategy::generate(const HoleFeature &,
                                               const ToolEntry &,
                                               const StrategyParams &) const
{
    ToolpathResult result;
    result.errorMsg = QObject::tr("Pocket wall finishing requires a pocket feature.");
    return result;
}

// 函数说明：generate负责当前模块的核心处理。
ToolpathResult PocketFinishStrategy::generate(const ContourFeature &feature,
                                               const ToolEntry &tool,
                                               const StrategyParams &params) const
{
    if(feature.subType=="verified_slot_opening")return generateRegionPass(feature,tool,params,true);
    ToolpathResult result;
    const bool rectangular = feature.subType == QStringLiteral("rectangular_pocket");
    const bool circular = feature.subType == QStringLiteral("circular_pocket");
    if (!rectangular && !circular) {
        result.errorMsg = QObject::tr("Pocket wall finishing currently supports rectangular and circular pockets only.");
        return result;
    }
    if (tool.diameter <= 0.0 || feature.depth <= 0.0) {
        result.errorMsg = QObject::tr("Tool diameter and pocket depth must be greater than zero.");
        return result;
    }

    const double stepDown = params.get("stepDown", 1.0);
    const double feed = params.get("feedRate", 800.0);
    const double plunge = params.get("plungeRate", 200.0);
    const double legacyStock = params.get("stockToLeave", 0.0);
    const double sideStock = params.values.contains(QStringLiteral("sideStockToLeave"))
        ? params.get("sideStockToLeave", 0.0)
        : legacyStock;
    const double floorStock = params.values.contains(QStringLiteral("floorStockToLeave"))
        ? params.get("floorStockToLeave", 0.0)
        : legacyStock;
    if (stepDown <= 0.0 || feed <= 0.0 || plunge <= 0.0 ||
        sideStock < 0.0 || floorStock < 0.0) {
        result.errorMsg = QObject::tr("Stepdown, feed, and plunge must be positive; side and floor stock cannot be negative.");
        return result;
    }
    const double effectiveDepth = feature.depth - floorStock;
    if (effectiveDepth <= 0.0) {
        result.errorMsg = QObject::tr("Floor stock must be smaller than the pocket depth.");
        return result;
    }

    const double toolRadius = tool.diameter * 0.5;
    const double cx = feature.center.x();
    const double cy = feature.center.y();
    const double angleRad = rectangular
        ? feature.angle * std::acos(-1.0) / 180.0
        : 0.0;
    const double cosAngle = std::cos(angleRad);
    const double sinAngle = std::sin(angleRad);
    auto mapPocketPoint = [=](double u, double v) {
        return QVector3D(float(cx + u * cosAngle - v * sinAngle),
                         float(cy + u * sinAngle + v * cosAngle),
                         0.0f);
    };
    double halfLength = 0.0;
    double halfWidth = 0.0;
    double pathRadius = 0.0;
    if (rectangular) {
        halfLength = feature.length * 0.5 - toolRadius - sideStock;
        halfWidth = feature.width * 0.5 - toolRadius - sideStock;
        if (halfLength <= 0.0 || halfWidth <= 0.0) {
            result.errorMsg = QObject::tr("Tool diameter plus stock to leave does not fit inside the pocket.");
            return result;
        }
    } else {
        pathRadius = feature.radius - toolRadius - sideStock;
        if (pathRadius <= 0.0) {
            result.errorMsg = QObject::tr("Tool diameter plus stock to leave does not fit inside the pocket.");
            return result;
        }
    }

    const double topZ = feature.center.z();
    const double safeZ = params.get("safeHeight", 50.0);
    const double feedZ = topZ + params.get("feedHeight", 3.0);
    const int layers = static_cast<int>(std::ceil(effectiveDepth / stepDown));
    QString gcode;
    gcode += QStringLiteral("T%1 M6\nS%2 M3\nG0 Z%3\n")
                 .arg(tool.id)
                 .arg(int(params.get("spindleSpeed", 3000.0)))
                 .arg(safeZ, 0, 'f', 3);

    double pathLength = 0.0;
    for (int layer = 1; layer <= layers; ++layer) {
        const double z = topZ - std::min(layer * stepDown, effectiveDepth);
        const QVector3D startPoint = rectangular
            ? mapPocketPoint(-halfLength, -halfWidth)
            : QVector3D(float(cx + pathRadius), float(cy), 0.0f);
        const double startX = startPoint.x();
        const double startY = startPoint.y();
        gcode += QStringLiteral("; Pocket wall finish Z=%1\nG0 X%2 Y%3\nG0 Z%4\nG1 Z%5 F%6\nG40\nG1 X%7 Y%8 F%9\n")
                     .arg(z, 0, 'f', 3)
                     .arg(cx, 0, 'f', 3)
                     .arg(cy, 0, 'f', 3)
                     .arg(feedZ, 0, 'f', 3)
                     .arg(z, 0, 'f', 3)
                     .arg(int(plunge))
                     .arg(startX, 0, 'f', 3)
                     .arg(startY, 0, 'f', 3)
                     .arg(int(feed));
        pathLength += rectangular
            ? std::sqrt(halfLength * halfLength + halfWidth * halfWidth)
            : pathRadius;
        if (rectangular) {
            const QVector3D lowerRight = mapPocketPoint(halfLength, -halfWidth);
            const QVector3D upperRight = mapPocketPoint(halfLength, halfWidth);
            const QVector3D upperLeft = mapPocketPoint(-halfLength, halfWidth);
            gcode += QStringLiteral("G1 X%1 Y%2 F%3\nG1 X%4 Y%5\nG1 X%6 Y%7\nG1 X%8 Y%9\n")
                         .arg(lowerRight.x(), 0, 'f', 3).arg(lowerRight.y(), 0, 'f', 3).arg(int(feed))
                         .arg(upperRight.x(), 0, 'f', 3).arg(upperRight.y(), 0, 'f', 3)
                         .arg(upperLeft.x(), 0, 'f', 3).arg(upperLeft.y(), 0, 'f', 3)
                         .arg(startX, 0, 'f', 3).arg(startY, 0, 'f', 3);
            pathLength += 4.0 * (halfLength + halfWidth);
        } else {
            gcode += QStringLiteral("G2 X%1 Y%2 I%3 J0.000 F%4\n")
                         .arg(startX, 0, 'f', 3).arg(startY, 0, 'f', 3)
                         .arg(-pathRadius, 0, 'f', 3).arg(int(feed));
            pathLength += 2.0 * std::acos(-1.0) * pathRadius;
        }
        gcode += QStringLiteral("G0 Z%1\n").arg(safeZ, 0, 'f', 3);
    }

    result.gcode = gcode;
    result.ok = true;
    result.estimatedTimeS = pathLength / feed * 60.0 + effectiveDepth / plunge * 60.0;
    return result;
}
