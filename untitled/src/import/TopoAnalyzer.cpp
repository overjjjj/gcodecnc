#include "TopoAnalyzer.h"

#ifdef CNEXT_ENABLE_OCC
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GeomAbs_Shape.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>

namespace {

static QVector3D toVector3D(const gp_Dir &dir)
{
    return QVector3D(float(dir.X()), float(dir.Y()), float(dir.Z()));
}

static bool faceNormalAtUv(const TopoDS_Face &face,
                           double u,
                           double v,
                           gp_Dir &outNormal)
{
    BRepAdaptor_Surface surface(face, false);
    gp_Pnt point;
    gp_Vec du;
    gp_Vec dv;
    surface.D1(u, v, point, du, dv);
    gp_Vec normal = du.Crossed(dv);
    if (normal.Magnitude() <= 1.0e-12) {
        return false;
    }

    if (face.Orientation() == TopAbs_REVERSED) {
        normal.Reverse();
    }

    outNormal = gp_Dir(normal);
    return true;
}

static bool sampleFaceNormal(const TopoDS_Face &face, gp_Dir &outNormal)
{
    double uMin = 0.0;
    double uMax = 0.0;
    double vMin = 0.0;
    double vMax = 0.0;
    BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);

    if (std::abs(uMax - uMin) <= 1.0e-12 || std::abs(vMax - vMin) <= 1.0e-12) {
        return false;
    }

    const double midU = 0.5 * (uMin + uMax);
    const double midV = 0.5 * (vMin + vMax);
    if (faceNormalAtUv(face, midU, midV, outNormal)) {
        return true;
    }

    const double du = 0.1 * (uMax - uMin);
    const double dv = 0.1 * (vMax - vMin);
    const gp_Pnt2d probes[] = {
        gp_Pnt2d(midU - du, midV - dv),
        gp_Pnt2d(midU + du, midV - dv),
        gp_Pnt2d(midU - du, midV + dv),
        gp_Pnt2d(midU + du, midV + dv)
    };
    for (const gp_Pnt2d &probe : probes) {
        if (faceNormalAtUv(face, probe.X(), probe.Y(), outNormal)) {
            return true;
        }
    }
    return false;
}

static FaceRegion classifyRegion(const QVector3D &normal)
{
    const QVector3D unit = normal.normalized();
    if (unit.lengthSquared() <= 1.0e-8f) {
        return FaceRegion::Unknown;
    }

    if (unit.z() >= 0.75f) {
        return FaceRegion::Front;
    }
    if (unit.z() <= -0.75f) {
        return FaceRegion::Back;
    }
    return FaceRegion::Side;
}

static FaceNode makeFaceNode(int index, const TopoDS_Face &face)
{
    FaceNode node;
    node.index = index;

    BRepAdaptor_Surface surface(face, false);
    node.surfType = surface.GetType();
    gp_Dir sampledNormal;
    if (sampleFaceNormal(face, sampledNormal)) {
        node.normal = toVector3D(sampledNormal);
        node.region = classifyRegion(node.normal);
    }

    switch (node.surfType) {
    case GeomAbs_Plane:
        node.axis = surface.Plane().Axis();
        if (node.normal.lengthSquared() <= 1.0e-8f) {
            node.normal = toVector3D(surface.Plane().Axis().Direction());
            node.region = classifyRegion(node.normal);
        }
        break;
    case GeomAbs_Cylinder:
        node.axis = surface.Cylinder().Axis();
        node.radius = surface.Cylinder().Radius();
        break;
    case GeomAbs_Cone:
        node.axis = surface.Cone().Axis();
        node.radius = surface.Cone().RefRadius();
        break;
    default:
        break;
    }
    return node;
}

