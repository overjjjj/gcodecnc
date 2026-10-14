#pragma once
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <QVector>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <Standard_Failure.hxx>
#include <cmath>
#include <BRep_TFace.hxx>
#include <BRep_TEdge.hxx>
#include <BRep_TVertex.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass3d_SolidExplorer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Pnt2d.hxx>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <limits>

struct PocketVolumeProof {
    bool separated=false;
    double boundaryBudget=0;
    double maximumEscapeBudget=0;
};

namespace PocketVolumeDetail {
// Only trusted shared edges connect faces; identity includes accumulated Location.
inline QVector<QVector<TopoDS_Face>> boundaryGroups(const TopoDS_Shape &shape) {
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape,TopAbs_FACE,faces);
    QVector<int> parents(faces.Extent());
    for(int i=0;i<parents.size();++i)parents[i]=i;
    auto root=[&](int i){while(parents[i]!=i)i=parents[i];return i;};
    TopTools_IndexedDataMapOfShapeListOfShape edges;
    TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,edges);
    for(int i=1;i<=edges.Extent();++i) {
        try {
            const auto edge=TopoDS::Edge(edges.FindKey(i));
            const double tolerance=BRep_Tool::Tolerance(edge);
            if(!std::isfinite(tolerance) || tolerance<0 || BRep_Tool::Degenerated(edge) ||
               !BRep_Tool::SameParameter(edge) || !BRep_Tool::SameRange(edge))continue;
            double first,last;TopLoc_Location location;
            if(BRep_Tool::Curve(edge,location,first,last).IsNull() ||
               !std::isfinite(first) || !std::isfinite(last) || first>=last)continue;
            int previous=-1;
            for(TopTools_ListIteratorOfListOfShape it(edges.FindFromIndex(i));it.More();it.Next()) {
                const auto face=TopoDS::Face(it.Value());
                const double faceTolerance=BRep_Tool::Tolerance(face);
                double pfirst,plast;
                if(!std::isfinite(faceTolerance) || faceTolerance<0 ||
                   BRep_Tool::CurveOnSurface(edge,face,pfirst,plast).IsNull() ||
                   pfirst!=first || plast!=last)continue;
                const int index=faces.FindIndex(face)-1;
                if(index<0)continue;
                if(previous>=0)parents[root(index)]=root(previous);
                previous=index;
            }
        } catch(const Standard_Failure &) { /* Unproved adjacency stays separate. */ }
    }
    QVector<QVector<TopoDS_Face>> groups;
    QVector<int> groupIndex(faces.Extent(),-1);
    for(int i=0;i<parents.size();++i) {
        const int component=root(i);
        if(groupIndex[component]<0){groupIndex[component]=groups.size();groups.append(QVector<TopoDS_Face>{});}
        groups[groupIndex[component]].append(TopoDS::Face(faces(i+1)));
    }
    return groups;
}

