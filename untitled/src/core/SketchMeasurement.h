#pragma once
#include "SketchDocument.h"

struct SketchMeasurement
{
    bool valid = false;
    double value = 0;
    QPointF point;
    double z = 0;
    QString text;
};

inline SketchMeasurement measureSketch(const SketchDocument &document,const SketchAnnotation &annotation)
{
    SketchMeasurement result;result.text=QStringLiteral("失效：请重新选择图元或点／边。");
    // Whole-document validation belongs to load/save; display checks its referenced values.
    if(!annotation.valid() || !std::isfinite(document.planeZ))return result;
    const auto entity=[&](const SketchGeometryReference &ref,SketchContour *out) {
        for(int i=0;i<document.contourCount();++i)if(QUuid(document.contour(i).id)==QUuid(ref.contourId)) {
            *out=document.contour(i);
            if(out->isCurve())return out->points.size()==1 && std::isfinite(out->radius) && out->radius>0 &&
                std::isfinite(out->startAngle) && std::isfinite(out->sweepAngle);
            return (out->kind=="line" || out->kind=="rectangle" || out->kind=="polyline") && out->points.size()>=2;
        }
        return false;
    };
    const auto point=[&](const SketchGeometryReference &ref,QPointF *out) {
        SketchContour c;if(!entity(ref,&c))return false;
        if(ref.kind=="vertex" && !c.isCurve() && ref.index<c.points.size()) {*out=c.points[ref.index];return true;}
        if(!c.isCurve())return false;
        if(ref.kind=="center")*out=c.points[0];
        else if(ref.kind=="start")*out=c.curvePoint(c.normalizedStart());
        else if(ref.kind=="end" && c.kind=="arc")*out=c.curvePoint(c.normalizedStart()+c.sweepAngle);
        else return false;
        return true;
    };
    const auto edge=[&](const SketchGeometryReference &ref,QPointF *a,QPointF *b) {
        SketchContour c;if(!entity(ref,&c) || c.isCurve() || ref.kind!="edge")return false;
        const int count=c.kind=="line"?1:c.points.size();if(ref.index>=count)return false;
        *a=c.points[ref.index];*b=c.points[(ref.index+1)%c.points.size()];return *a!=*b;
    };
    const auto number=[](double value){return QString::number(value,'g',17);};
    if(annotation.kind=="radius") {
        SketchContour c;if(!entity(annotation.first,&c) || !c.isCurve())return result;
        result.point=c.points[0];result.value=c.radius;result.text=QStringLiteral("R %1 mm").arg(number(c.radius));
    } else if(annotation.kind=="angle") {
        QPointF a,b,c,d;if(!edge(annotation.first,&a,&b) || !edge(annotation.second,&c,&d))return result;
        const long double ux=static_cast<long double>(b.x())-a.x(),uy=static_cast<long double>(b.y())-a.y();
        const long double vx=static_cast<long double>(d.x())-c.x(),vy=static_cast<long double>(d.y())-c.y();
        result.value=double(std::atan2(std::abs(ux*vy-uy*vx),ux*vx+uy*vy)*180/std::acos(-1.0L));
        result.point=a;result.text=QStringLiteral("夹角 %1°").arg(number(result.value));
    } else {
        if(!point(annotation.first,&result.point))return result;
        if(annotation.kind=="coordinate")
            result.text=QStringLiteral("X %1  Y %2  Z %3 mm").arg(number(result.point.x()),number(result.point.y()),number(document.planeZ));
        else {
            QPointF second;if(!point(annotation.second,&second))return result;
            result.value=double(std::hypot(static_cast<long double>(second.x())-result.point.x(),static_cast<long double>(second.y())-result.point.y()));
            result.text=QStringLiteral("距离 %1 mm").arg(number(result.value));
        }
    }
    result.z=document.planeZ;
    result.valid=std::isfinite(result.value) && std::isfinite(result.point.x()) && std::isfinite(result.point.y());
    if(!result.valid)result.text=QStringLiteral("失效：引用几何含非有限数值。");
    return result;
}