static bool surfaceNormalAtMidpoint(const TopoDS_Edge &edge,
                                    const TopoDS_Face &face,
                                    gp_Dir &outNormal)
{
    Standard_Real first = 0.0;
    Standard_Real last = 0.0;
    Handle(Geom2d_Curve) pcurve = BRep_Tool::CurveOnSurface(edge, face, first, last);
    if (pcurve.IsNull()) {
        return false;
    }

    const Standard_Real mid = 0.5 * (first + last);
    gp_Pnt2d uv = pcurve->Value(mid);

    BRepAdaptor_Surface surface(face, false);
    gp_Pnt point;
    gp_Vec du;
    gp_Vec dv;
    surface.D1(uv.X(), uv.Y(), point, du, dv);
    gp_Vec normal = du.Crossed(dv);
    if (normal.Magnitude() <= 1.0e-12) {
        return false;
    }

    if (face.Orientation() == TopAbs_REVERSED) {
        normal.Reverse();
    }

    outNormal = gp_Dir(normal);
    return true;
}

static bool edgeTangentAtMidpoint(const TopoDS_Edge &edge, gp_Dir &outTangent)
{
    BRepAdaptor_Curve curve(edge);
    if (curve.GetType() == GeomAbs_OtherCurve && curve.FirstParameter() == curve.LastParameter()) {
        return false;
    }

    const Standard_Real mid = 0.5 * (curve.FirstParameter() + curve.LastParameter());
    gp_Pnt point;
    gp_Vec tangent;
    curve.D1(mid, point, tangent);
    if (tangent.Magnitude() <= 1.0e-12) {
        return false;
    }

    outTangent = gp_Dir(tangent);
    return true;
}

static EdgeAttr makeEdgeAttr(const TopoDS_Edge &edge,
                             const TopoDS_Face &face,
                             const TopoDS_Face &neighbor)
{
    EdgeAttr attr;
    const GeomAbs_Shape continuity = BRep_Tool::Continuity(edge, face, neighbor);
    attr.isSmooth = continuity >= GeomAbs_G1;

    gp_Dir n1;
    gp_Dir n2;
    gp_Dir tangent;
    if (surfaceNormalAtMidpoint(edge, face, n1) &&
        surfaceNormalAtMidpoint(edge, neighbor, n2) &&
        edgeTangentAtMidpoint(edge, tangent)) {
        const gp_Vec cross = gp_Vec(n1).Crossed(gp_Vec(n2));
        const double sign = cross.Dot(gp_Vec(tangent));
        attr.isConcave = sign < 0.0;
    }

    return attr;
}

} // namespace

TopoGraph TopoAnalyzer::analyze(const TopoDS_Shape &shape) const
{
    TopoGraph graph;
    TopExp::MapShapes(shape, TopAbs_FACE, graph.faceMap);

    graph.nodes.reserve(graph.faceMap.Extent());
    for (int faceIndex = 1; faceIndex <= graph.faceMap.Extent(); ++faceIndex) {
        graph.nodes.append(makeFaceNode(faceIndex, TopoDS::Face(graph.faceMap(faceIndex))));
    }

    TopTools_IndexedDataMapOfShapeListOfShape edgeFaceMap;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeFaceMap);

    for (int faceIndex = 1; faceIndex <= graph.faceMap.Extent(); ++faceIndex) {
        const TopoDS_Face face = TopoDS::Face(graph.faceMap(faceIndex));
        for (TopExp_Explorer edgeExp(face, TopAbs_EDGE); edgeExp.More(); edgeExp.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());
            if (!edgeFaceMap.Contains(edge)) {
                continue;
            }

            const TopTools_ListOfShape &adjacent = edgeFaceMap.FindFromKey(edge);
            for (TopTools_ListIteratorOfListOfShape it(adjacent); it.More(); it.Next()) {
                const TopoDS_Shape &neighborShape = it.Value();
                if (neighborShape.IsSame(face)) {
                    continue;
                }

                const int neighborIndex = graph.faceMap.FindIndex(neighborShape);
                if (neighborIndex <= 0) {
                    continue;
                }

                const TopoDS_Face neighborFace = TopoDS::Face(neighborShape);
                graph.aag[faceIndex].append(qMakePair(neighborIndex,
                                                      makeEdgeAttr(edge, face, neighborFace)));
            }
        }
    }

    return graph;
}
#endif
