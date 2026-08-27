#include "SlotFrameRecognizer.h"

#include "../core/FeatureIdentity.h"

#include <algorithm>
#include <cmath>

#ifdef CNEXT_ENABLE_OCC
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#endif

namespace {

constexpr double kGeometryTolerance = 1.0e-5;

bool IsFrontZ(const MachiningFeature &source)
{
    const QVector3D axis = source.axis.normalized();
    return source.region == FaceRegion::Front &&
           axis.lengthSquared() > 0.0f && axis.z() >= 0.999f;
}

bool IsCoplanar(const QVector<QVector3D> &points)
{
    if (points.size() < 3) {
        return false;
    }
    const double z = points.first().z();
    return std::all_of(points.cbegin(), points.cend(), [z](const QVector3D &point) {
        return std::abs(double(point.z()) - z) <= kGeometryTolerance;
    });
}

double CrossZ(const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    return (double(b.x()) - a.x()) * (double(c.y()) - b.y()) -
           (double(b.y()) - a.y()) * (double(c.x()) - b.x());
}

bool IsConvex(const QVector<QVector3D> &points)
{
    if (points.size() < 3) {
        return false;
    }
    double sign = 0.0;
    for (int index = 0; index < points.size(); ++index) {
        const double cross = CrossZ(points.at(index),
                                    points.at((index + 1) % points.size()),
                                    points.at((index + 2) % points.size()));
        if (std::abs(cross) <= kGeometryTolerance) {
            continue;
        }
        if (sign == 0.0) {
            sign = cross;
        } else if (sign * cross < 0.0) {
            return false;
        }
    }
    return sign != 0.0;
}

bool HasInvalidAdjacency(const SlotFrameTopologyEvidence &evidence)
{
    return std::any_of(evidence.edgeFaceCounts.cbegin(),
                       evidence.edgeFaceCounts.cend(),
                       [](int count) { return count <= 0 || count > 2; }) ||
           std::any_of(evidence.vertexEdgeCounts.cbegin(),
                       evidence.vertexEdgeCounts.cend(),
                       [](int count) { return count <= 0 || count > 2; });
}

SlotFrameComplementType ComplementType(
    const QVector<SlotFrameFreeEdge> &free_edges)
{
    if (free_edges.size() != 2) {
        return SlotFrameComplementType::None;
    }
    const QVector3D first = free_edges.at(0).end - free_edges.at(0).start;
    const QVector3D second = free_edges.at(1).end - free_edges.at(1).start;
    if (first.lengthSquared() <= 1.0e-10f ||
        second.lengthSquared() <= 1.0e-10f) {
        return SlotFrameComplementType::None;
    }
    const double parallel = std::abs(QVector3D::dotProduct(
        first.normalized(), second.normalized()));
    return parallel >= 0.98 ? SlotFrameComplementType::DirectBridge
                            : SlotFrameComplementType::PerpendicularBridge;
}

ContourFeature BuildMachiningGeometry(const MachiningFeature &source,
                                       SlotFrameKind kind)
{
    ContourFeature geometry;
    geometry.subType = kind == SlotFrameKind::OpenSlot
        ? QStringLiteral("open_slot") : source.subType;
    geometry.center = source.center;
    geometry.depth = source.depth;
    geometry.width = source.width;
    geometry.length = source.length;
    geometry.angle = source.angle;
    geometry.axis = source.axis;
    geometry.region = source.region;
    geometry.points = source.boundaryPoints;
    geometry.islands = source.islandBoundaries;
    if (kind == SlotFrameKind::OpenSlot) {
        geometry.openSide = -1.0;
    } else if (geometry.subType != QStringLiteral("rectangular_pocket")) {
        geometry.subType = QStringLiteral("irregular_pocket");
    }
    return geometry;
}

}  // namespace

