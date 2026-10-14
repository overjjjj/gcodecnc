#include "SketchOffset.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>

namespace {
struct Vector {long double x,y;};
Vector direction(QPointF a,QPointF b)
{
    const long double x=static_cast<long double>(b.x())-a.x(),y=static_cast<long double>(b.y())-a.y();
    const auto length=std::hypot(x,y);return {x/length,y/length};
}
long double area(const QVector<QPointF> &points)
{
    long double result=0;const auto origin=points.first();
    for(int i=0;i<points.size();++i){const auto a=points[i]-origin,b=points[(i+1)%points.size()]-origin;
        result+=static_cast<long double>(a.x())*b.y()-static_cast<long double>(a.y())*b.x();}
    return result/2;
}
TopoDS_Face makeFace(QVector<QPointF> points)
{
    if(area(points)<0)std::reverse(points.begin(),points.end());
    BRepBuilderAPI_MakePolygon polygon;for(auto p:points)polygon.Add(gp_Pnt(p.x(),p.y(),0));polygon.Close();
    if(!polygon.IsDone())return {};
    BRepBuilderAPI_MakeFace face(polygon.Wire(),true);
    return face.IsDone()?face.Face():TopoDS_Face();
}
QPointF translated(QPointF p,Vector vector,long double distance)
{
    return {double(p.x()+vector.x*distance),double(p.y()+vector.y*distance)};
}
// The ordinary offset builder can fail when an inward neck splits. Boolean
// edge strips and miter joins resolve that topology without sampling curves.
TopoDS_Shape splitOffset(const QVector<QPointF> &points,const TopoDS_Face &source,double distance)
{
    TopTools_ListOfShape arguments,tools;arguments.Append(source);QVector<Vector> directions;
    const auto add=[&](const QVector<QPointF> &vertices) {
        const auto face=makeFace(vertices);
        if(face.IsNull() || !BRepCheck_Analyzer(face).IsValid())return false;
        tools.Append(face);return true;
    };
    for(int i=0;i<points.size();++i) {
        const auto a=points[i],b=points[(i+1)%points.size()];const auto u=direction(a,b);directions.append(u);
        const Vector n{-u.y,u.x};
        if(!add({a,b,translated(b,n,distance),translated(a,n,distance)}))return {};
    }
    for(int i=0;i<points.size();++i) {
        const auto a=directions[(i+points.size()-1)%points.size()],b=directions[i];
        const auto determinant=a.x*b.y-a.y*b.x;
        if(determinant==0)continue;
        const Vector n1{-a.y,a.x},n2{-b.y,b.x};const auto v=points[i];
        const auto p=translated(v,n1,distance),q=translated(v,n2,distance);
        const long double t=distance*((n2.x-n1.x)*b.y-(n2.y-n1.y)*b.x)/determinant;
        const auto join=translated(p,a,t);
        if(!std::isfinite(join.x()) || !std::isfinite(join.y()) || !add({v,p,join,q}))return {};
    }
    if(distance>0) {
        BRepAlgoAPI_Cut cut;cut.SetArguments(arguments);cut.SetTools(tools);cut.SetNonDestructive(true);cut.Build();
        if(!cut.IsDone())return {};
        cut.SimplifyResult(true,true);return cut.Shape();
    }
    BRepAlgoAPI_Fuse fuse;fuse.SetArguments(arguments);fuse.SetTools(tools);fuse.SetNonDestructive(true);fuse.Build();
    if(!fuse.IsDone())return {};
    fuse.SimplifyResult(true,true);return fuse.Shape();
}
bool offsetEdgesMatch(const QVector<QPointF> &source,const QVector<QPointF> &candidate,double distance)
{
    long double scale=std::max(1.0,std::abs(distance));
    for(auto p:source)scale=std::max({scale,std::abs(static_cast<long double>(p.x())),std::abs(static_cast<long double>(p.y()))});
    for(auto p:candidate)scale=std::max({scale,std::abs(static_cast<long double>(p.x())),std::abs(static_cast<long double>(p.y()))});
    const auto tolerance=std::min(256*std::numeric_limits<double>::epsilon()*scale,
                                 static_cast<long double>(std::abs(distance))*1e-8L);
    for(int i=0;i<candidate.size();++i) {
        bool matched=false;
        for(int j=0;j<source.size() && !matched;++j) {
            const auto u=direction(source[j],source[(j+1)%source.size()]);bool both=true;
            for(auto p:{candidate[i],candidate[(i+1)%candidate.size()]}) {
                const auto x=static_cast<long double>(p.x())-source[j].x(),y=static_cast<long double>(p.y())-source[j].y();
                both &= std::abs(u.x*y-u.y*x-distance)<=tolerance;
            }
            matched=both;
        }
        if(!matched)return false;
    }
    return true;
}
}

