#include "ExactRetractionCheck.h"
#include "SimulationController.h"
#include "../../process_design/selection/PocketOpening.h"
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <limits>

#ifdef CNEXT_ENABLE_OCC
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <gp_Ax2.hxx>

namespace {
bool collectSolids(const TopoDS_Shape &shape, QVector<TopoDS_Solid> &solids)
{
    if (shape.IsNull()) return false;
    if (shape.ShapeType() == TopAbs_SOLID) {
        if (!BRepCheck_Analyzer(shape).IsValid()) return false;
        bool hasShell = false;
        for (TopExp_Explorer it(shape, TopAbs_SHELL); it.More(); it.Next()) {
            hasShell = true;
            if (!BRep_Tool::IsClosed(it.Current())) return false;
        }
        BRepClass3d_SolidClassifier classifier(shape);
        classifier.PerformInfinitePoint(Precision::Confusion());
        if (!hasShell || classifier.State() != TopAbs_OUT) return false;
        solids.append(TopoDS::Solid(shape));
        return true;
    }
    if (shape.ShapeType() != TopAbs_COMPOUND && shape.ShapeType() != TopAbs_COMPSOLID)
        return false; // Loose faces/edges cannot certify all material components.
    bool hasChild = false;
    for (TopoDS_Iterator it(shape); it.More(); it.Next()) {
        hasChild = true;
        if (!collectSolids(it.Value(), solids)) return false;
    }
    return hasChild;
}

double maximumTolerance(const TopoDS_Shape &shape)
{
    double tolerance = 0;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next())
        tolerance = std::max(tolerance, BRep_Tool::Tolerance(TopoDS::Face(it.Current())));
    for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next())
        tolerance = std::max(tolerance, BRep_Tool::Tolerance(TopoDS::Edge(it.Current())));
    for (TopExp_Explorer it(shape, TopAbs_VERTEX); it.More(); it.Next())
        tolerance = std::max(tolerance, BRep_Tool::Tolerance(TopoDS::Vertex(it.Current())));
    return tolerance;
}

TopoDS_Shape boundary(const TopoDS_Shape &solid)
{
    BRep_Builder builder;
    TopoDS_Compound faces;
    builder.MakeCompound(faces);
    for (TopExp_Explorer it(solid, TopAbs_FACE); it.More(); it.Next())
        builder.Add(faces, it.Current());
    return faces;
}

bool interiorWitness(const TopoDS_Shape &sweep, const TopoDS_Solid &material, double error)
{
    BRepAlgoAPI_Common common;
    TopTools_ListOfShape arguments,tools;
    arguments.Append(sweep);tools.Append(material);
    common.SetArguments(arguments);common.SetTools(tools);
    common.SetNonDestructive(true);common.Build();
    if (!common.IsDone()) return false;
    QVector<gp_Pnt> candidates;
    for (TopExp_Explorer it(common.Shape(), TopAbs_SOLID); it.More(); it.Next()) {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(it.Current(), properties);
        if (!std::isfinite(properties.Mass()) || properties.Mass() <= 0) continue;
        candidates.append(properties.CentreOfMass());
    }
    // A ring's centroid lies in its hole. Probe both sides of its actual faces;
    // classification and boundary distance still have to prove every candidate.
    for (TopExp_Explorer it(common.Shape(), TopAbs_FACE); it.More(); it.Next()) {
        BRepAdaptor_Surface surface(TopoDS::Face(it.Current()), true);
        const double u = (surface.FirstUParameter()+surface.LastUParameter())*.5;
        const double v = (surface.FirstVParameter()+surface.LastVParameter())*.5;
        if (!std::isfinite(u) || !std::isfinite(v)) continue;
        gp_Pnt point; gp_Vec du, dv;
        surface.D1(u, v, point, du, dv);
        gp_Vec normal = du.Crossed(dv);
        if (normal.SquareMagnitude() <= 1e-24) continue;
        normal.Normalize();
        candidates.append(point.Translated(normal.Multiplied(4*error)));
        candidates.append(point.Translated(normal.Multiplied(-4*error)));
    }
    const auto sweepBoundary = boundary(sweep), materialBoundary = boundary(material);
    for (const auto &point : candidates) {
        BRepClass3d_SolidClassifier inSweep(sweep, point, error), inMaterial(material, point, error);
        if (inSweep.State() != TopAbs_IN || inMaterial.State() != TopAbs_IN) continue;
        const auto vertex = BRepBuilderAPI_MakeVertex(point).Shape();
        BRepExtrema_DistShapeShape d1(vertex, sweepBoundary, Precision::Confusion());
        BRepExtrema_DistShapeShape d2(vertex, materialBoundary, Precision::Confusion());
        if (d1.IsDone() && d2.IsDone() && d1.Value() > error && d2.Value() > error)
            return true;
    }
    return false; // No interior witness is not evidence of separation.
}

