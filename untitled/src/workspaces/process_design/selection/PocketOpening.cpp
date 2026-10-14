#include "PocketOpening.h"
#ifdef CNEXT_ENABLE_OCC
#include "BoundedCurveDiscretization.h"
#include "PocketVolumeProof.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <Standard_Failure.hxx>
#endif


bool verifyPocketOpening(const MeshData &mesh,const MeshBoundaryLoop &source,
                         SelectionBoundaryLoop *evidence,MachiningFeature *feature,QString *error)
{
    auto fail=[&](const char *message){if(error)*error=QString::fromLatin1(message);return false;};
    if(evidence)*evidence={};
    if(source.outer || source.orderedEdgeIds.isEmpty() ||
       source.orderedForward.size()!=source.orderedEdgeIds.size())return fail("REGION_INNER_WIRE_REQUIRED");
#ifdef CNEXT_ENABLE_OCC
    try {
        if(mesh.sourceShape.IsNull() || !TopExp_Explorer(mesh.sourceShape,TopAbs_SOLID).More())
            return fail("REGION_SOURCE_SOLID_REQUIRED");
        // OCC validation/classification can update shared topology flags. Keep the
        // complete proof, including its containment checks, on an independent shape.
        const auto sourceShape=BRepBuilderAPI_Copy(mesh.sourceShape).Shape();
        TopTools_IndexedMapOfShape faces,edges;
        TopExp::MapShapes(sourceShape,TopAbs_FACE,faces);
        TopExp::MapShapes(sourceShape,TopAbs_EDGE,edges);
        if(source.faceIndex<1 || source.faceIndex>faces.Extent())return fail("REGION_SOURCE_FACE_INVALID");
        const auto face=TopoDS::Face(faces(source.faceIndex));
        BRepAdaptor_Surface surface(face);
        if(surface.GetType()!=GeomAbs_Plane)return fail("REGION_HORIZONTAL_PLANE_REQUIRED");
        auto normal=surface.Plane().Axis().Direction();
        if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
        if(normal.Z()<.99999)return fail("REGION_SETUP_DIRECTION_MISMATCH");
        // Also bind the displayed/picked plane to the exact source plane.
        for(const auto &t:mesh.triangles)if(t.faceIndex==source.faceIndex && t.normal.z()<.99999f)
            return fail("REGION_SETUP_DIRECTION_MISMATCH");
        const double tolerance=.002;
        SelectionBoundaryLoop proof{source.faceId,source.outer,source.orderedEdgeIds,source.orderedForward};
        proof.boundaryTolerance=tolerance;
        proof.discretizationRule="occ-bezier-finite-chord-v1";
        BRepBuilderAPI_MakeWire wire;
        QSet<int> walls,common;
        QString firstVertex,lastVertex;
        QVector3D firstPoint,lastPoint;
        bool generalCurve=false;
        QVector<QVector3D> sourceEnds,sourceStarts;
        QVector<double> sourceBounds;
        for(int i=0;i<source.orderedEdgeIds.size();++i){
            const QString id=source.orderedEdgeIds[i];
            const MeshPickEdge *picked=nullptr;
            for(const auto &e:mesh.pickEdges)if(e.id==id){picked=&e;break;}
            const int index=id.section(':',1).toInt();
            if(!picked || picked->degenerated || picked->parameters.size()<2 || index<1 || index>edges.Extent())
                return fail("REGION_EDGE_INVALID");
            const bool forward=source.orderedForward[i];
            const QString start=forward?picked->firstVertexId:picked->lastVertexId;
            const QString end=forward?picked->lastVertexId:picked->firstVertexId;
            if(start.isEmpty() || end.isEmpty() || (i && start!=lastVertex))return fail("REGION_CHAIN_NOT_CLOSED");
            auto edge=TopoDS::Edge(edges(index));
            if(!forward)edge.Reverse();
            wire.Add(edge);
            BRepAdaptor_Curve curve(TopoDS::Edge(edges(index)));
            // This proof consumes the entire source edge, not its display samples.
            // Resolve its exact OCC interval in the same oriented vertex order.
            const bool increasing=forward != (edges(index).Orientation()==TopAbs_REVERSED);
            const double a=increasing?curve.FirstParameter():curve.LastParameter();
            const double b=increasing?curve.LastParameter():curve.FirstParameter();
            auto vec=[](const gp_Pnt &p){return QVector3D(float(p.X()),float(p.Y()),float(p.Z()));};
            MachiningBoundaryCurve segment;
            segment.start=vec(curve.Value(a));segment.end=vec(curve.Value(b));
            if(i && (segment.start-lastPoint).length()>.001f)return fail("REGION_CHAIN_NOT_CLOSED");
            if(!i){firstVertex=start;firstPoint=segment.start;}
            if(std::abs(segment.start.z()-firstPoint.z())>.001f || std::abs(segment.end.z()-firstPoint.z())>.001f)
                return fail("REGION_HORIZONTAL_PLANE_REQUIRED");
            if(curve.GetType()==GeomAbs_Circle){
                const auto circle=curve.Circle();
                if(std::abs(circle.Axis().Direction().Z())<.99999)return fail("REGION_HORIZONTAL_ARC_REQUIRED");
                segment.circular=true;segment.center=vec(circle.Location());
                segment.sweep=(b-a)*circle.Axis().Direction().Z();
            }
            const auto sampled=discretizeCurve(curve,a,b,tolerance*.25);
            if(!sampled.error.isEmpty())return fail(qPrintable(sampled.error));
            switch(curve.GetType()) {
            case GeomAbs_Line:segment.type="line";break;
            case GeomAbs_Circle:segment.type="circle";break;
            case GeomAbs_Ellipse:segment.type="ellipse";break;
            case GeomAbs_BezierCurve:segment.type="bezier";break;
            case GeomAbs_BSplineCurve:segment.type="bspline";break;
            default:return fail("REGION_CURVE_UNSUPPORTED");
            }
            segment.firstParameter=a;segment.lastParameter=b;segment.approximationBound=sampled.computedBound;
            generalCurve|=curve.GetType()!=GeomAbs_Line && curve.GetType()!=GeomAbs_Circle;
            sourceStarts.append(sampled.points.first());sourceEnds.append(sampled.points.last());sourceBounds.append(sampled.computedBound);
            for(int j=0;j<sampled.points.size()-1;++j)proof.machiningBoundary.append(sampled.points[j]);
            proof.curves.append(segment);
            lastVertex=end;lastPoint=segment.end;
            if(picked->adjacentFaceIndices.size()!=2 || !picked->adjacentFaceIndices.contains(source.faceIndex))
                return fail("REGION_WALL_TOPOLOGY_UNSUPPORTED");
            const int wall=picked->adjacentFaceIndices[0]==source.faceIndex?picked->adjacentFaceIndices[1]:picked->adjacentFaceIndices[0];
            walls.insert(wall);
            QSet<int> neighbours;
            for(const auto &e:mesh.pickEdges)if(e.adjacentFaceIndices.contains(wall))
                for(int f:e.adjacentFaceIndices)if(f!=wall && f!=source.faceIndex)neighbours.insert(f);
            if(!i)common=neighbours;else common.intersect(neighbours);
        }
        if(lastVertex!=firstVertex || (lastPoint-firstPoint).length()>.001f || !wire.IsDone())return fail("REGION_CHAIN_NOT_CLOSED");
        const auto center=machiningBoundaryCenter(proof.machiningBoundary);
        double sourceBound=0;
        for(int i=0;i<sourceEnds.size();++i)sourceBound=std::max(sourceBound,sourceBounds[i]+double((sourceEnds[i]-sourceStarts[(i+1)%sourceStarts.size()]).length()));
        if(sourceBound>tolerance*.25)return fail("REGION_SOURCE_JOIN_BUDGET_EXCEEDED");
        proof.sourceBoundaryBound=sourceBound;
        bool raisedWalls=true,hasRaisedWall=false;
        for(int wall:walls){Bnd_Box box;BRepBndLib::Add(faces(wall),box,false);double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);raisedWalls &= z0>=firstPoint.z()-.001;hasRaisedWall |= z1>firstPoint.z()+.001;}
        if(raisedWalls && hasRaisedWall){proof.machiningRole=MachiningBoundaryRole::Island;if(evidence)*evidence=proof;return fail("REGION_ISLAND_REQUIRES_SURROUNDING_OUTER");}
        double area=0;
        for(int i=0;i<proof.machiningBoundary.size();++i){const auto&a=proof.machiningBoundary[i];const auto&b=proof.machiningBoundary[(i+1)%proof.machiningBoundary.size()];area+=double(a.x())*b.y()-double(a.y())*b.x();}
        if(std::abs(area)<1e-6)return fail("REGION_WIRE_INVALID");
        common.subtract(walls);
        const int floor=common.size()==1?*common.cbegin():-1;
        double candidateBottom=-std::numeric_limits<double>::infinity();
        if(floor>0) {
            BRepAdaptor_Surface bottom(TopoDS::Face(faces(floor)));
            if(bottom.GetType()!=GeomAbs_Plane)return fail("REGION_TRANSITION_PLANE_UNSUPPORTED");
            Bnd_Box bounds;BRepBndLib::Add(faces(floor),bounds,false);
            double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);candidateBottom=z1;
        } else for(int wall:walls) {
            Bnd_Box bounds;BRepBndLib::Add(faces(wall),bounds,false);
            double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);candidateBottom=std::max(candidateBottom,z0);
        }
        proof.maximumDepth=std::floor((double(center.z())-candidateBottom-.001)*1000)/1000;
        if(!std::isfinite(proof.maximumDepth) || proof.maximumDepth<=.001)return fail("REGION_POSITIVE_DEPTH_UNPROVEN");
        BRepBuilderAPI_MakeFace makeFace(wire.Wire(),true);
        if(!makeFace.IsDone() || !BRepCheck_Analyzer(makeFace.Face()).IsValid())return fail("REGION_WIRE_INVALID");
        TopoDS_Face offsetInput=makeFace.Face();
        proof.candidateRule="occ-wire-inward-offset-v1";
        if(generalCurve) {
            BRepBuilderAPI_MakePolygon candidatePolygon;
            double flatten=0;
            for(const auto &p:proof.machiningBoundary){flatten=std::max(flatten,std::abs(double(p.z())-center.z()));candidatePolygon.Add(gp_Pnt(p.x(),p.y(),center.z()));}
            if(sourceBound+flatten>tolerance*.25)return fail("REGION_SOURCE_FLATTEN_BUDGET_EXCEEDED");
            proof.sourceBoundaryBound=sourceBound+flatten;
            proof.candidateRule="source-polygon-inset-candidate-v1";
            candidatePolygon.Close();if(!candidatePolygon.IsDone())return fail("REGION_SOURCE_POLYGON_INVALID");
            BRepBuilderAPI_MakeFace candidateFace(candidatePolygon.Wire(),true);
            if(!candidateFace.IsDone() || !BRepCheck_Analyzer(candidateFace.Face()).IsValid())return fail("REGION_SOURCE_POLYGON_INVALID");
            offsetInput=candidateFace.Face();
        }
        // Offset construction supplies a candidate; containment and the full
        // volume proof below establish the usable region without volume-noise allowances.
        GProp_GProps originalArea;BRepGProp::SurfaceProperties(makeFace.Face(),originalArea);
        TopoDS_Face insetFace;
        for(double sign:{-1.0,1.0}){
            BRepOffsetAPI_MakeOffset offset(offsetInput,GeomAbs_Arc);
            offset.Perform(sign*tolerance);
            if(!offset.IsDone())continue;
            TopExp_Explorer loops(offset.Shape(),TopAbs_WIRE);
            if(!loops.More())continue;
            BRepBuilderAPI_MakeFace candidate(TopoDS::Wire(loops.Current()),true);
            loops.Next();if(loops.More() || !candidate.IsDone())continue;
            GProp_GProps area;BRepGProp::SurfaceProperties(candidate.Face(),area);
            if(area.Mass()<=0 || area.Mass()>=originalArea.Mass())continue;
            BRepAlgoAPI_Cut outside;
            TopTools_ListOfShape arguments,tools;arguments.Append(candidate.Face());tools.Append(makeFace.Face());
            outside.SetArguments(arguments);outside.SetTools(tools);outside.SetNonDestructive(true);outside.Build();
            if(!outside.IsDone())continue;
            GProp_GProps excess;BRepGProp::SurfaceProperties(outside.Shape(),excess);
            if(excess.Mass()>1e-9)continue;
            insetFace=candidate.Face();break;
        }
        if(insetFace.IsNull() || !BRepCheck_Analyzer(insetFace).IsValid())return fail("REGION_INWARD_TOLERANCE_OFFSET_FAILED");
        proof.machiningBoundary.clear();
        TopExp_Explorer insetLoops(insetFace,TopAbs_WIRE);
        const auto insetWire=TopoDS::Wire(insetLoops.Current());
        QVector<QVector3D> insetEnds,insetStarts;QVector<double> insetBounds;
        for(BRepTools_WireExplorer it(insetWire,insetFace);it.More();it.Next()){
            const auto edge=it.Current();BRepAdaptor_Curve curve(edge);
            double a=curve.FirstParameter(),b=curve.LastParameter();if(edge.Orientation()==TopAbs_REVERSED)std::swap(a,b);
            const auto sampled=discretizeCurve(curve,a,b,tolerance*.25);
            if(!sampled.error.isEmpty())return fail(qPrintable(sampled.error));
            insetStarts.append(sampled.points.first());insetEnds.append(sampled.points.last());insetBounds.append(sampled.computedBound);
            for(int j=0;j<sampled.points.size()-1;++j)proof.machiningBoundary.append(sampled.points[j]);
        }
        BRepBuilderAPI_MakePolygon polygon;
        double consumedBound=0,flatten=0;
        for(int i=0;i<insetEnds.size();++i)consumedBound=std::max(consumedBound,insetBounds[i]+double((insetEnds[i]-insetStarts[(i+1)%insetStarts.size()]).length()));
        for(auto &p:proof.machiningBoundary){flatten=std::max(flatten,std::abs(double(p.z())-center.z()));p.setZ(center.z());polygon.Add(gp_Pnt(p.x(),p.y(),p.z()));}
        if(consumedBound+flatten>tolerance*.25)return fail("REGION_CONSUMED_BOUNDARY_BUDGET_EXCEEDED");
        proof.consumedBoundaryBound=consumedBound+flatten;
        polygon.Close();
        if(!polygon.IsDone())return fail("REGION_DISCRETE_BOUNDARY_INVALID");
        BRepBuilderAPI_MakeFace consumedFace(polygon.Wire(),true);
        if(!consumedFace.IsDone() || !BRepCheck_Analyzer(consumedFace.Face()).IsValid())return fail("REGION_DISCRETE_BOUNDARY_INVALID");
        // Compare XY regions on the same source plane. The actual constant-Z
        // polygon and its prism below remain unchanged for the 3D proof.
        BRepAdaptor_Surface originalSurface(makeFace.Face());
        if(originalSurface.GetType()!=GeomAbs_Plane)return fail("REGION_SOURCE_PLANE_INVALID");
        const auto plane=originalSurface.Plane();const auto origin=plane.Location();const auto direction=plane.Axis().Direction();
        for(double value:{origin.X(),origin.Y(),origin.Z(),direction.X(),direction.Y(),direction.Z()})
            if(!std::isfinite(value))return fail("REGION_SOURCE_PLANE_INVALID");
        if(std::abs(direction.Z())<.99999)return fail("REGION_SOURCE_PLANE_INVALID");
        BRepBuilderAPI_MakePolygon liftedPolygon;double maximumLift=0,maximumResidual=0,liftScale=1;
        for(const auto &p:proof.machiningBoundary) {
            const double z=origin.Z()-(direction.X()*(double(p.x())-origin.X())+direction.Y()*(double(p.y())-origin.Y()))/direction.Z();
            if(!std::isfinite(z))return fail("REGION_CONTAINMENT_PROJECTION_INVALID");
            const gp_Pnt lifted(p.x(),p.y(),z);liftedPolygon.Add(lifted);
            maximumLift=std::max(maximumLift,std::abs(z-double(p.z())));
            maximumResidual=std::max(maximumResidual,plane.Distance(lifted));
            liftScale=std::max({liftScale,std::abs(double(p.x())),std::abs(double(p.y())),std::abs(z),std::abs(origin.X()),std::abs(origin.Y()),std::abs(origin.Z())});
        }
        liftedPolygon.Close();if(!liftedPolygon.IsDone())return fail("REGION_CONTAINMENT_PROJECTION_INVALID");
        BRepBuilderAPI_MakeFace liftedFace(plane,liftedPolygon.Wire(),true);
        if(!liftedFace.IsDone() || !BRepCheck_Analyzer(liftedFace.Face()).IsValid())return fail("REGION_CONTAINMENT_PROJECTION_INVALID");
        const double liftBudget=PocketVolumeDetail::shapeTolerance(makeFace.Face())+PocketVolumeDetail::shapeTolerance(liftedFace.Face())+
            Precision::Confusion()+128*std::numeric_limits<double>::epsilon()*liftScale;
        if(!std::isfinite(liftBudget) || !std::isfinite(maximumResidual) || maximumResidual>liftBudget ||
           consumedBound+maximumLift+liftBudget>tolerance*.25)return fail("REGION_CONTAINMENT_PROJECTION_BUDGET_EXCEEDED");
        proof.consumedBoundaryBound=std::max(proof.consumedBoundaryBound,consumedBound+maximumLift+liftBudget);
        BRepAlgoAPI_Cut outsideConsumed;
        TopTools_ListOfShape consumedArguments,consumedTools;consumedArguments.Append(liftedFace.Face());consumedTools.Append(makeFace.Face());
        outsideConsumed.SetArguments(consumedArguments);outsideConsumed.SetTools(consumedTools);outsideConsumed.SetNonDestructive(true);outsideConsumed.Build();
        if(!outsideConsumed.IsDone() || TopExp_Explorer(outsideConsumed.Shape(),TopAbs_FACE).More())return fail("REGION_CONSUMED_POLYGON_NOT_CONTAINED");
        const auto prism=BRepPrimAPI_MakePrism(consumedFace.Face(),gp_Vec(0,0,-proof.maximumDepth)).Shape();
        BRepAlgoAPI_Common intersection;
        TopTools_ListOfShape intersectionArguments,intersectionTools;intersectionArguments.Append(prism);intersectionTools.Append(sourceShape);
        intersection.SetArguments(intersectionArguments);intersection.SetTools(intersectionTools);intersection.SetNonDestructive(true);intersection.Build();
        if(!intersection.IsDone())return fail("REGION_SOLID_INTERSECTION_FAILED");
        if(TopExp_Explorer(intersection.Shape(),TopAbs_SOLID).More())return fail("REGION_ISLAND_OR_WALL_OBSTRUCTION");
        const auto volume=verifyPocketVolume(prism,sourceShape);
        if(!volume.separated)return fail("REGION_POSITIVE_CLEARANCE_UNPROVEN");
        proof.volumeProofRule="occ-boundary-component-escape-v1";
        proof.volumeProofBudget=volume.boundaryBudget;
        proof.maximumEscapeBudget=volume.maximumEscapeBudget;
        proof.machiningRole=MachiningBoundaryRole::Outer;
        QList<int> support=walls.values();std::sort(support.begin(),support.end());if(floor>0)support.append(floor);
        for(int id:support)proof.supportFaceIds.append(QStringLiteral("face:%1").arg(id));
        if(evidence)*evidence=proof;
        if(feature){
            *feature={};feature->kind=FeatureKind::Pocket;feature->subType="verified_slot_opening";
            feature->center=machiningBoundaryCenter(proof.machiningBoundary);feature->axis={0,0,1};feature->region=FaceRegion::Front;
            feature->depth=0;feature->boundaryPoints=proof.machiningBoundary;
            feature->planarBoundaryVerified=true;feature->faceIndices=support.toVector();
            QVector3D lo=proof.machiningBoundary.first(),hi=lo;
            for(const auto&p:proof.machiningBoundary){lo.setX(qMin(lo.x(),p.x()));hi.setX(qMax(hi.x(),p.x()));lo.setY(qMin(lo.y(),p.y()));hi.setY(qMax(hi.y(),p.y()));}
            feature->length=hi.x()-lo.x();feature->width=hi.y()-lo.y();
        }
        if(error)error->clear();
        return true;
    } catch(const Standard_Failure &){return fail("REGION_SOURCE_GEOMETRY_FAILED");}
