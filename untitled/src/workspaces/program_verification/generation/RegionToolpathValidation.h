#pragma once
#include "../../process_design/operations/MachiningOperation.h"
#include "../simulation/SimulationController.h"
#include "../../../strategies/mill/PocketAllowances.h"
#include <QPolygonF>
#include <QLineF>
#include <cmath>
#include <algorithm>

inline double regionPointSegmentDistance(QPointF p,QPointF a,QPointF b)
{
    const auto d=b-a;const double length=d.x()*d.x()+d.y()*d.y();
    const double t=length>0?qBound(0.0,((p-a).x()*d.x()+(p-a).y()*d.y())/length,1.0):0;
    const auto delta=p-a-d*t;return std::hypot(delta.x(),delta.y());
}
inline double regionSegmentDistance(QPointF a,QPointF b,QPointF c,QPointF d)
{
    auto cross=[](QPointF u,QPointF v){return u.x()*v.y()-u.y()*v.x();};
    const double ab1=cross(b-a,c-a),ab2=cross(b-a,d-a),cd1=cross(d-c,a-c),cd2=cross(d-c,b-c);
    if(ab1*ab2<=0 && cd1*cd2<=0 &&
       qMax(qMin(a.x(),b.x()),qMin(c.x(),d.x()))<=qMin(qMax(a.x(),b.x()),qMax(c.x(),d.x())) &&
       qMax(qMin(a.y(),b.y()),qMin(c.y(),d.y()))<=qMin(qMax(a.y(),b.y()),qMax(c.y(),d.y())))return 0;
    return std::min({regionPointSegmentDistance(a,c,d),regionPointSegmentDistance(b,c,d),
                     regionPointSegmentDistance(c,a,b),regionPointSegmentDistance(d,a,b)});
}
// Check the whole center segment plus the cutter disk, not only its endpoints.
// An original arc is enclosed by its chord thickened by the analytic sagitta.
inline QString regionMoveError(const ToolMove &move,const ContourFeature &feature,
                               const StrategyParams &params,double diameter)
{
    constexpr double ncTolerance=.0015; // Feed-plane comparison only; never enlarges the cutting volume.
    const auto allowances=pocketRoughingAllowances(params,0);
    if(!allowances.valid(feature.depth))return "REGION_ALLOWANCE_INVALID";
    const double top=feature.center.z(),feed=top+params.get("feedHeight",3);
    const double bottom=top-feature.depth+allowances.floor;
    if((move.toKnownAxes & 4) && move.to.z()<bottom)return "REGION_TOOLPATH_DEPTH_EXCEEDED";
    if(!(move.fromKnownAxes & 4)){
        // A standalone absolute safe-Z rapid establishes a plane, not a verified trajectory
        // from the machine's unknown initial location. No lateral or feed motion is certified.
        if(move.isRapid && move.commandedAxes==4 && (move.toKnownAxes & 4) && move.to.z()>=feed-ncTolerance)return {};
        return "REGION_INITIAL_POSITION_UNKNOWN";
    }
    if(!(move.toKnownAxes & 4))return "REGION_INITIAL_POSITION_UNKNOWN";
    if(move.from.z()<bottom)return "REGION_TOOLPATH_DEPTH_EXCEEDED";
    const QPointF a(move.from.x(),move.from.y()),b(move.to.x(),move.to.y());
    if(move.isRapid && a!=b && qMin(move.from.z(),move.to.z())<feed-ncTolerance)
        return "REGION_LOW_RAPID_TRAVERSE";
    if(qMin(move.from.z(),move.to.z())>=feed-ncTolerance)return {};
    if(move.fromKnownAxes!=7 || move.toKnownAxes!=7)return "REGION_INITIAL_POSITION_UNKNOWN";
    // Known vertical retract retains the depth checks above.
    if(move.isRapid && a==b && move.to.z()>=move.from.z())return {};
    const double clearance=diameter*.5+allowances.side+move.chordDeviation;
    auto polygon=[](const QVector<QVector3D>&points){QPolygonF p;for(const auto &v:points)p<<QPointF(v.x(),v.y());return p;};
    const auto outer=polygon(feature.points);
    if(outer.size()<3 || !outer.containsPoint((a+b)*.5,Qt::OddEvenFill))return "REGION_TOOLPATH_OUTSIDE_BOUNDARY";
    auto nearBoundary=[&](const QPolygonF &p){for(int i=0;i<p.size();++i)if(regionSegmentDistance(a,b,p[i],p[(i+1)%p.size()])<clearance)return true;return false;};
    if(nearBoundary(outer))return "REGION_TOOL_ENVELOPE_OUTSIDE_BOUNDARY";
    for(const auto &points:feature.islands){const auto island=polygon(points);if(island.containsPoint((a+b)*.5,Qt::OddEvenFill)||nearBoundary(island))return "REGION_TOOL_ENVELOPE_INTERSECTS_ISLAND";}
    return {};
}
