#pragma once
#include "SketchDocument.h"

struct SketchTrimOption { QString label; SketchDocument document; };
struct SketchTrimResult { QString error; QVector<SketchTrimOption> options; };

namespace SketchTrimDetail {
constexpr long double tolerance=64*std::numeric_limits<double>::epsilon();
struct Vector {long double x,y;};
inline Vector delta(const QPointF &a,const QPointF &b){return {static_cast<long double>(a.x())-b.x(),static_cast<long double>(a.y())-b.y()};}
inline long double dot(Vector a,Vector b){return a.x*b.x+a.y*b.y;}
inline long double cross(Vector a,Vector b){return a.x*b.y-a.y*b.x;}
inline bool same(long double a,long double b){return std::abs(a-b)<=tolerance*std::max({1.0L,std::abs(a),std::abs(b)});}
inline bool inRange(long double t){return t>=-tolerance && t<=1+tolerance;}
inline QPointF at(const SketchContour &c,long double t)
{
    if(c.isCurve())return c.curvePoint(c.normalizedStart()+double(t*c.sweepAngle));
    const auto v=delta(c.points[1],c.points[0]);
    return {double(c.points[0].x()+t*v.x),double(c.points[0].y()+t*v.y)};
}
inline long double parameter(const SketchContour &c,const QPointF &p,bool before=false)
{
    if(!c.isCurve()) {const auto v=delta(c.points[1],c.points[0]);return dot(delta(p,c.points[0]),v)/dot(v,v);}
    const auto v=delta(p,c.points[0]);
    const long double angle=std::atan2(v.y,v.x)*180/std::acos(-1.0L);
    long double travel=std::fmod(c.sweepAngle>0?angle-c.normalizedStart():c.normalizedStart()-angle,360.0L);
    if(before) {if(travel>0)travel-=360;}
    else if(travel<0)travel+=360;
    return travel/std::abs(c.sweepAngle);
}
inline QVector<SketchContour> edges(const SketchContour &c)
{
    if(c.kind=="line" || c.isCurve())return {c};
    QVector<SketchContour> result;
    for(int i=0;i<c.points.size();++i)result.append({c.id,"line",{c.points[i],c.points[(i+1)%c.points.size()]}});
    return result;
}
// Intersections of the analytic supports; the caller clips to actual boundary domains.
inline QVector<QPointF> intersections(const SketchContour &a,const SketchContour &b,QString *error,long double domainStart=0,long double domainEnd=1)
{
    QVector<QPointF> result;
    if(!a.isCurve() && !b.isCurve()) {
        const auto u=delta(a.points[1],a.points[0]),v=delta(b.points[1],b.points[0]),w=delta(b.points[0],a.points[0]);
        const auto determinant=cross(u,v);
        if(std::abs(determinant)<=tolerance*std::sqrt(dot(u,u)*dot(v,v))) {
            if(std::abs(cross(w,u))<=tolerance*std::sqrt(dot(u,u)*dot(w,w))) {
                const auto first=parameter(a,b.points[0]),last=parameter(a,b.points[1]);
                if(std::max(domainStart,std::min(first,last))<std::min(domainEnd,std::max(first,last)))
                    *error=QStringLiteral("目标与边界共线重叠，无法确定唯一交点。");
            }
            return result;
        }
        result.append(at(a,cross(w,v)/determinant));return result;
    }
    if(!a.isCurve() || !b.isCurve()) {
        const auto &line=a.isCurve()?b:a;const auto &curve=a.isCurve()?a:b;
        const auto v=delta(line.points[1],line.points[0]),w=delta(curve.points[0],line.points[0]);
        const auto length2=dot(v,v),middle=dot(w,v)/length2;
        const auto perpendicular=cross(w,v);
        const long double distance2=perpendicular*perpendicular/length2,r2=static_cast<long double>(curve.radius)*curve.radius;
        long double h2=r2-distance2;
        if(h2< -tolerance*std::max(r2,distance2))return result;
        h2=std::max(0.0L,h2);const auto step=std::sqrt(h2/length2);
        result.append(at(line,middle-step));if(step>0)result.append(at(line,middle+step));return result;
    }
    const auto v=delta(b.points[0],a.points[0]);const auto distance=std::hypot(v.x,v.y);
    const long double ra=a.radius,rb=b.radius;
    if(distance==0) {
        if(ra==rb)*error=QStringLiteral("目标与边界同圆，无法确定唯一交点。");
        return result;
    }
    if(distance>ra+rb+tolerance*std::max(ra,rb) || distance<std::abs(ra-rb)-tolerance*std::max(ra,rb))return result;
    const auto along=(distance+(ra-rb)*(ra+rb)/distance)/2;
    long double h2=(ra-along)*(ra+along);
    if(h2< -tolerance*ra*ra)return result;
    h2=std::max(0.0L,h2);const auto height=std::sqrt(h2);
    for(int sign : {-1,1}) {
        result.append({double(a.points[0].x()+(along*v.x-sign*height*v.y)/distance),
                       double(a.points[0].y()+(along*v.y+sign*height*v.x)/distance)});
        if(height==0)break;
    }
    return result;
}
inline SketchContour piece(const SketchContour &source,long double start,long double end,bool fresh)
{
    auto c=source;
    if(fresh)c.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(c.isCurve()) {c.kind="arc";c.startAngle=std::fmod(source.normalizedStart()+double(start*source.sweepAngle),360.0);c.sweepAngle=double((end-start)*source.sweepAngle);}
    else c.points={at(source,start),at(source,end)};
    return c;
}
inline SketchDocument replace(const SketchDocument &d,int target,const QVector<SketchContour> &pieces)
{
    auto result=d;QVector<SketchContour> all;
    for(int i=0;i<d.contourCount();++i) {if(i==target)all+=pieces;else all.append(d.contour(i));}
    result.additionalContours.clear();result.setContour(0,all.first());
    for(int i=1;i<all.size();++i)result.additionalContours.append(all[i]);
    return result;
}
inline QString pointText(const QPointF &p){return QStringLiteral("(%1, %2)").arg(p.x(),0,'g',17).arg(p.y(),0,'g',17);}
}

