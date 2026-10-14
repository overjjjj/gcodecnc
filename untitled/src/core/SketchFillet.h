#pragma once
#include "SketchTrim.h"

struct SketchFilletResult {QString error;SketchDocument document;};

inline SketchFilletResult sketchFillet(const SketchDocument &document,int target,int corner,int second,double radius)
{
    using namespace SketchTrimDetail;
    const auto fail=[&](const QString &error){return SketchFilletResult{error,document};};
    if(!document.validationError().isEmpty() || target<0 || target>=document.contourCount())return fail(QStringLiteral("请选择有效的圆角目标。"));
    if(!std::isfinite(radius) || radius<=0)return fail(QStringLiteral("圆角半径必须为有限正数（mm）。"));
    const auto original=document.contour(target);const bool polygon=original.kind=="rectangle" || original.kind=="polyline";
    QPointF a,b,c;int firstEndpoint=-1,secondEndpoint=-1;
    if(polygon) {
        if(corner<0 || corner>=original.points.size())return fail(QStringLiteral("请选择有效的轮廓角点。"));
        a=original.points[(corner+original.points.size()-1)%original.points.size()];b=original.points[corner];c=original.points[(corner+1)%original.points.size()];
    } else {
        if(original.kind!="line" || second<0 || second>=document.contourCount() || second==target || document.contour(second).kind!="line")
            return fail(QStringLiteral("请选择两条具有唯一共端点的独立直线。"));
        const auto other=document.contour(second);int matches=0;
        for(int i=0;i<2;++i)for(int j=0;j<2;++j)
            if(original.points[i].x()==other.points[j].x() && original.points[i].y()==other.points[j].y()){++matches;firstEndpoint=i;secondEndpoint=j;}
        if(matches!=1)return fail(QStringLiteral("两直线必须恰有一个相同端点；不自动延长或猜测交点。"));
        a=original.points[1-firstEndpoint];b=original.points[firstEndpoint];c=other.points[1-secondEndpoint];
    }
    const auto ba=delta(a,b),bc=delta(c,b);const auto lengthA=std::hypot(ba.x,ba.y),lengthC=std::hypot(bc.x,bc.y);
    const Vector u{ba.x/lengthA,ba.y/lengthA},v{bc.x/lengthC,bc.y/lengthC};
    const auto determinant=cross(u,v),cosine=dot(u,v);
    if(determinant==0)return fail(QStringLiteral("共线或折返角点无法建立唯一圆角。"));
    const long double tangent=radius*(cosine>=0?(1+cosine)/std::abs(determinant):std::abs(determinant)/(1-cosine));
    if(!std::isfinite(tangent) || tangent<=0 || tangent>=lengthA || tangent>=lengthC)
        return fail(QStringLiteral("半径过大或角点退化；两条邻边都必须保留正长度。"));
    const QPointF t1(double(b.x()+tangent*u.x),double(b.y()+tangent*u.y));
    const QPointF t2(double(b.x()+tangent*v.x),double(b.y()+tangent*v.y));
    const long double sign=determinant<0?1:-1;
    // Incoming tangent is -u. Its left normal is (u.y,-u.x).
    const QPointF center(double(b.x()+tangent*u.x+sign*radius*u.y),double(b.y()+tangent*u.y-sign*radius*u.x));
    const auto radial=delta(t1,center);
    const double start=double(std::atan2(radial.y,radial.x)*180/std::acos(-1.0L));
    const double sweep=double(std::atan2(-determinant,-cosine)*180/std::acos(-1.0L));
    SketchContour arc{QUuid::createUuid().toString(QUuid::WithoutBraces),"arc",{center},radius,start,sweep};
    const auto first=arc.curvePoint(start),last=arc.curvePoint(start+sweep);
    const long double precision=radius*1e-8L;
    const auto distance=[](Vector d){return std::hypot(d.x,d.y);};
    const auto firstDirection=delta(first,a),lastDirection=delta(c,last);
    const auto firstLength=distance(firstDirection),lastLength=distance(lastDirection);
    if(distance(delta(first,t1))>precision || distance(delta(last,t2))>precision ||
       std::abs(dot(delta(first,center),u))>precision || std::abs(dot(delta(last,center),v))>precision ||
       distance(delta(first,b))<=precision || distance(delta(last,b))<=precision ||
       firstLength==0 || lastLength==0 || dot(firstDirection,u)>=0 || dot(lastDirection,v)<=0 ||
       std::abs(dot(delta(first,center),firstDirection))>precision*firstLength ||
       std::abs(dot(delta(last,center),lastDirection))>precision*lastLength)
        return fail(QStringLiteral("当前坐标精度无法保留相切圆角，请调整坐标或半径。"));
    QVector<SketchContour> pieces;
    if(polygon) {
        const auto sourceEdges=edges(original);const int incoming=(corner+sourceEdges.size()-1)%sourceEdges.size();
        for(int i=0;i<sourceEdges.size();++i) {
            if(i!=incoming && i!=corner) {
                QString error;const auto intersectionsFound=intersections(arc,sourceEdges[i],&error);
                if(!error.isEmpty())return fail(error);
                for(auto p:intersectionsFound)if(inRange(parameter(arc,p)) && inRange(parameter(sourceEdges[i],p)))
                    return fail(QStringLiteral("圆角与轮廓其他边相交或相切，请减小半径。"));
            }
            auto edge=sourceEdges[i];edge.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            if(i==incoming)edge.points[1]=first;
            if(i==corner)edge.points[0]=last;
            pieces.append(edge);if(i==incoming)pieces.append(arc);
        }
    }
    auto candidate=document;
    if(polygon)candidate=replace(document,target,pieces);
    else {
        auto firstLine=original,secondLine=document.contour(second);
        firstLine.id=QUuid::createUuid().toString(QUuid::WithoutBraces);secondLine.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        firstLine.points[firstEndpoint]=first;secondLine.points[secondEndpoint]=last;
        candidate.setContour(target,firstLine);candidate.setContour(second,secondLine);candidate.additionalContours.append(arc);
    }
    const auto error=candidate.validationError();if(!error.isEmpty())return fail(error);
    return {{},candidate};
}
