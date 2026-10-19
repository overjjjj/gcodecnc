#pragma once
#include "../StrategyBase.h"
#include <algorithm>
#include <cmath>

namespace SlopedFloorRaster {
struct Interval {double min=0,max=0;};
static QVector<Interval> mergedIntervals(QVector<Interval> intervals)
{
    std::sort(intervals.begin(), intervals.end(), [](const Interval &a, const Interval &b) {
        return a.min < b.min;
    });
    QVector<Interval> merged;
    for (const Interval &interval : intervals) {
        if (interval.max <= interval.min + 1.0e-9) {
            continue;
        }
        if (merged.isEmpty() || interval.min > merged.last().max + 1.0e-9) {
            merged.push_back(interval);
        } else {
            merged.last().max = std::max(merged.last().max, interval.max);
        }
    }
    return merged;
}

static QVector<Interval> polygonIntervalsAtY(const QVector<QVector3D> &polygon, double y)
{
    QVector<double> intersections;
    for (int i = 0; i < polygon.size(); ++i) {
        const QVector3D &a = polygon.at(i);
        const QVector3D &b = polygon.at((i + 1) % polygon.size());
        const double ay = a.y();
        const double by = b.y();
        if ((ay <= y && by > y) || (by <= y && ay > y)) {
            const double t = (y - ay) / (by - ay);
            intersections.push_back(a.x() + t * (b.x() - a.x()));
        }
    }
    std::sort(intersections.begin(), intersections.end());
    QVector<Interval> intervals;
    for (int i = 0; i + 1 < intersections.size(); i += 2) {
        intervals.push_back({intersections.at(i), intersections.at(i + 1)});
    }
    return intervals;
}

static QVector<Interval> subtractIntervals(const QVector<Interval> &source,
                                           const QVector<Interval> &removed)
{
    QVector<Interval> result = source;
    for (const Interval &cut : mergedIntervals(removed)) {
        QVector<Interval> next;
        for (const Interval &part : result) {
            if (cut.max <= part.min || cut.min >= part.max) {
                next.push_back(part);
                continue;
            }
            if (cut.min > part.min) {
                next.push_back({part.min, std::min(cut.min, part.max)});
            }
            if (cut.max < part.max) {
                next.push_back({std::max(cut.max, part.min), part.max});
            }
        }
        result = next;
    }
    return result;
}

static void appendCircleInterval(QVector<Interval> &intervals,
                                 const QVector3D &center,
                                 double y,
                                 double radius)
{
    const double dy = y - center.y();
    if (std::abs(dy) >= radius) {
        return;
    }
    const double span = std::sqrt(std::max(0.0, radius * radius - dy * dy));
    intervals.push_back({center.x() - span, center.x() + span});
}

static QVector<Interval> boundaryClearanceIntervals(const QVector<QVector3D> &polygon,
                                                    double y,
                                                    double clearance)
{
    QVector<Interval> forbidden;
    for (int i = 0; i < polygon.size(); ++i) {
        const QVector3D &a = polygon.at(i);
        const QVector3D &b = polygon.at((i + 1) % polygon.size());
        const double dx = b.x() - a.x();
        const double dy = b.y() - a.y();
        const double length = std::sqrt(dx * dx + dy * dy);
        if (length <= 1.0e-9) {
            appendCircleInterval(forbidden, a, y, clearance);
            continue;
        }
        const double nx = -dy / length;
        const double ny = dx / length;
        const QVector<QVector3D> strip = {
            QVector3D(float(a.x() + nx * clearance), float(a.y() + ny * clearance), 0.0f),
            QVector3D(float(b.x() + nx * clearance), float(b.y() + ny * clearance), 0.0f),
            QVector3D(float(b.x() - nx * clearance), float(b.y() - ny * clearance), 0.0f),
            QVector3D(float(a.x() - nx * clearance), float(a.y() - ny * clearance), 0.0f)
        };
        forbidden += polygonIntervalsAtY(strip, y);
        appendCircleInterval(forbidden, a, y, clearance);
        appendCircleInterval(forbidden, b, y, clearance);
    }
    return mergedIntervals(forbidden);
}

static QVector<Interval> safeIntervalsAtY(
    const QVector<QVector3D> &outer,
    const QVector<QVector<QVector3D>> &islands,
    double y,
    double clearance)
{
    QVector<Interval> allowed = polygonIntervalsAtY(outer, y);
    QVector<Interval> removed = boundaryClearanceIntervals(outer, y, clearance);
    for (const QVector<QVector3D> &island : islands) {
        removed += polygonIntervalsAtY(island, y);
        removed += boundaryClearanceIntervals(island, y, clearance);
    }
    return subtractIntervals(allowed, removed);
}

}