bool floorReleaseTop(const MeshData &mesh,const ToolMove &move,double error,double *releaseZ)
{
    const QPointF center(move.from.x(),move.from.y());
    for(const auto &source:mesh.boundaryLoops) {
        SelectionBoundaryLoop opening;double depth=0,floor=0;
        if(!verifyPocketOpening(mesh,source,&opening,nullptr) ||
           !verifyPocketFlatFloor(mesh,source,opening,&depth,nullptr,&floor))continue;
        // Never turn a below-floor start into contact by applying a tolerance.
        if(move.from.z()!=floor)continue;
        const double top=machiningBoundaryCenter(opening.machiningBoundary).z();
        const double split=floor+std::max(.001,8*error);
        if(!std::isfinite(split) || split<=floor || split>=top || split>=move.to.z()+move.fluteLength)continue;
        QPolygonF polygon;for(const auto &p:opening.machiningBoundary)polygon.append(QPointF(p.x(),p.y()));
        if(!polygon.containsPoint(center,Qt::OddEvenFill))continue;
        bool inside=true;
        for(int i=0;i<polygon.size();++i) {
            const auto a=polygon[i],edge=polygon[(i+1)%polygon.size()]-a;
            const double length2=edge.x()*edge.x()+edge.y()*edge.y();
            if(length2<=0){inside=false;break;}
            const double t=qBound(0.,((center-a).x()*edge.x()+(center-a).y()*edge.y())/length2,1.);
            const auto delta=center-a-edge*t;
            if(std::hypot(delta.x(),delta.y())<=move.toolDiameter*.5+8*error){inside=false;break;}
        }
        if(inside){*releaseZ=split;return true;}
    }
    return false;
}
}
#endif

struct ExactRetractionBatch::Source {
#ifdef CNEXT_ENABLE_OCC
    QVector<TopoDS_Solid> solids;
    bool complete = false;
    double tolerance = 0;
    Bnd_Box bounds;
#endif
};

ExactRetractionBatch::ExactRetractionBatch(const MeshData &mesh) : m_mesh(mesh) {}
ExactRetractionBatch::~ExactRetractionBatch() = default;

RetractionEvidence checkExactRetraction(const MeshData &mesh, const ToolMove &move)
{
    ExactRetractionBatch batch(mesh);
    return batch.check(move);
}