SketchOffsetResult sketchOffset(const SketchDocument &document,int target,double distance,const QString &side)
{
    const auto fail=[&](const QString &message){return SketchOffsetResult{message,document};};
    if(!document.validationError().isEmpty() || target<0 || target>=document.contourCount())return fail(QStringLiteral("请选择有效的偏移目标。"));
    if(!std::isfinite(distance) || distance<=0)return fail(QStringLiteral("偏移距离必须为有限正数（mm）。"));
    const auto original=document.contour(target);QVector<SketchContour> copies;
    if(original.kind=="line") {
        if(side!="left" && side!="right")return fail(QStringLiteral("直线请选择沿首点到末点的左侧或右侧。"));
        const auto u=direction(original.points[0],original.points[1]);const Vector n{-u.y,u.x};
        const double signedDistance=side=="left"?distance:-distance;
        auto c=original;for(auto &p:c.points)p=translated(p,n,signedDistance);
        const auto tolerance=256*std::numeric_limits<double>::epsilon()*distance;
        for(int i=0;i<c.points.size();++i) {
            const long double dx=static_cast<long double>(c.points[i].x())-original.points[i].x();
            const long double dy=static_cast<long double>(c.points[i].y())-original.points[i].y();
            if(std::abs(dx-n.x*signedDistance)>tolerance || std::abs(dy-n.y*signedDistance)>tolerance)
                return fail(QStringLiteral("当前坐标精度无法保留该偏移量，请调整距离或坐标。"));
        }
        copies.append(c);
    } else {
        if(side!="inside" && side!="outside")return fail(QStringLiteral("请选择向内或向外偏移。"));
        if(original.isCurve()) {
            auto c=original;c.radius=double(static_cast<long double>(c.radius)+(side=="inside"?-distance:distance));
            if(!std::isfinite(c.radius) || c.radius<=0 || c.radius==original.radius)return fail(QStringLiteral("偏移后半径消失或不可区分，请减小内偏距离或调整数值。"));
            const long double actual=static_cast<long double>(c.radius)-original.radius;
            if(std::abs(actual-(side=="inside"?-distance:distance))>distance*1e-8L)
                return fail(QStringLiteral("当前半径精度无法保留该偏移量，请调整距离或半径。"));
            copies.append(c);
        } else try {
            const auto origin=original.points.first();QVector<QPointF> local;
            for(auto p:original.points)local.append({double(static_cast<long double>(p.x())-origin.x()),double(static_cast<long double>(p.y())-origin.y())});
            const bool reversed=area(local)<0;if(reversed)std::reverse(local.begin(),local.end());
            const auto source=makeFace(local);if(source.IsNull() || !BRepCheck_Analyzer(source).IsValid())return fail(QStringLiteral("当前轮廓无法建立有效偏移平面。"));
            const double signedDistance=side=="inside"?distance:-distance;
            BRepOffsetAPI_MakeOffset offset(source,GeomAbs_Intersection);offset.Perform(-signedDistance);
            const auto shape=offset.IsDone()?offset.Shape():splitOffset(local,source,signedDistance);
            if(shape.IsNull() || !BRepCheck_Analyzer(shape).IsValid())return fail(QStringLiteral("偏移拓扑无法解析，请调整距离或轮廓。"));
            for(TopExp_Explorer it(shape,TopAbs_WIRE);it.More();it.Next()) {
                QVector<QPointF> points;
                for(BRepTools_WireExplorer edge(TopoDS::Wire(it.Current()));edge.More();edge.Next()) {
                    BRepAdaptor_Curve curve(edge.Current());if(curve.GetType()!=GeomAbs_Line)return fail(QStringLiteral("尖角偏移产生了非直线结果，未修改草图。"));
                    const auto p=curve.Value(edge.Current().Orientation()==TopAbs_REVERSED?curve.LastParameter():curve.FirstParameter());points.append({p.X(),p.Y()});
                }
                if(points.size()<3 || !offsetEdgesMatch(local,points,signedDistance))return fail(QStringLiteral("结果未能证明每条边的解析偏移距离，未修改草图。"));
                if(reversed)std::reverse(points.begin(),points.end());
                for(auto &p:points)p={double(static_cast<long double>(origin.x())+p.x()),double(static_cast<long double>(origin.y())+p.y())};
                QVector<QPointF> storedLocal;
                for(auto p:points)storedLocal.append({double(static_cast<long double>(p.x())-origin.x()),double(static_cast<long double>(p.y())-origin.y())});
                if(!offsetEdgesMatch(local,storedLocal,signedDistance))return fail(QStringLiteral("当前坐标精度无法保留该偏移量，请调整距离或坐标。"));
                SketchContour c;c.kind="polyline";c.points=points;
                if(original.kind=="rectangle" && points.size()==4) {
                    bool axis=true;for(int i=0;i<4;++i){const auto a=points[i],b=points[(i+1)%4];axis &= (a.x()==b.x())!=(a.y()==b.y());}
                    if(axis)c.kind="rectangle";
                }
                copies.append(c);
            }
            if(copies.isEmpty())return fail(QStringLiteral("偏移后轮廓已消失，请减小内偏距离。"));
        } catch(const Standard_Failure &) {return fail(QStringLiteral("几何内核无法完成该偏移，请调整距离或轮廓。"));}
    }
    auto candidate=document;
    for(auto &copy:copies){copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces);candidate.additionalContours.append(copy);}
    const auto error=candidate.validationError();if(!error.isEmpty())return fail(error);
    return {{},candidate};
}