inline bool completeSolids(const TopoDS_Shape &shape,QVector<TopoDS_Solid> &solids) {
    if(shape.IsNull())return false;
    if(shape.ShapeType()==TopAbs_SOLID) {
        if(!BRepCheck_Analyzer(shape).IsValid())return false;
        bool shell=false;
        for(TopExp_Explorer it(shape,TopAbs_SHELL);it.More();it.Next()) {
            shell=true;if(!BRep_Tool::IsClosed(it.Current()))return false;
        }
        BRepClass3d_SolidClassifier classifier(shape);classifier.PerformInfinitePoint(Precision::Confusion());
        if(!shell || classifier.State()!=TopAbs_OUT)return false;
        solids.append(TopoDS::Solid(shape));return true;
    }
    if(shape.ShapeType()!=TopAbs_COMPOUND && shape.ShapeType()!=TopAbs_COMPSOLID)return false;
    bool child=false;
    for(TopoDS_Iterator it(shape);it.More();it.Next()){child=true;if(!completeSolids(it.Value(),solids))return false;}
    return child;
}
inline double shapeTolerance(const TopoDS_Shape &shape) {
    double value=0;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()){const double raw=Handle(BRep_TFace)::DownCast(it.Current().TShape())->Tolerance();if(!std::isfinite(raw) || raw<0)return std::numeric_limits<double>::quiet_NaN();const double t=BRep_Tool::Tolerance(TopoDS::Face(it.Current()));if(!std::isfinite(t) || t<0)return std::numeric_limits<double>::quiet_NaN();value=std::max(value,t);}
    for(TopExp_Explorer it(shape,TopAbs_EDGE);it.More();it.Next()){const double raw=Handle(BRep_TEdge)::DownCast(it.Current().TShape())->Tolerance();if(!std::isfinite(raw) || raw<0)return std::numeric_limits<double>::quiet_NaN();const double t=BRep_Tool::Tolerance(TopoDS::Edge(it.Current()));if(!std::isfinite(t) || t<0)return std::numeric_limits<double>::quiet_NaN();value=std::max(value,t);}
    for(TopExp_Explorer it(shape,TopAbs_VERTEX);it.More();it.Next()){const double raw=Handle(BRep_TVertex)::DownCast(it.Current().TShape())->Tolerance();if(!std::isfinite(raw) || raw<0)return std::numeric_limits<double>::quiet_NaN();const double t=BRep_Tool::Tolerance(TopoDS::Vertex(it.Current()));if(!std::isfinite(t) || t<0)return std::numeric_limits<double>::quiet_NaN();value=std::max(value,t);}
    return value;
}
inline TopoDS_Compound boundary(const TopoDS_Shape &shape) {
    BRep_Builder builder;TopoDS_Compound result;builder.MakeCompound(result);
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())builder.Add(result,it.Current());
    return result;
}
inline bool escapePath(const gp_Pnt &point,const Bnd_Box &bounds,const TopoDS_Shape &targetBoundary,
                       double budget,double witnessDisplacement,double &maximumBudget) {
    if(!std::isfinite(point.X()) || !std::isfinite(point.Y()) || !std::isfinite(point.Z()) ||
       !std::isfinite(budget) || budget<=0 || !std::isfinite(witnessDisplacement) || witnessDisplacement<0)return false;
    if(bounds.IsVoid() || bounds.IsOpen())return false;
    double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
    for(double value:{x0,y0,z0,x1,y1,z1})if(!std::isfinite(value))return false;
    const double scale=std::max({1.,std::abs(x0),std::abs(y0),std::abs(z0),std::abs(x1),std::abs(y1),std::abs(z1),std::abs(point.X()),std::abs(point.Y()),std::abs(point.Z())});
    const double baseError=budget+128*std::numeric_limits<double>::epsilon()*scale+witnessDisplacement+Precision::Confusion();
    if(!std::isfinite(baseError))return false;
    // An outward ray from a point strictly outside the complete conservative
    // bounds is already separated from every target boundary point.
    double outsideGap=0;
    for(double gap:{x0-point.X(),point.X()-x1,y0-point.Y(),point.Y()-y1,z0-point.Z(),point.Z()-z1}) {
        if(!std::isfinite(gap))return false;
        outsideGap=std::max(outsideGap,gap);
    }
    if(outsideGap>baseError){maximumBudget=std::max(maximumBudget,baseError);return true;}
    const double margin=std::max(1.,16*baseError);
    if(!std::isfinite(margin))return false;
    for(int direction=0;direction<6;++direction) {
        gp_Pnt end=point;
        if(direction==0)end.SetZ(z1+margin);
        if(direction==1)end.SetZ(z0-margin);
        if(direction==2)end.SetX(x1+margin);
        if(direction==3)end.SetX(x0-margin);
        if(direction==4)end.SetY(y1+margin);
        if(direction==5)end.SetY(y0-margin);
        if(!std::isfinite(end.X()) || !std::isfinite(end.Y()) || !std::isfinite(end.Z()) || point.Distance(end)<=baseError)continue;
        const double outside=direction==0?end.Z()-z1:direction==1?z0-end.Z():direction==2?end.X()-x1:direction==3?x0-end.X():direction==4?end.Y()-y1:y0-end.Y();
        BRepBuilderAPI_MakeEdge edge(point,end);if(!edge.IsDone())continue;
        const double error=baseError+shapeTolerance(edge.Edge());
        if(!std::isfinite(error) || !std::isfinite(outside) || outside<=error)continue;
        BRepExtrema_DistShapeShape distance(edge.Edge(),targetBoundary,Precision::Confusion());
        if(distance.IsDone() && distance.NbSolution()>0 && !distance.InnerSolution() &&
           std::isfinite(distance.Value()) && distance.Value()>error) {
            maximumBudget=std::max(maximumBudget,error);return true;
        }
    }
    return false;
}
inline bool faceOutside(const TopoDS_Face &face,const Bnd_Box &targetBounds,
                        const TopoDS_Shape &targetBoundary,double budget,double &maximumBudget) {
    gp_Pnt point;double u=0,v=0;
        BRepAdaptor_Surface surface(face,true);
        const double uvTolerance=std::max(surface.UResolution(budget),surface.VResolution(budget));
        if(!std::isfinite(uvTolerance) || uvTolerance<=0)return false;
        bool ownPoint=false;double witnessDisplacement=0;
        for(int attempt=0;attempt<4;++attempt) {
            double parameter=attempt==1?.5:attempt==2?.25:.75;
            const bool found=attempt==0?BRepClass3d_SolidExplorer::FindAPointInTheFace(face,point,u,v):BRepClass3d_SolidExplorer::FindAPointInTheFace(face,point,u,v,parameter);
            if(!found || !std::isfinite(u) || !std::isfinite(v) || !std::isfinite(point.X()) || !std::isfinite(point.Y()) || !std::isfinite(point.Z()))continue;
            const double displacement=point.Distance(surface.Value(u,v));
            if(!std::isfinite(displacement) || displacement>budget)continue;
            BRepClass_FaceClassifier own(face,gp_Pnt2d(u,v),uvTolerance);
            if(own.State()==TopAbs_IN){ownPoint=true;witnessDisplacement=displacement;break;}
        }
        if(!ownPoint) {
            u=surface.FirstUParameter()*.5+surface.LastUParameter()*.5;
            v=surface.FirstVParameter()*.5+surface.LastVParameter()*.5;
            if(std::isfinite(u) && std::isfinite(v)) {
                BRepClass_FaceClassifier own(face,gp_Pnt2d(u,v),uvTolerance);
                if(own.State()==TopAbs_IN) {
                    point=surface.Value(u,v);
                    ownPoint=std::isfinite(point.X()) && std::isfinite(point.Y()) && std::isfinite(point.Z());
                }
            }
        }

    return ownPoint && escapePath(point,targetBounds,targetBoundary,budget,witnessDisplacement,maximumBudget);
}
inline bool allComponentsOutside(const TopoDS_Shape &source,const Bnd_Box &targetBounds,
                            const TopoDS_Shape &targetBoundary,double budget,double &maximumBudget) {
    const auto groups=boundaryGroups(source);
    for(const auto &group:groups) {
        bool outside=false;
        for(const auto &face:group) {
            if(faceOutside(face,targetBounds,targetBoundary,budget,maximumBudget)){outside=true;break;}
        }
        if(!outside)return false;
    }
    return !groups.isEmpty();
}
} // namespace PocketVolumeDetail

