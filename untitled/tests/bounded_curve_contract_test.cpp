#include "../src/workspaces/process_design/selection/BoundedCurveDiscretization.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_OffsetCurve.hxx>
#include <GeomConvert.hxx>
#include <Geom_BSplineCurve.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <GCPnts_UniformDeflection.hxx>
#include <TopExp.hxx>
#include "../src/import/StepImporter.h"
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <iostream>
#include <limits>
#include <cmath>
#include <iomanip>

static int failures=0;
static void check(bool value,const char *message) {
    if(!value){++failures;std::cerr<<"FAIL "<<message<<std::endl;}
}
static double segmentDistance(const gp_Pnt &p,const QVector3D &a,const QVector3D &b) {
    const gp_Pnt start(a.x(),a.y(),a.z()),end(b.x(),b.y(),b.z());
    const gp_Vec d(start,end),v(start,p);const double square=d.SquareMagnitude();
    const double t=square?std::max(0.,std::min(1.,v.Dot(d)/square)):0.;
    return p.Distance(start.Translated(d*t));
}
static BoundedCurveResult verify(const TopoDS_Edge &edge,double first,double last,const char *name) {
    BRepAdaptor_Curve curve(edge);
    const auto result=discretizeCurve(curve,first,last,.001);
    std::cout<<"CASE "<<name<<" points="<<result.points.size()<<" error="<<result.error.toStdString()
             <<" bound="<<result.computedBound<<std::endl;
    check(result.error.isEmpty() && result.points.size()>=2,name);
    if(!result.error.isEmpty() || result.points.size()<2)return result;
    check(result.computedBound<=.001,"reported budget");
    check(segmentDistance(curve.Value(first),result.points.first(),result.points.first())<=.001,"first endpoint");
    check(segmentDistance(curve.Value(last),result.points.last(),result.points.last())<=.001,"last endpoint");
    // Regression sampling detects mistakes; it is not the algorithm's bound proof.
    for(int i=0;i<=2000;++i) {
        const auto p=curve.Value(first+(last-first)*i/2000.);
        double closest=std::numeric_limits<double>::infinity();
        for(int j=1;j<result.points.size();++j)closest=std::min(closest,segmentDistance(p,result.points[j-1],result.points[j]));
        if(closest>.001){std::cerr<<"WITNESS "<<name<<" parameter="<<first+(last-first)*i/2000.<<" distance="<<closest<<std::endl;check(false,"dense regression witness exceeds budget");break;}
    }
    return result;
}
static Handle(Geom_BezierCurve) bezier(std::initializer_list<gp_Pnt> points) {
    TColgp_Array1OfPnt poles(1,int(points.size()));int i=1;for(const auto &p:points)poles.SetValue(i++,p);
    return new Geom_BezierCurve(poles);
}
int main(int argc,char **argv) {
    auto line=BRepBuilderAPI_MakeEdge(gp_Pnt(1,2,3),gp_Pnt(8,2,3)).Edge();
    gp_Trsf location;location.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),.7);
    location.SetTranslationPart(gp_Vec(13,-21,8));line.Location(TopLoc_Location(location));
    BRepAdaptor_Curve lc(line);verify(line,lc.FirstParameter(),lc.LastParameter(),"located line");
    verify(line,lc.LastParameter(),lc.FirstParameter(),"reverse traversal");
    auto s=bezier({{0,0,0},{1,10,0},{2,-10,0},{3,0,0}});
    auto sr=verify(BRepBuilderAPI_MakeEdge(s).Edge(),0,1,"S curve");check(sr.points.size()>8,"S midpoint cancellation must subdivide");
    auto overshoot=bezier({{0,0,0},{10,0,0},{1,0,0}});
    auto over=verify(BRepBuilderAPI_MakeEdge(overshoot).Edge(),0,1,"collinear overshoot");
    bool beyond=false;for(const auto &p:over.points)beyond|=p.x()>4;check(beyond,"finite chord retains excursion");
    auto loop=bezier({{0,0,0},{10,0,0},{0,10,0},{0,0,0}});
    verify(BRepBuilderAPI_MakeEdge(loop).Edge(),0,1,"closed endpoint loop");
    Handle(Geom_Circle) circle=new Geom_Circle(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,1)),4);
    verify(BRepBuilderAPI_MakeEdge(circle,0,2*M_PI).Edge(),0,2*M_PI,"full rational circle");
    verify(BRepBuilderAPI_MakeEdge(circle,5.5,7).Edge(),5.5,7,"circle seam");
    Handle(Geom_Ellipse) ellipse=new Geom_Ellipse(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,1)),9,2);
    verify(BRepBuilderAPI_MakeEdge(ellipse,5.5,7).Edge(),5.5,7,"ellipse seam");
    verify(BRepBuilderAPI_MakeEdge(ellipse,0,2*M_PI).Edge(),0,2*M_PI,"full rational ellipse");
    auto spline=GeomConvert::CurveToBSplineCurve(s);
    verify(BRepBuilderAPI_MakeEdge(spline).Edge(),.13,.87,"trimmed spline");
    TColgp_Array1OfPnt poles(1,7);
    for(int i=1;i<=7;++i)poles.SetValue(i,gp_Pnt(i-1,(i%3)-1,0));
    TColStd_Array1OfReal knots(1,3);knots.SetValue(1,0);knots.SetValue(2,.5);knots.SetValue(3,1);
    TColStd_Array1OfInteger multiplicities(1,3);multiplicities.SetValue(1,4);multiplicities.SetValue(2,3);multiplicities.SetValue(3,4);
    Handle(Geom_BSplineCurve) joined=new Geom_BSplineCurve(poles,knots,multiplicities,3);
    auto joinedEdge=BRepBuilderAPI_MakeEdge(joined).Edge();joinedEdge.Location(TopLoc_Location(location));
    auto joinedResult=verify(joinedEdge,.13,.87,"located C0 multi-span non-knot trim");
    check(joinedResult.continuityIntervals>1,"multiple Bezier spans covered");
    verify(joinedEdge,.87,.13,"located C0 reversed trim");
    auto reversedEdge=TopoDS::Edge(joinedEdge.Reversed());
    const bool backward=reversedEdge.Orientation()==TopAbs_REVERSED;
    auto reversedResult=verify(reversedEdge,backward?.87:.13,backward?.13:.87,"actual reversed edge caller traversal");
    auto expectedReverse=joinedResult.points;std::reverse(expectedReverse.begin(),expectedReverse.end());
    check(reversedResult.points==expectedReverse,"edge orientation applied exactly once by caller");
    TColgp_Array1OfPnt tightPoles(1,10);for(int i=1;i<=10;++i)tightPoles.SetValue(i,gp_Pnt(i,std::sin(i)*3,0));
    TColStd_Array1OfReal tightKnots(1,4);tightKnots.SetValue(1,0);tightKnots.SetValue(2,.5);tightKnots.SetValue(3,.500000001);tightKnots.SetValue(4,1);
    TColStd_Array1OfInteger tightMults(1,4);tightMults.SetValue(1,4);tightMults.SetValue(2,3);tightMults.SetValue(3,3);tightMults.SetValue(4,4);
    Handle(Geom_BSplineCurve) tight=new Geom_BSplineCurve(tightPoles,tightKnots,tightMults,3);
    verify(BRepBuilderAPI_MakeEdge(tight).Edge(),.50000000001,.50000000099,"near-knot high-derivative trim");
    TColgp_Array1OfPnt rationalPoles(1,4);rationalPoles.SetValue(1,gp_Pnt(0,0,0));rationalPoles.SetValue(2,gp_Pnt(3,12,0));rationalPoles.SetValue(3,gp_Pnt(8,-7,0));rationalPoles.SetValue(4,gp_Pnt(11,0,0));
    TColStd_Array1OfReal weights(1,4);weights.SetValue(1,.1);weights.SetValue(2,20);weights.SetValue(3,.5);weights.SetValue(4,3);
    Handle(Geom_BezierCurve) rational=new Geom_BezierCurve(rationalPoles,weights);
    verify(BRepBuilderAPI_MakeEdge(rational).Edge(),0,1,"nonuniform positive rational weights");
    auto periodic=GeomConvert::CurveToBSplineCurve(circle);periodic->SetPeriodic();
    BRepAdaptor_Curve periodicCurve(BRepBuilderAPI_MakeEdge(periodic).Edge());
    check(!discretizeCurve(periodicCurve,periodicCurve.FirstParameter(),periodicCurve.LastParameter(),.001).error.isEmpty(),"periodic spline rejected");
    BRepAdaptor_Curve sc(BRepBuilderAPI_MakeEdge(s).Edge());
    for(double invalid:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        check(!discretizeCurve(sc,0,1,invalid).error.isEmpty(),"invalid budget rejected");
    check(!discretizeCurve(sc,0,0,.001).error.isEmpty(),"empty interval rejected");
    check(!discretizeCurve(sc,-1,1,.001).error.isEmpty(),"outside interval rejected");
    check(!discretizeCurve(sc,0,1,1e-12).error.isEmpty(),"unresolvable budget rejected");
    auto rejected=discretizeCurve(sc,0,1,1e-12);
    check(rejected.points.isEmpty() && rejected.computedBound==0,"rejection never exposes partial result");
    check(!discretizeCurve(sc,0,std::numeric_limits<double>::infinity(),.001).error.isEmpty(),"infinite interval rejected");
    auto large=bezier({{100000000.1,0,0},{100000010.1,0,0}});
    BRepAdaptor_Curve largeCurve(BRepBuilderAPI_MakeEdge(large).Edge());
    check(!discretizeCurve(largeCurve,0,1,.001).error.isEmpty(),"float loss rejected");
    Handle(Geom_OffsetCurve) offset=new Geom_OffsetCurve(s,1,gp_Dir(0,0,1));
    BRepAdaptor_Curve oc(BRepBuilderAPI_MakeEdge(offset).Edge());
    check(!discretizeCurve(oc,0,1,.001).error.isEmpty(),"offset curve rejected");
    // Diagnose the rejected kernel candidate separately; not used by the adapter.
    BRepAdaptor_Curve cc(BRepBuilderAPI_MakeEdge(circle,0,2*M_PI).Edge());
    GCPnts_UniformDeflection circleDiagnostic(cc,.0005,0,2*M_PI,true);
    std::cout<<std::setprecision(17)<<"CONTROLLED_CIRCLE_DIAGNOSTIC first="<<circleDiagnostic.Parameter(1)
        <<" last="<<circleDiagnostic.Parameter(circleDiagnostic.NbPoints())<<" requested="<<2*M_PI<<std::endl;
    if(argc>1) {
        StepImporter importer;check(importer.load(QString::fromLocal8Bit(argv[1])),"real STEP loads");
        TopTools_IndexedMapOfShape edges;TopExp::MapShapes(importer.mesh().sourceShape,TopAbs_EDGE,edges);
        for(int id:{462,470}) {
            check(edges.Extent()>=id,"real edge exists");if(edges.Extent()<id)continue;
            auto edge=TopoDS::Edge(edges(id));BRepAdaptor_Curve curve(edge);
            check(curve.GetType()==GeomAbs_BSplineCurve && curve.Degree()==3 && curve.NbPoles()==12,"real spline identity");
            verify(edge,curve.FirstParameter(),curve.LastParameter(),"real 12-pole full spline");
            edge.Location(TopLoc_Location(location));
            verify(edge,.17,.83,"real located non-knot trim");
            verify(edge,.83,.17,"real located reverse trim");
        }
    }
    std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