RetractionEvidence ExactRetractionBatch::check(const ToolMove &move)
{
    const auto &mesh = m_mesh;
    RetractionEvidence result;
    const auto positive = [](double value) { return std::isfinite(value) && value > 0; };
    if (!move.isRapid || move.fromKnownAxes != 7 || move.toKnownAxes != 7 ||
        move.from.x() != move.to.x() || move.from.y() != move.to.y() || move.to.z() <= move.from.z()) {
        result.message = QObject::tr("Only a known, fixed-XY upward retract can use this check.");
        return result;
    }
    if (!positive(move.toolDiameter) || !positive(move.fluteLength) || !positive(move.totalLength) ||
        move.totalLength < move.fluteLength || !move.toolModelPath.isEmpty()) {
        result.message = QObject::tr("Complete nominal cutter/shank dimensions are required; custom tool geometry is not certified by cylinders.");
        return result;
    }
#ifdef CNEXT_ENABLE_OCC
    try {
        if (!m_source) {
            auto prepared = std::make_unique<Source>();
            prepared->complete = collectSolids(mesh.sourceShape, prepared->solids);
            if (prepared->complete) {
                prepared->tolerance = maximumTolerance(mesh.sourceShape);
                BRepBndLib::Add(mesh.sourceShape, prepared->bounds);
            }
            // Do not publish partial preparation if OCC throws.
            m_source = std::move(prepared);
        }
        const auto &solids = m_source->solids;
        if (!m_source->complete) {
            result.message = QObject::tr("The exact source is not a complete set of valid closed material solids.");
            return result;
        }
        const double scale = std::max({1.0, std::abs(move.from.x()), std::abs(move.from.y()),
                                      std::abs(move.from.z()), std::abs(move.to.z()), move.totalLength, move.toolDiameter, move.shankDiameter});
        const double rounding = 128 * std::numeric_limits<double>::epsilon() * scale;
        const double sourceTolerance = m_source->tolerance;
        const auto &bounds = m_source->bounds;
        double minX,minY,minZ,maxX,maxY,maxZ;
        if (bounds.IsVoid() || bounds.IsOpen()) return result;
        bounds.Get(minX,minY,minZ,maxX,maxY,maxZ);
        const bool shankAboveMaterial = move.from.z()+move.fluteLength > maxZ+sourceTolerance+Precision::Confusion()+rounding;
        if (move.totalLength > move.fluteLength && !shankAboveMaterial && !positive(move.shankDiameter)) {
            result.message = QObject::tr("Shank diameter is unknown where the shank may approach material.");
            return result;
        }
        const double travel = move.to.z() - move.from.z();
        QVector<TopoDS_Shape> sweeps;
        sweeps.append(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(move.from.x(), move.from.y(), move.from.z()),
                                                    gp_Dir(0,0,1)), move.toolDiameter*.5, travel+move.fluteLength).Shape());
        if (move.totalLength > move.fluteLength && !shankAboveMaterial)
            sweeps.append(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(move.from.x(), move.from.y(), move.from.z()+move.fluteLength),
                                                        gp_Dir(0,0,1)), move.shankDiameter*.5, travel+move.totalLength-move.fluteLength).Shape());
        bool separated = true;
        bool cutterUncertain = false, shankUncertain = false;
        double largestError = 0;
        for (int sweepIndex=0;sweepIndex<sweeps.size();++sweepIndex) {
            const auto &sweep=sweeps[sweepIndex];
            if (!BRepCheck_Analyzer(sweep).IsValid()) {
                result.message = QObject::tr("The cutter sweep could not be represented reliably.");
                return result;
            }
            const double error = sourceTolerance + maximumTolerance(sweep) + Precision::Confusion() + rounding;
            if (!std::isfinite(error)) return result;
            largestError = std::max(largestError, error);
            for (const auto &solid : solids) {
                BRepExtrema_DistShapeShape distance(sweep, solid, Precision::Confusion());
                if (distance.IsDone() && !distance.InnerSolution() && std::isfinite(distance.Value()) && distance.Value() > error)
                    continue;
                separated = false;
                if(sweepIndex==0)cutterUncertain=true;else shankUncertain=true;
                if (interiorWitness(sweep, solid, error)) {
                    result.state = RetractionEvidenceState::Interference;
                    result.message = QObject::tr("An interior overlap exceeds the numerical bound of %1 mm.").arg(error, 0, 'g', 12);
                    return result;
                }
            }
        }
        // A fresh flat-floor prism certifies only the thin contact layer. The
        // upper cutter remains subject to exact separation, and the original
        // full shank sweep above was never shortened. Proven overlap returned
        // already and cannot be overridden by this branch.
        double releaseZ=0;
        bool floorReleased=false;
        if(cutterUncertain && !shankUncertain && floorReleaseTop(mesh,move,largestError,&releaseZ)) {
            const auto upper=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(move.from.x(),move.from.y(),releaseZ),
                gp_Dir(0,0,1)),move.toolDiameter*.5,move.to.z()+move.fluteLength-releaseZ).Shape();
            const double error=sourceTolerance+maximumTolerance(upper)+Precision::Confusion()+rounding;
            separated=BRepCheck_Analyzer(upper).IsValid() && std::isfinite(error);
            for(const auto &solid:solids) {
                BRepExtrema_DistShapeShape distance(upper,solid,Precision::Confusion());
                if(!distance.IsDone() || distance.InnerSolution() || !std::isfinite(distance.Value()) || distance.Value()<=error)
                    separated=false;
            }
            largestError=std::max(largestError,error);
            floorReleased=separated;
        }
        result.state = separated ? RetractionEvidenceState::Separated : RetractionEvidenceState::Uncertain;
        result.message = separated
            ? (floorReleased
               ? QObject::tr("Flat-floor contact release is source-proved; the upper cutter and full shank sweep are separated beyond %1 mm.")
               : QObject::tr("The nominal cutter/shank sweep is separated from all source solids beyond %1 mm.")).arg(largestError, 0, 'g', 12)
            : QObject::tr("Contact or overlap is unresolved within the CAD/numerical bound of %1 mm.").arg(largestError, 0, 'g', 12);
    } catch (const Standard_Failure &) {
        result.message = QObject::tr("Exact geometry computation did not establish retract clearance.");
    }
#else
    Q_UNUSED(mesh)
    result.message = QObject::tr("Exact geometry is unavailable in this build.");
#endif
    return result;
}