inline SketchTrimResult sketchTrimOptions(const SketchDocument &document,int target,int boundary,int edge,bool extend,int endpoint)
{
    using namespace SketchTrimDetail;
    SketchTrimResult result;
    if(!document.validationError().isEmpty() || target<0 || boundary<0 || target>=document.contourCount() || boundary>=document.contourCount() || target==boundary) {
        result.error=QStringLiteral("请选择两个不同的有效图元作为目标和边界。");return result;
    }
    const auto original=document.contour(target);const auto targetEdges=edges(original);
    if(edge<0 || edge>=targetEdges.size() || (extend && (original.kind!="line" && original.kind!="arc")) || (extend && endpoint!=0 && endpoint!=1)) {
        result.error=QStringLiteral("延伸需要开放直线／圆弧端点；修剪需要有效目标边。");return result;
    }
    const auto source=targetEdges[edge];QVector<long double> cuts;
    for(const auto &limit:edges(document.contour(boundary))) {
        const auto points=intersections(source,limit,&result.error,
            extend && endpoint==0?-std::numeric_limits<long double>::infinity():extend?1:0,
            extend && endpoint==1?std::numeric_limits<long double>::infinity():extend?0:1);
        if(!result.error.isEmpty())return result;
        for(const auto &p:points) {
            if(limit.kind!="circle" && !inRange(parameter(limit,p)))continue;
            auto t=parameter(source,p,extend && endpoint==0);
            if(extend) {
                if(endpoint==0?t>=-tolerance:t<=1+tolerance)continue;
                if(source.isCurve() && (endpoint==0?(1-t)*std::abs(source.sweepAngle):t*std::abs(source.sweepAngle))>=360-tolerance*360)continue;
            } else {
                if(source.kind!="circle" && (t<=tolerance || t>=1-tolerance))continue;
            }
            bool duplicate=false;for(auto existing:cuts)duplicate |= same(existing,t);
            if(!duplicate)cuts.append(t);
        }
    }
    std::sort(cuts.begin(),cuts.end());
    const auto add=[&](const QVector<SketchContour> &pieces,const QString &label) {
        auto candidate=replace(document,target,pieces);const auto error=candidate.validationError();
        if(error.isEmpty())result.options.append({label,candidate});else if(result.error.isEmpty())result.error=error;
    };
    if(extend) {
        if(endpoint==0)std::reverse(cuts.begin(),cuts.end());
        for(auto t:cuts) {
            const auto c=piece(source,endpoint==0?t:0,endpoint==0?1:t,false);
            add({c},QStringLiteral("延伸至 %1 mm").arg(pointText(at(source,t))));
        }
    } else if(!cuts.isEmpty() && (source.kind!="circle" || cuts.size()>=2)) {
        if(source.kind!="circle") {cuts.prepend(0);cuts.append(1);}else cuts.append(cuts.first()+1);
        for(int removed=0;removed+1<cuts.size();++removed) {
            QVector<SketchContour> pieces;
            for(int i=0;i<targetEdges.size();++i) {
                if(i!=edge) {pieces.append(piece(targetEdges[i],0,1,true));continue;}
                if(source.kind=="circle")pieces.append(piece(source,cuts[removed+1],cuts[removed]+1,true));
                else {if(cuts[removed]>0)pieces.append(piece(source,0,cuts[removed],true));if(cuts[removed+1]<1)pieces.append(piece(source,cuts[removed+1],1,true));}
            }
            add(pieces,QStringLiteral("删除 %1 → %2 mm%3").arg(pointText(at(source,cuts[removed])),pointText(at(source,cuts[removed+1])),
                source.isCurve()?QStringLiteral(" · 扫角 %1°").arg(double((cuts[removed+1]-cuts[removed])*source.sweepAngle),0,'g',17):QString()));
        }
    }
    if(!result.options.isEmpty())result.error.clear();
    else if(result.error.isEmpty())result.error=QStringLiteral("没有可用结果：请检查交点、所选端点、相切及图元尺寸。");
    return result;
}