SlotFrameRecognitionResult SlotFrameRecognizer::Recognize(
    const MachiningFeature &source,
    const SetupContext &setup,
    const SlotFrameTopologyEvidence &evidence)
{
    SlotFrameRecognitionResult result;
    SlotFrameFeature feature;
    feature.setup = setup;
    feature.axis = source.axis;
    feature.region = source.region;
    feature.depth = source.depth;
    feature.outerBoundary = source.boundaryPoints;
    feature.islands = source.islandBoundaries;
    feature.protectionRegions = evidence.protectionRegions;
    feature.freeEdges = evidence.freeEdges;

    if (source.kind != FeatureKind::Pocket && source.kind != FeatureKind::Slot) {
        result.reasons.append(QStringLiteral(
            "Selected geometry is not a slot or pocket feature."));
    }
    if (!setup.validate().isEmpty()) {
        result.reasons.append(QStringLiteral(
            "A confirmed Setup and work offset are required."));
    }
    if (!IsFrontZ(source)) {
        result.reasons.append(QStringLiteral(
            "Only front-face +Z slot frames are supported."));
    }
    if (!evidence.bottomPlanar || !evidence.sideWallsPlanar) {
        result.reasons.append(QStringLiteral(
            "The slot frame requires a planar bottom and straight planar walls."));
    }
    if (source.depth <= 0.0 || !IsCoplanar(source.boundaryPoints) ||
        !IsConvex(source.boundaryPoints)) {
        result.reasons.append(QStringLiteral(
            "The bottom boundary is degenerate, non-planar, or non-convex."));
    }
    if (evidence.intersecting) {
        result.reasons.append(QStringLiteral(
            "Intersecting slot-frame geometry is not supported."));
    }
    if (HasInvalidAdjacency(evidence)) {
        result.reasons.append(QStringLiteral(
            "The boundary contains non-manifold or disconnected adjacency."));
    }

    const bool open_slot = source.kind == FeatureKind::Slot &&
                           source.subType == QStringLiteral("open_slot");
    feature.kind = open_slot ? SlotFrameKind::OpenSlot
                             : SlotFrameKind::ClosedPocket;
    feature.closed = !open_slot;
    if (open_slot) {
        feature.complementType = ComplementType(evidence.freeEdges);
        if (evidence.freeEdges.size() != 2 ||
            feature.complementType == SlotFrameComplementType::None) {
            result.reasons.append(QStringLiteral(
                "An open slot requires exactly two explainable free ends."));
        }
    } else {
        if (!evidence.bottomBoundaryComplete || !evidence.freeEdges.isEmpty() ||
            std::any_of(evidence.edgeFaceCounts.cbegin(),
                        evidence.edgeFaceCounts.cend(),
                        [](int count) { return count != 2; })) {
            result.reasons.append(QStringLiteral(
                "A closed pocket requires one complete outer loop with no free edges."));
        }
    }

    feature.machiningGeometry = BuildMachiningGeometry(source, feature.kind);
    feature.geometryRef = QStringLiteral("slot-frame:") +
        stableContourId(feature.machiningGeometry).mid(
            QStringLiteral("contour:").size());
    feature.bottomFaceIndices = source.faceIndices.isEmpty()
        ? QVector<int>() : QVector<int>{source.faceIndices.first()};
    for (int index = 1; index < source.faceIndices.size(); ++index) {
        feature.sideFaceIndices.append(source.faceIndices.at(index));
    }
    feature.confidence = result.reasons.isEmpty() ? 0.95 : 0.0;
    feature.rejectionReasons = result.reasons;
    result.feature = feature;
    result.ok = result.reasons.isEmpty();
    return result;
}

#ifdef CNEXT_ENABLE_OCC
SlotFrameTopologyEvidence SlotFrameRecognizer::BuildEvidence(
    const TopoGraph &graph,
    const MachiningFeature &source)
{
    SlotFrameTopologyEvidence evidence;
    if (source.faceIndices.isEmpty()) {
        return evidence;
    }
    const int bottom_index = source.faceIndices.first();
    if (bottom_index <= 0 || bottom_index > graph.faceMap.Extent() ||
        bottom_index > graph.nodes.size()) {
        return evidence;
    }

    evidence.bottomPlanar =
        graph.nodes.at(bottom_index - 1).surfType == GeomAbs_Plane;
    evidence.sideWallsPlanar = true;
    for (int index = 1; index < source.faceIndices.size(); ++index) {
        const int face_index = source.faceIndices.at(index);
        if (face_index <= 0 || face_index > graph.nodes.size() ||
            graph.nodes.at(face_index - 1).surfType != GeomAbs_Plane) {
            evidence.sideWallsPlanar = false;
            break;
        }
    }

    TopoDS_Shape all_faces;
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (int face_index = 1; face_index <= graph.faceMap.Extent(); ++face_index) {
        builder.Add(compound, graph.faceMap(face_index));
    }
    all_faces = compound;

    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(
        all_faces, TopAbs_EDGE, TopAbs_FACE, edge_faces);
    const TopoDS_Face bottom = TopoDS::Face(graph.faceMap(bottom_index));
    const TopoDS_Wire outer = BRepTools::OuterWire(bottom);
    if (outer.IsNull()) {
        return evidence;
    }
    evidence.bottomBoundaryComplete = outer.Closed();

    TopTools_IndexedDataMapOfShapeListOfShape vertex_edges;
    TopExp::MapShapesAndAncestors(
        outer, TopAbs_VERTEX, TopAbs_EDGE, vertex_edges);
    for (int vertex_index = 1; vertex_index <= vertex_edges.Extent(); ++vertex_index) {
        evidence.vertexEdgeCounts.append(
            vertex_edges.FindFromIndex(vertex_index).Extent());
    }

    for (TopExp_Explorer explorer(outer, TopAbs_EDGE);
         explorer.More(); explorer.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
        const int face_count = edge_faces.Contains(edge)
            ? edge_faces.FindFromKey(edge).Extent() : 0;
        evidence.edgeFaceCounts.append(face_count);
        if (face_count == 1) {
            BRepAdaptor_Curve curve(edge);
            const gp_Pnt start = curve.Value(curve.FirstParameter());
            const gp_Pnt end = curve.Value(curve.LastParameter());
            evidence.freeEdges.append({
                QVector3D(float(start.X()), float(start.Y()), float(start.Z())),
                QVector3D(float(end.X()), float(end.Y()), float(end.Z())),
                face_count});
        }
    }
    evidence.protectionRegions = source.islandBoundaries;
    return evidence;
}
#endif