// Full boundary separation plus a verified path to known exterior for every verified boundary component
// excludes both intersection and containment. This is an OCC numeric contract,
// not an interval-arithmetic certificate; no target point classifier grants OUT.
inline PocketVolumeProof verifyPocketVolume(const TopoDS_Shape &prism,const TopoDS_Shape &material) {
    using namespace PocketVolumeDetail;
    PocketVolumeProof proof;
    try {
        QVector<TopoDS_Solid> volumes,solids;
        if(!completeSolids(prism,volumes) || volumes.size()!=1 || !completeSolids(material,solids))return {};
        Bnd_Box bounds;BRepBndLib::Add(prism,bounds,false);BRepBndLib::Add(material,bounds,false);
        if(bounds.IsVoid() || bounds.IsOpen())return {};
        double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
        for(double value:{x0,y0,z0,x1,y1,z1})if(!std::isfinite(value))return {};
        const double scale=std::max({1.,std::abs(x0),std::abs(y0),std::abs(z0),std::abs(x1),std::abs(y1),std::abs(z1)});
        proof.boundaryBudget=shapeTolerance(prism)+shapeTolerance(material)+Precision::Confusion()+128*std::numeric_limits<double>::epsilon()*scale;
        if(!std::isfinite(proof.boundaryBudget))return {};
        Bnd_Box prismBounds;BRepBndLib::Add(prism,prismBounds,false);
        const auto prismBoundary=boundary(prism);
        for(const auto &solid:solids) {
            const auto materialBoundary=boundary(solid);
            Bnd_Box materialBounds;BRepBndLib::Add(solid,materialBounds,false);
            BRepExtrema_DistShapeShape distance(prismBoundary,materialBoundary,Precision::Confusion());
            if(!distance.IsDone() || distance.NbSolution()==0 || distance.InnerSolution() ||
               !std::isfinite(distance.Value()) || distance.Value()<=proof.boundaryBudget)return {};
            if(!allComponentsOutside(prism,materialBounds,materialBoundary,proof.boundaryBudget,proof.maximumEscapeBudget) ||
               !allComponentsOutside(solid,prismBounds,prismBoundary,proof.boundaryBudget,proof.maximumEscapeBudget))return {};
        }
        proof.separated=true;return proof;
    } catch(const Standard_Failure &) {return {};}
}
