#pragma once
#include "SelectionChainController.h"
#include "../../../import/StepImporter.h"
#include "SlopedFloorProof.h"
#ifdef CNEXT_ENABLE_OCC
#include "BoundedCurveDiscretization.h"
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#endif

struct SlopedFloorBinding {
    MachiningFeature feature;
    int faceIndex=0;
    QString error;
    QVector<QVector3D> sourceOuterBoundary;
    QStringList overhangingWallIds;
    double sourceTopZ=0;
    bool curvedWallCandidate=false;
};

inline SlopedFloorBinding bindSlopedFloor(const MeshData &mesh, const SelectionChain &selection)
{
    auto fail=[](const char *error){SlopedFloorBinding result;result.error=QString::fromLatin1(error);return result;};
    if(!selection.explicitUserSelection || selection.orderedTopologyRefs.size()!=1 ||
       selection.orderedTopologyRefs.first().kind!=SelectionTopologyKind::Face || selection.toolAxis!=QVector3D(0,0,1))
        return fail("SLOPED_FLOOR_SINGLE_FACE_FIXED_Z_REQUIRED");
    const QString id=selection.orderedTopologyRefs.first().id;
    const int index=id.section(':',1).toInt();
    if(id!=QStringLiteral("face:%1").arg(index))return fail("SLOPED_FLOOR_FACE_REQUIRED");
#ifdef CNEXT_ENABLE_OCC
    try {
        if(mesh.sourceShape.IsNull())return fail("SLOPED_FLOOR_SOURCE_REQUIRED");
        const auto material=BRepBuilderAPI_Copy(mesh.sourceShape).Shape();
        QVector<TopoDS_Solid> solids;
        if(!PocketVolumeDetail::completeSolids(material,solids))return fail("SLOPED_FLOOR_SOLID_REQUIRED");
        TopTools_IndexedMapOfShape faces,edges,vertices;
        TopExp::MapShapes(material,TopAbs_FACE,faces);TopExp::MapShapes(material,TopAbs_EDGE,edges);TopExp::MapShapes(material,TopAbs_VERTEX,vertices);
        if(index<1 || index>faces.Extent())return fail("SLOPED_FLOOR_FACE_REQUIRED");
        const auto face=TopoDS::Face(faces(index));BRepAdaptor_Surface surface(face);
        if(surface.GetType()!=GeomAbs_Plane || !BRepCheck_Analyzer(face).IsValid())return fail("SLOPED_FLOOR_PLANE_REQUIRED");
        const auto plane=surface.Plane();auto normal=plane.Axis().Direction();
        if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
        if(normal.Z()<=0)return fail("SLOPED_FLOOR_SETUP_DIRECTION_MISMATCH");
        SlopedFloorBinding result;result.faceIndex=index;
        Bnd_Box sourceBounds;BRepBndLib::Add(material,sourceBounds,false);
        if(sourceBounds.IsVoid() || sourceBounds.IsOpen())return fail("SLOPED_FLOOR_FINITE_SOURCE_REQUIRED");
        double x0,y0,z0,x1,y1,z1;sourceBounds.Get(x0,y0,z0,x1,y1,z1);
        if(!std::isfinite(z1))return fail("SLOPED_FLOOR_FINITE_SOURCE_REQUIRED");
        result.sourceTopZ=z1+PocketVolumeDetail::shapeTolerance(material);
        auto &feature=result.feature;feature.kind=FeatureKind::FlatSurface;feature.subType="verified_sloped_floor";
        feature.axis={float(normal.X()),float(normal.Y()),float(normal.Z())};feature.region=FaceRegion::Front;
        const auto outer=BRepTools::OuterWire(face);QSet<int> matched;
        for(TopExp_Explorer wires(face,TopAbs_WIRE);wires.More();wires.Next()) {
            const auto wire=TopoDS::Wire(wires.Current());const bool isOuter=wire.IsSame(outer);
            QStringList ids;QVector<bool> forward;QVector<QVector3D> points;
            for(BRepTools_WireExplorer it(wire,face);it.More();it.Next()) {
                const auto edge=it.Current();const int edgeIndex=edges.FindIndex(edge);
                if(edgeIndex<1)return fail("SLOPED_FLOOR_EDGE_INVALID");
                ids.append(QStringLiteral("edge:%1").arg(edgeIndex));
                forward.append(vertices.FindIndex(TopExp::FirstVertex(edge,true))==vertices.FindIndex(TopExp::FirstVertex(TopoDS::Edge(edges(edgeIndex)),true)));
                BRepAdaptor_Curve curve(edge);double a=curve.FirstParameter(),b=curve.LastParameter();
                if(edge.Orientation()==TopAbs_REVERSED)std::swap(a,b);
                const auto sampled=discretizeCurve(curve,a,b,.0005);
                if(!sampled.error.isEmpty())return fail("SLOPED_FLOOR_CURVE_UNPROVEN");
                for(int i=0;i<sampled.points.size()-1;++i)points.append(sampled.points[i]);
            }
            int match=-1;
            for(int i=0;i<selection.boundaryLoops.size();++i) {
                const auto &loop=selection.boundaryLoops[i];
                if(!matched.contains(i) && loop.faceId==id && loop.outer==isOuter && loop.orderedEdgeIds==ids && loop.orderedForward==forward){match=i;break;}
            }
            if(match<0 || points.size()<3)return fail("SLOPED_FLOOR_COMPLETE_SOURCE_WIRES_REQUIRED");
            matched.insert(match);
            if(isOuter) {
                if(!feature.boundaryPoints.isEmpty())return fail("SLOPED_FLOOR_SINGLE_OUTER_REQUIRED");
                feature.boundaryPoints=points;
            } else feature.islandBoundaries.append(points);
        }
        if(feature.boundaryPoints.size()<3 || matched.size()!=selection.boundaryLoops.size())
            return fail("SLOPED_FLOOR_COMPLETE_SOURCE_WIRES_REQUIRED");
        result.sourceOuterBoundary=feature.boundaryPoints;
        auto finish=[&]() {
            feature.center=feature.boundaryPoints.first();feature.planarBoundaryVerified=true;feature.faceIndices={index};
            auto lo=feature.center,hi=feature.center;
            for(const auto &p:feature.boundaryPoints)for(int axis=0;axis<3;++axis){lo[axis]=std::min(double(lo[axis]),double(p[axis]));hi[axis]=std::max(double(hi[axis]),double(p[axis]));}
            feature.length=hi.x()-lo.x();feature.width=hi.y()-lo.y();feature.depth=std::max(.001,double(hi.z()-lo.z()));
            return result;
        };
        // Restrict the candidate region by the full vertical projection of each
        // adjacent planar wall. The exact whole-source sweep remains mandatory.
        QVector<gp_Pnt> polygon;
        QVector<TopoDS_Edge> outerEdges;
        for(BRepTools_WireExplorer it(outer,face);it.More();it.Next()) {
            BRepAdaptor_Curve curve(it.Current());
            if(curve.GetType()!=GeomAbs_Line) {
                // Complete source trims are candidates only. Curved walls have no
                // planar shadow proof; every final cutter/shank sweep must pass.
                result.curvedWallCandidate=true;
                return finish();
            }
            polygon.append(BRep_Tool::Pnt(TopExp::FirstVertex(it.Current(),true)));
            outerEdges.append(it.Current());
        }
        double area=0;
        for(int i=0;i<polygon.size();++i) {
            const auto &a=polygon[i],&b=polygon[(i+1)%polygon.size()];
            area+=a.X()*b.Y()-b.X()*a.Y();
        }
        if(!std::isfinite(area) || std::abs(area)<1e-9)return fail("SLOPED_FLOOR_STRAIGHT_CONVEX_OUTER_REQUIRED");
        const double orientation=area>0?1:-1;
        const auto original=polygon;
        TopTools_IndexedDataMapOfShapeListOfShape adjacent;
        TopExp::MapShapesAndAncestors(material,TopAbs_EDGE,TopAbs_FACE,adjacent);
        const double tolerance=PocketVolumeDetail::shapeTolerance(material)+Precision::Confusion();
        for(int i=0;i<original.size();++i) {
            const auto &a=original[i],&b=original[(i+1)%original.size()];
            const double dx=b.X()-a.X(),dy=b.Y()-a.Y(),length=std::hypot(dx,dy);
            if(length<=tolerance)return fail("SLOPED_FLOOR_STRAIGHT_CONVEX_OUTER_REQUIRED");
            const double nx=-dy/length*orientation,ny=dx/length*orientation;
            auto projection=[&](const gp_Pnt &p){return nx*p.X()+ny*p.Y();};
            const double boundary=projection(a);
            for(const auto &p:original)if(projection(p)<boundary-tolerance)
                return fail("SLOPED_FLOOR_STRAIGHT_CONVEX_OUTER_REQUIRED");
            const int adjacency=adjacent.FindIndex(outerEdges[i]);
            if(adjacency<1)return fail("SLOPED_FLOOR_WALL_ADJACENCY_REQUIRED");
            int count=0,wallIndex=0;
            for(TopTools_ListIteratorOfListOfShape it(adjacent.FindFromIndex(adjacency));it.More();it.Next()) {
                ++count;const int candidate=faces.FindIndex(it.Value());if(candidate!=index)wallIndex=candidate;
            }
            if(count!=2 || wallIndex<1)return fail("SLOPED_FLOOR_WALL_ADJACENCY_REQUIRED");
            const auto wall=TopoDS::Face(faces(wallIndex));BRepAdaptor_Surface wallSurface(wall);
            if(wallSurface.GetType()!=GeomAbs_Plane)return fail("SLOPED_FLOOR_PLANAR_WALL_REQUIRED");
            int wireCount=0;for(TopExp_Explorer it(wall,TopAbs_WIRE);it.More();it.Next())++wireCount;
            if(wireCount!=1)return fail("SLOPED_FLOOR_SINGLE_WALL_WIRE_REQUIRED");
            double limit=boundary;
            for(TopExp_Explorer it(wall,TopAbs_EDGE);it.More();it.Next()) {
                BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
                if(curve.GetType()!=GeomAbs_Line)return fail("SLOPED_FLOOR_STRAIGHT_WALL_REQUIRED");
                for(double t:{curve.FirstParameter(),curve.LastParameter()}) {
                    if(!std::isfinite(t))return fail("SLOPED_FLOOR_WALL_INVALID");
                    const double value=projection(curve.Value(t));
                    if(!std::isfinite(value))return fail("SLOPED_FLOOR_WALL_INVALID");
                    limit=std::max(limit,value);
                }
            }
            if(limit>boundary+tolerance)result.overhangingWallIds.append(QStringLiteral("face:%1").arg(wallIndex));
            QVector<gp_Pnt> clipped;
            for(int j=0;j<polygon.size();++j) {
                const auto &p=polygon[j],&q=polygon[(j+1)%polygon.size()];
                const double dp=projection(p)-limit,dq=projection(q)-limit;
                if(dp>=0)clipped.append(p);
                if((dp>=0)!=(dq>=0)) {
                    const double t=dp/(dp-dq);
                    clipped.append(gp_Pnt(p.X()+t*(q.X()-p.X()),p.Y()+t*(q.Y()-p.Y()),p.Z()+t*(q.Z()-p.Z())));
                }
            }
            polygon=clipped;
            if(polygon.size()<3)return fail("SLOPED_FLOOR_NO_ACCESSIBLE_REGION");
        }
        feature.boundaryPoints.clear();
        for(const auto &p:polygon)feature.boundaryPoints.append(QVector3D(float(p.X()),float(p.Y()),float(p.Z())));
        return finish();
    } catch(const Standard_Failure &) {return fail("SLOPED_FLOOR_GEOMETRY_FAILED");}
#else
    Q_UNUSED(mesh);
    return fail("REGION_EXACT_GEOMETRY_UNAVAILABLE");
#endif
}