#else
    Q_UNUSED(mesh);Q_UNUSED(feature);
    return fail("REGION_EXACT_GEOMETRY_UNAVAILABLE");
#endif
}

bool verifyPocketFlatFloor(const MeshData &mesh, const MeshBoundaryLoop &source,
                           const SelectionBoundaryLoop &opening, double *depth, QString *error, double *floorZ)
{
    if(depth)*depth=0;
    if(floorZ)*floorZ=0;
    auto fail=[&](const char *message){if(error)*error=QString::fromLatin1(message);return false;};
#ifdef CNEXT_ENABLE_OCC
    using namespace PocketVolumeDetail;
    try {
        SelectionBoundaryLoop fresh;
        if(!verifyPocketOpening(mesh,source,&fresh,nullptr) || !(fresh==opening))
            return fail("POCKET_FLOOR_SOURCE_PROOF_MISMATCH");
        // Keep all additional Boolean and classifier work off the shared source.
        const auto material=BRepBuilderAPI_Copy(mesh.sourceShape).Shape();
        QVector<TopoDS_Solid> solids;
        if(!completeSolids(material,solids))return fail("POCKET_FLOOR_SOLID_REQUIRED");
        TopTools_IndexedMapOfShape faces;TopExp::MapShapes(material,TopAbs_FACE,faces);
        const double top=machiningBoundaryCenter(opening.machiningBoundary).z();
        for(const QString &id:opening.supportFaceIds) {
            const int index=id.section(':',1).toInt();
            if(index<1 || index>faces.Extent())continue;
            const auto floor=TopoDS::Face(faces(index));
            BRepAdaptor_Surface surface(floor);
            if(surface.GetType()!=GeomAbs_Plane)continue;
            const auto plane=surface.Plane();auto normal=plane.Axis().Direction();
            if(floor.Orientation()==TopAbs_REVERSED)normal.Reverse();
            // First support exact axial planes only; a near-horizontal slope is not a flat floor.
            if(normal.X()!=0 || normal.Y()!=0 || normal.Z()!=1)continue;
            const double bottom=plane.Location().Z(),fullDepth=top-bottom;
            if(!std::isfinite(fullDepth) || fullDepth<=opening.maximumDepth)continue;
            BRepBuilderAPI_MakePolygon upperPolygon,lowerPolygon;
            for(const auto &p:opening.machiningBoundary) {
                upperPolygon.Add(gp_Pnt(p.x(),p.y(),top));
                lowerPolygon.Add(gp_Pnt(p.x(),p.y(),bottom));
            }
            upperPolygon.Close();lowerPolygon.Close();
            if(!upperPolygon.IsDone() || !lowerPolygon.IsDone())continue;
            BRepBuilderAPI_MakeFace upper(upperPolygon.Wire(),true),lower(lowerPolygon.Wire(),true);
            if(!upper.IsDone() || !lower.IsDone() || !BRepCheck_Analyzer(lower.Face()).IsValid())continue;
            const auto prism=BRepPrimAPI_MakePrism(upper.Face(),gp_Vec(0,0,-fullDepth)).Shape();
            QVector<TopoDS_Solid> volumes;
            if(!completeSolids(prism,volumes) || volumes.size()!=1)continue;
            Bnd_Box bounds;BRepBndLib::Add(material,bounds,false);BRepBndLib::Add(prism,bounds,false);
            if(bounds.IsVoid() || bounds.IsOpen())continue;
            double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
            const double scale=std::max({1.,std::abs(x0),std::abs(y0),std::abs(z0),std::abs(x1),std::abs(y1),std::abs(z1)});
            const double budget=shapeTolerance(material)+shapeTolerance(prism)+shapeTolerance(lower.Face())+
                Precision::Confusion()+128*std::numeric_limits<double>::epsilon()*scale;
            if(!std::isfinite(budget) || budget<=0)continue;
            // A connected cap cannot cross any outer or inner trim edge of the floor.
            bool covered=true,hasEdges=false;
            for(TopExp_Explorer edge(floor,TopAbs_EDGE);edge.More();edge.Next()) {
                hasEdges=true;
                BRepExtrema_DistShapeShape distance(lower.Face(),edge.Current(),Precision::Confusion());
                if(!distance.IsDone() || distance.NbSolution()==0 || distance.InnerSolution() ||
                   !std::isfinite(distance.Value()) || distance.Value()<=budget){covered=false;break;}
            }
            if(!covered || !hasEdges)continue;
            gp_Pnt witness;double u=0,v=0;
            if(!BRepClass3d_SolidExplorer::FindAPointInTheFace(lower.Face(),witness,u,v))continue;
            BRepClass_FaceClassifier own(lower.Face(),witness,budget),onFloor(floor,witness,budget);
            if(own.State()!=TopAbs_IN || onFloor.State()!=TopAbs_IN)continue;
            BRepAlgoAPI_Cut outside;
            TopTools_ListOfShape arguments,tools;arguments.Append(lower.Face());tools.Append(floor);
            outside.SetArguments(arguments);outside.SetTools(tools);outside.SetNonDestructive(true);outside.Build();
            if(!outside.IsDone() || TopExp_Explorer(outside.Shape(),TopAbs_FACE).More())continue;
            const auto prismBoundary=boundary(prism);
            Bnd_Box prismBounds;BRepBndLib::Add(prism,prismBounds,false);
            double escapeBudget=0;bool clear=true;
            for(int other=1;other<=faces.Extent();++other) {
                if(other==index)continue;
                const auto face=TopoDS::Face(faces(other));
                BRepExtrema_DistShapeShape distance(face,prismBoundary,Precision::Confusion());
                if(!distance.IsDone() || distance.NbSolution()==0 || distance.InnerSolution() ||
                   !std::isfinite(distance.Value()) || distance.Value()<=budget ||
                   !faceOutside(face,prismBounds,prismBoundary,budget,escapeBudget)){clear=false;break;}
            }
            if(!clear)continue;
            // Every non-floor source face is outside P. The allowed face only touches its
            // bottom plane. The freshly proved upper void is a nonempty subset of P,
            // so connected P cannot be contained in material without crossing a source face.
            if(depth)*depth=fullDepth;
            if(floorZ)*floorZ=bottom;
            if(error)error->clear();
            return true;
        }
        return fail("POCKET_SINGLE_FLAT_FLOOR_UNPROVEN");
    } catch(const Standard_Failure &) {return fail("POCKET_FLOOR_GEOMETRY_FAILED");}
#else
    Q_UNUSED(mesh);Q_UNUSED(source);Q_UNUSED(opening);
    return fail("REGION_EXACT_GEOMETRY_UNAVAILABLE");
#endif
}