class SlopedFloorStrategy : public StrategyBase {
public:
    QString id() const override {return QStringLiteral("mill_sloped_floor");}
    QString displayName() const override {return QStringLiteral("带孔斜面底面加工");}
    StrategyParams defaultParams() const override {
        StrategyParams p;
        p.set("safeHeight",50);p.set("feedHeight",3);p.set("stepover",.5);
        p.set("feedRate",300);p.set("plungeRate",100);p.set("spindleSpeed",4000);
        p.set("sideStockToLeave",.3);p.set("floorStockToLeave",.01);
        p.set("entryMode",-1);
        return p;
    }
    ToolpathResult generate(const HoleFeature &,const ToolEntry &,const StrategyParams &) const override {
        return {QString(),false,QStringLiteral("SLOPED_FLOOR_FACE_REQUIRED"),0};
    }
    ToolpathResult generate(const ContourFeature &feature,const ToolEntry &tool,const StrategyParams &params) const override {
        auto fail=[](const char *error){return ToolpathResult{QString(),false,QString::fromLatin1(error),0};};
        if(params.get("entryMode",-1)!=0)return fail("SLOPED_FLOOR_CLEARED_VERTICAL_ENTRY_REQUIRED");
        if(feature.subType!="verified_sloped_floor" || !feature.planarBoundaryVerified || feature.points.size()<3 ||
           tool.type!="end_mill" || !tool.modelPath.isEmpty() || tool.diameter<=0 || tool.fluteLen<=0 ||
           tool.totalLen<tool.fluteLen || tool.shankDia<=0 || tool.shankDia>tool.diameter)
            return fail("SLOPED_FLOOR_SOURCE_AND_NOMINAL_TOOL_REQUIRED");
        const double safe=params.get("safeHeight",50),feedHeight=params.get("feedHeight",3),step=params.get("stepover",.5);
        const double feed=params.get("feedRate",300),plunge=params.get("plungeRate",100),spindle=params.get("spindleSpeed",4000);
        const double side=params.get("sideStockToLeave",.3),stock=params.get("floorStockToLeave",.01);
        for(double v:{safe,feedHeight,step,feed,plunge,spindle,side,stock,tool.diameter,tool.fluteLen,tool.totalLen,tool.shankDia,
                      feature.center.x(),feature.center.y(),feature.center.z(),double(feature.axis.x()),double(feature.axis.y()),double(feature.axis.z())})
            if(!std::isfinite(v))return fail("SLOPED_FLOOR_NONFINITE_INPUT");
        if(step<.001 || step>tool.diameter || feed<.001 || plunge<.001 || spindle<1 || spindle>2147483647. || feedHeight<=0 || side<0 || stock<0 || feature.axis.z()<=0)
            return fail("SLOPED_FLOOR_PARAMETERS_INVALID");
        auto loops=feature.islands;loops.prepend(feature.points);
        for(const auto &loop:loops) {
            if(loop.size()<3)return fail("SLOPED_FLOOR_BOUNDARY_INVALID");
            for(const auto &p:loop)if(!std::isfinite(p.x()) || !std::isfinite(p.y()) || !std::isfinite(p.z()))return fail("SLOPED_FLOOR_BOUNDARY_INVALID");
        }
        const double gx=-feature.axis.x()/feature.axis.z(),gy=-feature.axis.y()/feature.axis.z();
        // The final source-face sweep gate remains authoritative. These allowances
        // absorb bounded source sampling and three-decimal output rounding.
        const double lift=tool.diameter*.5*std::hypot(gx,gy)+stock+.002;
        auto z=[&](double x,double y){return feature.center.z()+gx*(x-feature.center.x())+gy*(y-feature.center.y())+lift;};
        double minY=feature.points.first().y(),maxY=minY,top=-1e100;
        for(const auto &p:feature.points){minY=std::min(minY,double(p.y()));maxY=std::max(maxY,double(p.y()));top=std::max(top,z(p.x(),p.y()));}
        if(!std::isfinite(top) || safe<top+feedHeight)return fail("SLOPED_FLOOR_SAFE_HEIGHT_INVALID");
        // Match the conservative rectangular sweep gate, including its end corners.
        const double clearance=tool.diameter*.5*std::sqrt(2.)+side+.002;
        const double first=std::ceil((minY+clearance)*1000)/1000,last=std::floor((maxY-clearance)*1000)/1000;
        const double count=std::ceil((last-first)/step);
        if(!std::isfinite(count) || count<0 || count>10000)return fail("SLOPED_FLOOR_RASTER_SIZE_INVALID");
        const int n=std::max(1,int(count));
        QString code=QStringLiteral("G17 G90 G94 G40\nT%1 M6\nS%2 M3\nG0 Z%3\n; PARTIAL SLOPED FLOOR: FIXED-Z ACCESS ONLY; OCCLUDED STRIPS RETAINED\n; HOLE LINKS RETRACT; SIDE AND FLOOR STOCK RETAINED\n")
            .arg(tool.id).arg(int(spindle)).arg(safe,0,'f',3);
        int rows=0;double seconds=0;
        for(int i=0;i<=n;++i) {
            const double y=std::round((first+(last-first)*i/n)*1000)/1000;
            for(const auto &span:SlopedFloorRaster::safeIntervalsAtY(feature.points,feature.islands,y,clearance)) {
                const double a=std::ceil(span.min*1000)/1000,b=std::floor(span.max*1000)/1000;
                if(b-a<.001)continue;
                const double za=std::ceil(z(a,y)*1000)/1000,zb=std::ceil(z(b,y)*1000)/1000;
                if(!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(za) || !std::isfinite(zb))return fail("SLOPED_FLOOR_OUTPUT_NONFINITE");
                code+=QStringLiteral("G0 X%1 Y%2\nG1 Z%3 F%4\nG1 X%5 Y%2 Z%6 F%7\nG0 Z%8\n")
                    .arg(a,0,'f',3).arg(y,0,'f',3).arg(za,0,'f',3).arg(plunge,0,'f',3)
                    .arg(b,0,'f',3).arg(zb,0,'f',3).arg(feed,0,'f',3).arg(safe,0,'f',3);
                seconds+=(std::hypot(b-a,zb-za)/feed+(safe-za)/plunge)*60;++rows;
            }
        }
        if(!rows)return fail("SLOPED_FLOOR_NO_REACHABLE_ROWS");
        return {code,true,QString(),seconds};
    }
};
