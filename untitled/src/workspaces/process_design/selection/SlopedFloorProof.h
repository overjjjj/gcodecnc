#pragma once
#include "../../../import/StepImporter.h"
#include "../../../tool/ToolEntry.h"
#include <cmath>
#ifdef CNEXT_ENABLE_OCC
#include "PocketVolumeProof.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#endif

// A conservative enclosure of one fixed-axis flat-end cutter/shank sweep.
// This is a source-face check, not a stock-removal or complete coverage proof.
inline QString slopedFloorMoveError(const MeshData &mesh, int faceIndex, const Coordinate3D &from,
                                   const Coordinate3D &to, const ToolEntry &tool)
{
    for(double value:{from.x(),from.y(),from.z(),to.x(),to.y(),to.z(),tool.diameter,tool.fluteLen,tool.totalLen,tool.shankDia})
        if(!std::isfinite(value))return QStringLiteral("SLOPED_FLOOR_NONFINITE_INPUT");
    if(tool.type!="end_mill" || !tool.modelPath.isEmpty() || tool.diameter<=0 || tool.fluteLen<=0 ||
       tool.totalLen<tool.fluteLen || tool.shankDia<=0 || tool.shankDia>tool.diameter)
        return QStringLiteral("SLOPED_FLOOR_NOMINAL_FLAT_CUTTER_REQUIRED");
#ifdef CNEXT_ENABLE_OCC
    using namespace PocketVolumeDetail;
    try {
        if(mesh.sourceShape.IsNull())return QStringLiteral("SLOPED_FLOOR_SOURCE_REQUIRED");
        const auto material=BRepBuilderAPI_Copy(mesh.sourceShape).Shape();
        QVector<TopoDS_Solid> solids;
        if(!completeSolids(material,solids))return QStringLiteral("SLOPED_FLOOR_SOLID_REQUIRED");
        TopTools_IndexedMapOfShape faces;TopExp::MapShapes(material,TopAbs_FACE,faces);
        if(faceIndex<1 || faceIndex>faces.Extent())return QStringLiteral("SLOPED_FLOOR_FACE_REQUIRED");
        const auto face=TopoDS::Face(faces(faceIndex));BRepAdaptor_Surface surface(face);
        if(surface.GetType()!=GeomAbs_Plane)return QStringLiteral("SLOPED_FLOOR_PLANE_REQUIRED");
        const auto plane=surface.Plane();auto normal=plane.Axis().Direction();
        if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
        if(normal.Z()<=0)return QStringLiteral("SLOPED_FLOOR_SETUP_DIRECTION_MISMATCH");
        const auto origin=plane.Location();
        const double gx=-normal.X()/normal.Z(),gy=-normal.Y()/normal.Z(),slope=std::hypot(gx,gy);
        if(!std::isfinite(slope))return QStringLiteral("SLOPED_FLOOR_PLANE_INVALID");
        auto z=[&](double x,double y){return origin.Z()+gx*(x-origin.X())+gy*(y-origin.Y());};
        const double r=tool.diameter*.5,dx=to.x()-from.x(),dy=to.y()-from.y(),length=std::hypot(dx,dy);
        const double ux=length>0?dx/length:1,uy=length>0?dy/length:0;
        const double firstOffset=from.z()-z(from.x(),from.y()),lastOffset=to.z()-z(to.x(),to.y());
        const double low=std::min(firstOffset,lastOffset)-r*slope;
        const double height=tool.totalLen+std::abs(firstOffset-lastOffset)+2*r*slope;
        for(double value:{dx,dy,length,ux,uy,firstOffset,lastOffset,low,height})
            if(!std::isfinite(value))return QStringLiteral("SLOPED_FLOOR_SWEEP_NONFINITE");
        BRepBuilderAPI_MakePolygon capPolygon,sweepPolygon;
        // The oriented rectangle encloses the entire XY capsule, including both caps.
        for(const auto &uv:QVector<QPair<double,double>>{{-r,-r},{length+r,-r},{length+r,r},{-r,r}}) {
            const double x=from.x()+uv.first*ux-uv.second*uy,y=from.y()+uv.first*uy+uv.second*ux;
            if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z(x,y)) || !std::isfinite(z(x,y)+low))
                return QStringLiteral("SLOPED_FLOOR_SWEEP_NONFINITE");
            capPolygon.Add(gp_Pnt(x,y,z(x,y)));
            sweepPolygon.Add(gp_Pnt(x,y,z(x,y)+low));
        }
        capPolygon.Close();sweepPolygon.Close();
        if(!capPolygon.IsDone() || !sweepPolygon.IsDone())return QStringLiteral("SLOPED_FLOOR_SWEEP_INVALID");
        BRepBuilderAPI_MakeFace cap(plane,capPolygon.Wire(),true),base(sweepPolygon.Wire(),true);
        if(!cap.IsDone() || !base.IsDone() || !BRepCheck_Analyzer(cap.Face()).IsValid() || !BRepCheck_Analyzer(base.Face()).IsValid())
            return QStringLiteral("SLOPED_FLOOR_SWEEP_INVALID");
        const double scale=std::max({1.,std::abs(from.x()),std::abs(from.y()),std::abs(from.z()),
                                    std::abs(to.x()),std::abs(to.y()),std::abs(to.z()),height});
        const double budget=shapeTolerance(material)+shapeTolerance(cap.Face())+shapeTolerance(base.Face())+
            Precision::Confusion()+128*std::numeric_limits<double>::epsilon()*scale;
        if(!std::isfinite(budget) || budget<=0 || low*normal.Z()<=budget)
            return QStringLiteral("SLOPED_FLOOR_CUTTER_PLANE_CLEARANCE_UNPROVEN");
        bool hasEdges=false;
        for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next()) {
            hasEdges=true;BRepExtrema_DistShapeShape distance(cap.Face(),edge.Current(),Precision::Confusion());
            if(!distance.IsDone() || distance.NbSolution()==0 || distance.InnerSolution() ||
               !std::isfinite(distance.Value()) || distance.Value()<=budget)
                return QStringLiteral("SLOPED_FLOOR_OUTER_OR_HOLE_CLEARANCE_UNPROVEN");
        }
        if(!hasEdges)return QStringLiteral("SLOPED_FLOOR_TRIM_REQUIRED");
        const double mx=(from.x()+to.x())*.5,my=(from.y()+to.y())*.5;
        BRepClass_FaceClassifier inside(face,gp_Pnt(mx,my,z(mx,my)),budget);
        if(inside.State()!=TopAbs_IN)return QStringLiteral("SLOPED_FLOOR_OUTSIDE_TRIMMED_FACE");
        const auto sweep=BRepPrimAPI_MakePrism(base.Face(),gp_Vec(0,0,height)).Shape();
        if(!verifyPocketVolume(sweep,material).separated)
            return QStringLiteral("SLOPED_FLOOR_CUTTER_OR_SHANK_SWEEP_UNPROVEN");
        return {};
    } catch(const Standard_Failure &) {return QStringLiteral("SLOPED_FLOOR_GEOMETRY_FAILED");}
#else
    Q_UNUSED(mesh);Q_UNUSED(faceIndex);
    return QStringLiteral("REGION_EXACT_GEOMETRY_UNAVAILABLE");
#endif
}
