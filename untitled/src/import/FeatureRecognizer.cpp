#include "FeatureRecognizer.h"

#ifdef CNEXT_ENABLE_OCC
#include "FeatureClassifier.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Torus.hxx>

#include <QSet>
#include <QDebug>
#include <QHash>
#include <algorithm>
#include <cmath>

namespace {

static double dotDir(const gp_Dir &a, const gp_Dir &b)
{
    return a.X() * b.X() + a.Y() * b.Y() + a.Z() * b.Z();
}

static QVector3D toVector3D(const gp_Dir &dir)
{
    return QVector3D(float(dir.X()), float(dir.Y()), float(dir.Z()));
}

static gp_Pnt pointOnAxisAtProjection(const gp_Ax1 &axis, double targetProj)
{
    const gp_Pnt origin = axis.Location();
    const gp_Dir dir = axis.Direction();
    const double originProj = origin.X() * dir.X() + origin.Y() * dir.Y() + origin.Z() * dir.Z();
    return gp_Pnt(origin.X() + dir.X() * (targetProj - originProj),
                  origin.Y() + dir.Y() * (targetProj - originProj),
                  origin.Z() + dir.Z() * (targetProj - originProj));
}

struct FaceProjection {
    bool valid = false;
    double minProj = 0.0;
    double maxProj = 0.0;
    double avgProj = 0.0;
};

static FaceProjection estimateFaceProjection(const TopoDS_Face &face, const gp_Dir &axis)
{
    FaceProjection projection;
    TopLoc_Location loc;
    Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
    if (tri.IsNull() || tri->NbTriangles() <= 0) {
        return projection;
    }

    const gp_Trsf trsf = loc.Transformation();
    double minProj = 1e100;
    double maxProj = -1e100;
    double projAcc = 0.0;
    int projCount = 0;

    for (Standard_Integer i = 1; i <= tri->NbTriangles(); ++i) {
        Standard_Integer n1 = 0;
        Standard_Integer n2 = 0;
        Standard_Integer n3 = 0;
        tri->Triangle(i).Get(n1, n2, n3);

        const gp_Pnt pts[] = {
            tri->Node(n1).Transformed(trsf),
            tri->Node(n2).Transformed(trsf),
            tri->Node(n3).Transformed(trsf)
        };
        for (const gp_Pnt &p : pts) {
            const double proj = p.X() * axis.X() + p.Y() * axis.Y() + p.Z() * axis.Z();
            minProj = std::min(minProj, proj);
            maxProj = std::max(maxProj, proj);
            projAcc += proj;
            ++projCount;
        }
    }

    projection.valid = projCount > 0;
    if (projection.valid) {
        projection.minProj = minProj;
        projection.maxProj = maxProj;
        projection.avgProj = projAcc / double(projCount);
    }
    return projection;
}

static double cylinderRadialNormalAlignment(const TopoDS_Face &face, const gp_Ax1 &axis)
{
    TopLoc_Location loc;
    Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
    if (tri.IsNull() || tri->NbTriangles() <= 0) {
        return 0.0;
    }

    const gp_Trsf trsf = loc.Transformation();
    const gp_Pnt axisOrigin = axis.Location();
    const gp_Dir axisDir = axis.Direction();
    double dotAcc = 0.0;
    int dotCount = 0;

    for (Standard_Integer i = 1; i <= tri->NbTriangles(); ++i) {
        Standard_Integer n1 = 0;
        Standard_Integer n2 = 0;
        Standard_Integer n3 = 0;
        tri->Triangle(i).Get(n1, n2, n3);
        const gp_Pnt p1 = tri->Node(n1).Transformed(trsf);
        const gp_Pnt p2 = tri->Node(n2).Transformed(trsf);
        const gp_Pnt p3 = tri->Node(n3).Transformed(trsf);

        gp_XYZ normal = (p2.XYZ() - p1.XYZ()).Crossed(p3.XYZ() - p1.XYZ());
        if (normal.Modulus() <= 1.0e-12) {
            continue;
        }
        normal.Normalize();

        const gp_Pnt centroid((p1.X() + p2.X() + p3.X()) / 3.0,
                              (p1.Y() + p2.Y() + p3.Y()) / 3.0,
                              (p1.Z() + p2.Z() + p3.Z()) / 3.0);
        const gp_XYZ rel = centroid.XYZ() - axisOrigin.XYZ();
        const double axial = rel.X() * axisDir.X() + rel.Y() * axisDir.Y() + rel.Z() * axisDir.Z();
        gp_XYZ radial = rel - gp_XYZ(axisDir.X() * axial, axisDir.Y() * axial, axisDir.Z() * axial);
        if (radial.Modulus() <= 1.0e-12) {
            continue;
        }
        radial.Normalize();

        dotAcc += radial.Dot(normal);
        ++dotCount;
    }

    return dotCount > 0 ? dotAcc / double(dotCount) : 0.0;
}

struct FaceBox2D {
    bool valid = false;
    QVector3D center;
    QVector3D normal;
    QVector3D majorAxis;
    QVector3D minorAxis;
    double width = 0.0;
    double length = 0.0;
    double angle = 0.0;  // orientation of the long axis in XY plane (degrees, 0=X axis)
};

static FaceBox2D buildPlanarFaceBox(const QVector<QVector3D> &points, const QVector3D &normalHint)
{
    FaceBox2D box;
    if (points.isEmpty()) {
        return box;
    }

    QVector3D normal = normalHint.normalized();
    if (normal.lengthSquared() <= 1.0e-8f) {
        return box;
    }

    QVector3D ref = std::abs(normal.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(1, 0, 0);
    QVector3D u = QVector3D::crossProduct(normal, ref).normalized();
    QVector3D v = QVector3D::crossProduct(normal, u).normalized();
    if (u.lengthSquared() <= 1.0e-8f || v.lengthSquared() <= 1.0e-8f) {
        return box;
    }

    QVector3D centerAccum;
    for (const QVector3D &q : points) {
        centerAccum += q;
    }

    box.center = centerAccum / float(points.size());
    box.normal = normal;

    double cxx = 0.0, cxy = 0.0, cyy = 0.0;
    for (const QVector3D &q : points) {
        const QVector3D rel = q - box.center;
        const double dx = QVector3D::dotProduct(rel, u);
        const double dy = QVector3D::dotProduct(rel, v);
        cxx += dx * dx;
        cxy += dx * dy;
        cyy += dy * dy;
    }

    const double trace  = cxx + cyy;
    const double det    = cxx * cyy - cxy * cxy;
    const double disc   = std::sqrt(std::max(0.0, trace * trace / 4.0 - det));
    const double lambda = trace / 2.0 + disc;  // larger eigenvalue
    double ex = cxy;
    double ey = lambda - cxx;
    const double elen = std::sqrt(ex * ex + ey * ey);
    if (elen > 1e-12) {
        ex /= elen;
        ey /= elen;
    } else {
        ex = 1.0;
        ey = 0.0;
    }

    box.majorAxis = (u * float(ex) + v * float(ey)).normalized();
    if (box.majorAxis.lengthSquared() <= 1.0e-8f) {
        box.majorAxis = u;
    }
    box.minorAxis = QVector3D::crossProduct(normal, box.majorAxis).normalized();
    if (box.minorAxis.lengthSquared() <= 1.0e-8f) {
        box.minorAxis = v;
    }

    double minMajor = 1e100, maxMajor = -1e100;
    double minMinor = 1e100, maxMinor = -1e100;
    for (const QVector3D &q : points) {
        const double majorProj = QVector3D::dotProduct(q, box.majorAxis);
        const double minorProj = QVector3D::dotProduct(q, box.minorAxis);
        minMajor = std::min(minMajor, majorProj);
        maxMajor = std::max(maxMajor, majorProj);
        minMinor = std::min(minMinor, minorProj);
        maxMinor = std::max(maxMinor, minorProj);
    }

    box.length = maxMajor - minMajor;
    box.width = maxMinor - minMinor;
    if (box.width > box.length) {
        std::swap(box.width, box.length);
        std::swap(box.majorAxis, box.minorAxis);
    }

    box.valid = true;
    box.angle = std::atan2(box.majorAxis.y(), box.majorAxis.x()) * (180.0 / std::acos(-1.0));

    return box;
}

static FaceBox2D estimatePlanarFaceBox(const TopoDS_Face &face, const QVector3D &normalHint)
{
    QVector<QVector3D> points;
    TopLoc_Location loc;
    Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
    if (tri.IsNull() || tri->NbTriangles() <= 0) {
        return {};
    }

    const gp_Trsf trsf = loc.Transformation();
    for (Standard_Integer i = 1; i <= tri->NbNodes(); ++i) {
        const gp_Pnt p = tri->Node(i).Transformed(trsf);
        points.append(QVector3D(float(p.X()), float(p.Y()), float(p.Z())));
    }

    return buildPlanarFaceBox(points, normalHint);
}

static double radialDistanceToAxis(const QVector3D &point, const gp_Ax1 &axis)
{
    const gp_Pnt origin = axis.Location();
    const gp_Dir dir = axis.Direction();
    const QVector3D axisOrigin(float(origin.X()), float(origin.Y()), float(origin.Z()));
    const QVector3D axisDir(float(dir.X()), float(dir.Y()), float(dir.Z()));
    const QVector3D rel = point - axisOrigin;
    const QVector3D radial = rel - axisDir * QVector3D::dotProduct(rel, axisDir);
    return radial.length();
}

static bool isCompactPlanarCap(const TopoGraph &graph,
                               int faceIndex,
                               const gp_Ax1 &axis,
                               double radius)
{
    if (faceIndex <= 0 || faceIndex > graph.nodes.size() || radius <= 0.0) {
        return false;
    }

    const FaceNode &node = graph.nodes[faceIndex - 1];
    if (node.surfType != GeomAbs_Plane) {
        return false;
    }

    const QVector3D axisDir(float(axis.Direction().X()),
                            float(axis.Direction().Y()),
                            float(axis.Direction().Z()));
    const double normalAlignment = std::abs(QVector3D::dotProduct(node.normal.normalized(), axisDir.normalized()));
    if (normalAlignment < 0.90) {
        return false;
    }

    // Compact planar caps should have a single boundary loop; inner loops indicate pockets or annular faces.
    {
        int wireCount = 0;
        for (TopExp_Explorer ex(TopoDS::Face(graph.faceMap(faceIndex)), TopAbs_WIRE); ex.More(); ex.Next())
            ++wireCount;
        if (wireCount > 1)
            return false;
    }

    const FaceBox2D box = estimatePlanarFaceBox(TopoDS::Face(graph.faceMap(faceIndex)), node.normal);
    if (!box.valid) {
        return false;
    }

    const double diameter = radius * 2.0;
    const double maxCapSpan = diameter * 1.35;
    if (box.length > maxCapSpan || box.width > maxCapSpan) {
        return false;
    }

    return radialDistanceToAxis(box.center, axis) <= std::max(radius * 0.35, 0.25);
}

static bool faceHasInnerWires(const TopoDS_Face &face)
{
    // Inner wires (holes) have REVERSED orientation relative to the face.
    // Outer contours are FORWARD. Arc-ended slots may have multiple FORWARD
    // wires (arc + straight segments split by STEP), so only reject when
    // a true inner (hole) wire is present.
    for (TopExp_Explorer explorer(face, TopAbs_WIRE); explorer.More(); explorer.Next()) {
        const TopoDS_Shape &wire = explorer.Current();
        if (wire.Orientation() == TopAbs_REVERSED) {
            return true;
        }
    }
    return false;
}

static void appendDistinctPoint(QVector<QVector3D> &points, const gp_Pnt &point)
{
    const QVector3D value(float(point.X()), float(point.Y()), float(point.Z()));
    if (points.isEmpty() || (points.last() - value).lengthSquared() > 1.0e-10f) {
        points.append(value);
    }
}

static QVector<QVector3D> sampleBoundaryWire(const TopoDS_Wire &wire)
{
    QVector<QVector3D> points;
    for (BRepTools_WireExplorer explorer(wire); explorer.More(); explorer.Next()) {
        const TopoDS_Edge edge = explorer.Current();
        BRepAdaptor_Curve curve(edge);
        double first = curve.FirstParameter();
        double last = curve.LastParameter();
        if (!std::isfinite(first) || !std::isfinite(last)) {
            continue;
        }
        if (edge.Orientation() == TopAbs_REVERSED) {
            std::swap(first, last);
        }
        int segmentCount = 1;
        if (curve.GetType() == GeomAbs_Circle || curve.GetType() == GeomAbs_Ellipse) {
            segmentCount = 24;
        } else if (curve.GetType() != GeomAbs_Line) {
            segmentCount = 12;
        }
        for (int segment = 0; segment <= segmentCount; ++segment) {
            const double t = first + (last - first) * double(segment) / double(segmentCount);
            appendDistinctPoint(points, curve.Value(t));
        }
    }
    if (points.size() >= 2 &&
        (points.first() - points.last()).lengthSquared() <= 1.0e-10f) {
        points.removeLast();
    }
    return points;
}

static void extractPocketLoops(const TopoDS_Face &face,
                               QVector<QVector3D> &outerBoundary,
                               QVector<QVector<QVector3D>> &islandBoundaries)
{
    outerBoundary.clear();
    islandBoundaries.clear();
    const TopoDS_Wire outerWire = BRepTools::OuterWire(face);
    if (outerWire.IsNull()) {
        return;
    }
    outerBoundary = sampleBoundaryWire(outerWire);
    for (TopExp_Explorer explorer(face, TopAbs_WIRE); explorer.More(); explorer.Next()) {
        const TopoDS_Wire wire = TopoDS::Wire(explorer.Current());
        if (wire.IsSame(outerWire)) {
            continue;
        }
        const QVector<QVector3D> island = sampleBoundaryWire(wire);
        if (island.size() >= 3) {
            islandBoundaries.append(island);
        }
    }
}

static QVector3D estimateFaceCentroid(const TopoDS_Face &face);

static double estimateSideFeatureSpan(const TopoGraph &graph, const QVector<int> &sideFaces)
{
    double span = 0.0;
    for (int sideIndex : sideFaces) {
        if (sideIndex <= 0 || sideIndex > graph.nodes.size()) {
            continue;
        }

        const FaceNode &sideNode = graph.nodes[sideIndex - 1];
        if (sideNode.surfType == GeomAbs_Cylinder && sideNode.radius > 0.0) {
            span = std::max(span, sideNode.radius * 2.0);
            continue;
        }
        if (sideNode.surfType == GeomAbs_Plane) {
            const FaceBox2D sideBox = estimatePlanarFaceBox(TopoDS::Face(graph.faceMap(sideIndex)), sideNode.normal);
            if (sideBox.valid) {
                span = std::max(span, sideBox.length);
            }
        }
    }
    return span;
}

static bool isSlotSideLayoutConsistent(const TopoGraph &graph,
                                       const QVector<int> &sideFaces,
                                       const FaceBox2D &bottomBox)
{
    if (!bottomBox.valid || sideFaces.size() < 2) {
        return false;
    }
    if (bottomBox.minorAxis.lengthSquared() <= 1.0e-8f || bottomBox.width <= 1.0e-6) {
        return false;
    }

    const double minorHalf = bottomBox.width * 0.5;
    const double edgeTol = std::max(0.5, bottomBox.width * 0.35);
    bool hasPositiveMinorWall = false;
    bool hasNegativeMinorWall = false;
    int nearMinorEdgeCount = 0;

    for (int sideIndex : sideFaces) {
        if (sideIndex <= 0 || sideIndex > graph.faceMap.Extent()) {
            continue;
        }
        const QVector3D sideCenter = estimateFaceCentroid(TopoDS::Face(graph.faceMap(sideIndex)));
        const double minorProj = QVector3D::dotProduct(sideCenter - bottomBox.center, bottomBox.minorAxis);
        if (minorProj >= 0.0) {
            hasPositiveMinorWall = true;
        } else {
            hasNegativeMinorWall = true;
        }
        if (std::abs(std::abs(minorProj) - minorHalf) <= edgeTol) {
            ++nearMinorEdgeCount;
        }
    }

    return hasPositiveMinorWall && hasNegativeMinorWall && nearMinorEdgeCount >= 2;
}

static QVector<int> collectBoundaryWallFaces(const TopoGraph &graph,
                                             const QVector<int> &bottomFaces,
                                             const QSet<int> &candidateSideFaces)
{
    QVector<int> wallFaces;
    if (bottomFaces.isEmpty() || candidateSideFaces.isEmpty()) {
        return wallFaces;
    }

    QHash<int, int> boundaryHits;
    for (int bottomFaceIndex : bottomFaces) {
        if (bottomFaceIndex <= 0 || bottomFaceIndex > graph.faceMap.Extent()) {
            continue;
        }

        const TopoDS_Face bottomFace = TopoDS::Face(graph.faceMap(bottomFaceIndex));
        TopoDS_Wire outerWire = BRepTools::OuterWire(bottomFace);
        if (outerWire.IsNull()) {
            continue;
        }

        for (TopExp_Explorer edgeEx(outerWire, TopAbs_EDGE); edgeEx.More(); edgeEx.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(edgeEx.Current());
            int matchedNeighbor = 0;

            for (const auto &entry : graph.aag.value(bottomFaceIndex)) {
                const int neighborIndex = entry.first;
                const EdgeAttr &edgeAttr = entry.second;
                if (!edgeAttr.isConcave || !candidateSideFaces.contains(neighborIndex)) {
                    continue;
                }

                const TopoDS_Face neighborFace = TopoDS::Face(graph.faceMap(neighborIndex));
                bool sharesEdge = false;
                for (TopExp_Explorer neighborEdgeEx(neighborFace, TopAbs_EDGE);
                     neighborEdgeEx.More(); neighborEdgeEx.Next()) {
                    if (edge.IsSame(neighborEdgeEx.Current())) {
                        sharesEdge = true;
                        break;
                    }
                }
                if (!sharesEdge) {
                    continue;
                }

                matchedNeighbor = neighborIndex;
                break;
            }

            if (matchedNeighbor > 0) {
                boundaryHits[matchedNeighbor] += 1;
            }
        }
    }

    QSet<int> seen;
    for (auto it = boundaryHits.cbegin(); it != boundaryHits.cend(); ++it) {
        if (it.value() <= 0 || seen.contains(it.key())) {
            continue;
        }
        seen.insert(it.key());
        wallFaces.append(it.key());
    }
    return wallFaces;
}

static QVector<int> selectSlotWallFaces(const TopoGraph &graph,
                                        const QVector<int> &bottomFaces,
                                        const QVector<int> &sideFaces,
                                        const FaceBox2D &bottomBox)
{
    QVector<int> wallFaces;
    if (!bottomBox.valid || bottomBox.minorAxis.lengthSquared() <= 1.0e-8f) {
        return wallFaces;
    }

    const QSet<int> candidateSet(sideFaces.cbegin(), sideFaces.cend());
    const QVector<int> boundaryWalls = collectBoundaryWallFaces(graph, bottomFaces, candidateSet);
    if (!boundaryWalls.isEmpty()) {
        wallFaces = boundaryWalls;
    }

    const double minorHalf = bottomBox.width * 0.5;
    const double edgeTol = std::max(0.5, bottomBox.width * 0.35);
    const QVector<int> filterSource = wallFaces.isEmpty() ? sideFaces : wallFaces;
    QVector<int> filteredWalls;
    for (int sideIndex : filterSource) {
        if (sideIndex <= 0 || sideIndex > graph.faceMap.Extent()) {
            continue;
        }
        const QVector3D sideCenter = estimateFaceCentroid(TopoDS::Face(graph.faceMap(sideIndex)));
        const double minorProj = QVector3D::dotProduct(sideCenter - bottomBox.center, bottomBox.minorAxis);
        if (std::abs(std::abs(minorProj) - minorHalf) <= edgeTol) {
            filteredWalls.append(sideIndex);
        }
    }

    QSet<int> seen;
    QVector<int> uniqueWalls;
    const QVector<int> dedupeSource = filteredWalls.isEmpty() ? wallFaces : filteredWalls;
    for (int faceIndex : dedupeSource) {
        if (seen.contains(faceIndex)) {
            continue;
        }
        seen.insert(faceIndex);
        uniqueWalls.append(faceIndex);
    }
    return uniqueWalls;
}

static double estimateFacesSpanAlongDirection(const TopoGraph &graph,
                                              const QVector<int> &faceIndices,
                                              const QVector3D &direction)
{
    const QVector3D dir = direction.normalized();
    if (dir.lengthSquared() <= 1.0e-8f) {
        return 0.0;
    }

    double minProj = 1e100;
    double maxProj = -1e100;
    bool hasPoint = false;
    for (int faceIndex : faceIndices) {
        if (faceIndex <= 0 || faceIndex > graph.faceMap.Extent()) {
            continue;
        }
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(TopoDS::Face(graph.faceMap(faceIndex)), loc);
        if (tri.IsNull() || tri->NbNodes() <= 0) {
            continue;
        }
        const gp_Trsf trsf = loc.Transformation();
        for (Standard_Integer i = 1; i <= tri->NbNodes(); ++i) {
            const gp_Pnt p = tri->Node(i).Transformed(trsf);
            const QVector3D q(float(p.X()), float(p.Y()), float(p.Z()));
            const double proj = QVector3D::dotProduct(q, dir);
            minProj = std::min(minProj, proj);
            maxProj = std::max(maxProj, proj);
            hasPoint = true;
        }
    }

    return hasPoint ? (maxProj - minProj) : 0.0;
}

struct ProjectionRange {
    bool valid = false;
    double minProj = 0.0;
    double maxProj = 0.0;
};

static ProjectionRange estimateModelProjectionRange(const TopoGraph &graph, const QVector3D &direction)
{
    ProjectionRange range;
    const QVector3D dir = direction.normalized();
    if (dir.lengthSquared() <= 1.0e-8f) {
        return range;
    }

    double minProj = 1e100;
    double maxProj = -1e100;
    bool hasPoint = false;
    for (int faceIndex = 1; faceIndex <= graph.faceMap.Extent(); ++faceIndex) {
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(TopoDS::Face(graph.faceMap(faceIndex)), loc);
        if (tri.IsNull() || tri->NbNodes() <= 0) {
            continue;
        }
        const gp_Trsf trsf = loc.Transformation();
        for (Standard_Integer i = 1; i <= tri->NbNodes(); ++i) {
            const gp_Pnt p = tri->Node(i).Transformed(trsf);
            const QVector3D q(float(p.X()), float(p.Y()), float(p.Z()));
            const double proj = QVector3D::dotProduct(q, dir);
            minProj = std::min(minProj, proj);
            maxProj = std::max(maxProj, proj);
            hasPoint = true;
        }
    }

    range.valid = hasPoint;
    if (range.valid) {
        range.minProj = minProj;
        range.maxProj = maxProj;
    }
    return range;
}

static ProjectionRange estimateFacesProjectionRange(const TopoGraph &graph,
                                                    const QVector<int> &faceIndices,
                                                    const QVector3D &direction)
{
    ProjectionRange range;
    const QVector3D dir = direction.normalized();
    if (dir.lengthSquared() <= 1.0e-8f || faceIndices.isEmpty()) {
        return range;
    }

    double minProj = 1e100;
    double maxProj = -1e100;
    bool hasPoint = false;
    for (int faceIndex : faceIndices) {
        if (faceIndex <= 0 || faceIndex > graph.faceMap.Extent()) {
            continue;
        }
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(TopoDS::Face(graph.faceMap(faceIndex)), loc);
        if (tri.IsNull() || tri->NbNodes() <= 0) {
            continue;
        }
        const gp_Trsf trsf = loc.Transformation();
        for (Standard_Integer i = 1; i <= tri->NbNodes(); ++i) {
            const gp_Pnt p = tri->Node(i).Transformed(trsf);
            const QVector3D q(float(p.X()), float(p.Y()), float(p.Z()));
            const double proj = QVector3D::dotProduct(q, dir);
            minProj = std::min(minProj, proj);
            maxProj = std::max(maxProj, proj);
            hasPoint = true;
        }
    }

    range.valid = hasPoint;
    if (range.valid) {
        range.minProj = minProj;
        range.maxProj = maxProj;
    }
    return range;
}

static FaceRegion slotOpeningRegionFromFaceRange(const TopoGraph &graph,
                                                 const MachiningFeature &feature,
                                                 const QVector3D &frontNormal)
{
    const QVector3D fn = frontNormal.normalized();
    if (fn.lengthSquared() <= 1.0e-8f || feature.faceIndices.isEmpty()) {
        return FaceRegion::Unknown;
    }

    const ProjectionRange modelRange = estimateModelProjectionRange(graph, fn);
    const ProjectionRange featureRange = estimateFacesProjectionRange(graph, feature.faceIndices, fn);
    if (!modelRange.valid || !featureRange.valid) {
        return FaceRegion::Unknown;
    }

    const double featureSpan = featureRange.maxProj - featureRange.minProj;
    const double tolerance = std::max(0.35, std::max(feature.depth, feature.width) * 0.15);
    if (featureSpan > 0.05 &&
        std::abs(modelRange.maxProj - featureRange.maxProj) <= tolerance) {
        return FaceRegion::Front;
    }
    if (featureSpan > 0.05 &&
        std::abs(featureRange.minProj - modelRange.minProj) <= tolerance) {
        return FaceRegion::Back;
    }
    return FaceRegion::Unknown;
}

static bool isNearModelBoundary(const TopoGraph &graph, const QVector3D &point, const QVector3D &axis)
{
    const ProjectionRange range = estimateModelProjectionRange(graph, axis);
    if (!range.valid) {
        return false;
    }

    const QVector3D dir = axis.normalized();
    const double span = range.maxProj - range.minProj;
    const double tol = std::max(0.25, span * 0.02);
    const double proj = QVector3D::dotProduct(point, dir);
    return (proj - range.minProj) <= tol || (range.maxProj - proj) <= tol;
}

static QVector<int> collectConcaveConnectedPlanarFaces(const TopoGraph &graph, int seedFaceIndex)
{
    QVector<int> group;
    if (seedFaceIndex <= 0 || seedFaceIndex > graph.nodes.size()) {
        return group;
    }
    if (graph.nodes[seedFaceIndex - 1].surfType != GeomAbs_Plane) {
        return group;
    }

    QSet<int> visited;
    QVector<int> stack = { seedFaceIndex };
    visited.insert(seedFaceIndex);

    while (!stack.isEmpty()) {
        const int faceIndex = stack.takeLast();
        group.append(faceIndex);
        for (const auto &entry : graph.aag.value(faceIndex)) {
            const int neighborIndex = entry.first;
            const EdgeAttr &edgeAttr = entry.second;
            if (visited.contains(neighborIndex) || !edgeAttr.isConcave) {
                continue;
            }
            if (neighborIndex <= 0 || neighborIndex > graph.nodes.size()) {
                continue;
            }
            if (graph.nodes[neighborIndex - 1].surfType != GeomAbs_Plane) {
                continue;
            }
            visited.insert(neighborIndex);
            stack.append(neighborIndex);
        }
    }

    QSet<int> seen;
    QVector<int> uniqueGroup;
    for (int faceIndex : group) {
        if (seen.contains(faceIndex)) {
            continue;
        }
        seen.insert(faceIndex);
        uniqueGroup.append(faceIndex);
    }
    return uniqueGroup;
}

static bool hasPlanarBottomNeighborForWallGroup(const TopoGraph &graph,
                                                const QVector<int> &wallFaces,
                                                const QVector3D &openingAxis)
{
    if (openingAxis.lengthSquared() <= 1.0e-8f) {
        return false;
    }

    QSet<int> wallSet;
    for (int faceIndex : wallFaces) {
        wallSet.insert(faceIndex);
    }

    QMap<int, int> touchCount;
    for (int wallFace : wallFaces) {
        for (const auto &entry : graph.aag.value(wallFace)) {
            const int neighborIndex = entry.first;
            if (wallSet.contains(neighborIndex) || !entry.second.isConcave) {
                continue;
            }
            if (neighborIndex <= 0 || neighborIndex > graph.nodes.size()) {
                continue;
            }
            const FaceNode &neighborNode = graph.nodes[neighborIndex - 1];
            if (neighborNode.surfType != GeomAbs_Plane) {
                continue;
            }
            const QVector3D normal = neighborNode.normal.normalized();
            if (normal.lengthSquared() <= 1.0e-8f) {
                continue;
            }
            if (std::abs(QVector3D::dotProduct(normal, openingAxis.normalized())) < 0.9f) {
                continue;
            }
            touchCount[neighborIndex] += 1;
        }
    }

    for (auto it = touchCount.cbegin(); it != touchCount.cend(); ++it) {
        if (it.value() >= 2) {
            return true;
        }
    }
    return false;
}

static bool isReasonableRecessBottom(const TopoGraph &graph,
                                     int bottomFace,
                                     const QVector<int> &sideFaces,
                                     const FaceBox2D &bottomBox,
                                     double maxBottomToSideRatio)
{
    if (!bottomBox.valid || sideFaces.isEmpty()) {
        return false;
    }

    const TopoDS_Face face = TopoDS::Face(graph.faceMap(bottomFace));
    if (faceHasInnerWires(face)) {
        return false;
    }

    const double sideSpan = estimateSideFeatureSpan(graph, sideFaces);
    if (sideSpan <= 1.0e-6) {
        return true;
    }

    const double allowedSpan = sideSpan * maxBottomToSideRatio;
    return bottomBox.length <= allowedSpan;
}

static QVector<int> uniqueIndices(const QVector<int> &indices)
{
    QSet<int> seen;
    QVector<int> result;
    result.reserve(indices.size());
    for (int index : indices) {
        if (!seen.contains(index)) {
            seen.insert(index);
            result.append(index);
        }
    }
    return result;
}

static QVector3D estimateFaceCentroid(const TopoDS_Face &face)
{
    TopLoc_Location loc;
    Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
    if (tri.IsNull() || tri->NbNodes() <= 0) {
        return {};
    }

    const gp_Trsf trsf = loc.Transformation();
    QVector3D centerAccum;
    int pointCount = 0;
    for (Standard_Integer i = 1; i <= tri->NbNodes(); ++i) {
        const gp_Pnt p = tri->Node(i).Transformed(trsf);
        centerAccum += QVector3D(float(p.X()), float(p.Y()), float(p.Z()));
        ++pointCount;
    }
    return pointCount > 0 ? centerAccum / float(pointCount) : QVector3D();
}

static QString planarSurfaceSubtype(FaceRegion region, double z, double minZ, double maxZ)
{
    constexpr double zTol = 1.0e-3;
    if (region == FaceRegion::Front && std::abs(z - maxZ) <= zTol) {
        return QStringLiteral("top_surface");
    }
    if (region == FaceRegion::Back && std::abs(z - minZ) <= zTol) {
        return QStringLiteral("bottom_surface");
    }
    if (region == FaceRegion::Side) {
        return QStringLiteral("side_surface");
    }
    return QStringLiteral("step_surface");
}

static int regionPriority(FaceRegion region)
{
    switch (region) {
    case FaceRegion::Front: return 3;
    case FaceRegion::Back:  return 2;
    case FaceRegion::Side:  return 1;
    case FaceRegion::Unknown:
    default:
        return 0;
    }
}

static FaceRegion regionFromAxis(const QVector3D &axis, const QVector3D &frontNormal)
{
    const QVector3D n = axis.normalized();
    if (n.lengthSquared() <= 1.0e-8f) {
        return FaceRegion::Unknown;
    }
    const float proj = QVector3D::dotProduct(n, frontNormal.normalized());
    if (std::abs(proj) >= 0.65f) {
        return proj >= 0.0f ? FaceRegion::Front : FaceRegion::Back;
    }
    return FaceRegion::Side;
}

// Returns "side_x" if hole axis is more aligned to X, "side_y" if more to Y.
// Only meaningful when regionFromAxis returns Side.
static QString sideDirectionSubtype(const QVector3D &axis)
{
    const QVector3D n = axis.normalized();
    return std::abs(n.x()) >= std::abs(n.y())
               ? QStringLiteral("side_x")
               : QStringLiteral("side_y");
}

static FaceRegion dominantFaceRegion(const TopoGraph &graph, const QVector<int> &faceIndices)
{
    int front = 0;
    int side = 0;
    int back = 0;
    for (int faceIndex : faceIndices) {
        if (faceIndex <= 0 || faceIndex > graph.nodes.size()) {
            continue;
        }
        switch (graph.nodes[faceIndex - 1].region) {
        case FaceRegion::Front: ++front; break;
        case FaceRegion::Side:  ++side;  break;
        case FaceRegion::Back:  ++back;  break;
        case FaceRegion::Unknown:
        default:
            break;
        }
    }

    if (front == 0 && side == 0 && back == 0) {
        return FaceRegion::Unknown;
    }
    if (front >= side && front >= back) {
        return FaceRegion::Front;
    }
    if (back >= side && back >= front) {
        return FaceRegion::Back;
    }
    return FaceRegion::Side;
}

static bool graphProjectionRange(const TopoGraph &graph,
                                 const QVector3D &direction,
                                 double &minProj,
                                 double &maxProj)
{
    minProj =  std::numeric_limits<double>::max();
    maxProj = -std::numeric_limits<double>::max();
    const QVector3D n = direction.normalized();
    if (n.lengthSquared() <= 1.0e-8f) {
        return false;
    }

    for (int faceIndex = 1; faceIndex <= graph.faceMap.Extent(); ++faceIndex) {
        const QVector3D center = estimateFaceCentroid(TopoDS::Face(graph.faceMap(faceIndex)));
        const double proj = QVector3D::dotProduct(center, n);
        minProj = std::min(minProj, proj);
        maxProj = std::max(maxProj, proj);
    }
    return minProj <= maxProj;
}

static FaceRegion machiningRegionForFeature(const TopoGraph &graph,
                                            const MachiningFeature &feature,
                                            const QVector3D &frontNormal)
{
    if (feature.kind == FeatureKind::FlatSurface && feature.region != FaceRegion::Unknown) {
        return feature.region;
    }

    FaceRegion axisRegion = regionFromAxis(feature.axis, frontNormal);
    FaceRegion faceRegion = dominantFaceRegion(graph, feature.faceIndices);

    if (feature.kind == FeatureKind::Hole ||
        feature.kind == FeatureKind::Thread ||
        feature.kind == FeatureKind::Boss) {
        if (feature.subType.contains(QStringLiteral("through_hole"))) {
            if (axisRegion == FaceRegion::Side) {
                return FaceRegion::Side;
            }

            double minProjection = 0.0;
            double maxProjection = 0.0;
            const QVector3D setupNormal = frontNormal.normalized();
            if (setupNormal.lengthSquared() > 1.0e-8f &&
                graphProjectionRange(graph,
                                     setupNormal,
                                     minProjection,
                                     maxProjection)) {
                const double centerProjection =
                    QVector3D::dotProduct(feature.center, setupNormal);
                const double distanceToFront =
                    std::abs(maxProjection - centerProjection);
                const double distanceToBack =
                    std::abs(centerProjection - minProjection);
                return distanceToFront <= distanceToBack
                    ? FaceRegion::Front
                    : FaceRegion::Back;
            }
        }

        // Hole-like features are machined from the opening side opposite to the feature axis.
        const FaceRegion openingRegion = regionFromAxis(-feature.axis, frontNormal);
        if (openingRegion != FaceRegion::Unknown) {
            return openingRegion;
        }
        if (axisRegion != FaceRegion::Unknown) {
            return axisRegion;
        }
    }

    if (feature.kind == FeatureKind::Slot ||
        feature.kind == FeatureKind::Pocket ||
        feature.kind == FeatureKind::Chamfer ||
        feature.kind == FeatureKind::Fillet) {
        if (feature.kind == FeatureKind::Slot && !feature.faceIndices.isEmpty()) {
            const FaceRegion openingRegion =
                slotOpeningRegionFromFaceRange(graph, feature, frontNormal);
            if (openingRegion != FaceRegion::Unknown) {
                return openingRegion;
            }

            const QVector3D fn = frontNormal.normalized();
            const QVector3D slotAxis = feature.axis.normalized();
            const double axisAlignment = std::abs(double(QVector3D::dotProduct(slotAxis, fn)));
            if (fn.lengthSquared() > 1.0e-8f &&
                slotAxis.lengthSquared() > 1.0e-8f &&
                axisAlignment >= 0.65 &&
                feature.depth > 0.0) {
                double minProj = 0.0;
                double maxProj = 0.0;
                if (graphProjectionRange(graph, fn, minProj, maxProj)) {
                    const double bottomProj = QVector3D::dotProduct(feature.center, fn);
                    const double frontOpeningProj = bottomProj + feature.depth * axisAlignment;
                    const double backOpeningProj = bottomProj - feature.depth * axisAlignment;
                    const double tolerance = std::max(0.35, feature.depth * 0.15);
                    if (std::abs(maxProj - frontOpeningProj) <= tolerance) {
                        return FaceRegion::Front;
                    }
                    if (std::abs(backOpeningProj - minProj) <= tolerance) {
                        return FaceRegion::Back;
                    }
                }
            }

            const FaceRegion reverseAxisRegion = regionFromAxis(-feature.axis, frontNormal);
            if (axisRegion == FaceRegion::Side && reverseAxisRegion == FaceRegion::Side) {
                return FaceRegion::Side;
            }
            if (axisRegion == FaceRegion::Front || reverseAxisRegion == FaceRegion::Front) {
                return FaceRegion::Front;
            }
            if (axisRegion == FaceRegion::Back || reverseAxisRegion == FaceRegion::Back) {
                return FaceRegion::Back;
            }
        }
        if (faceRegion != FaceRegion::Unknown && regionPriority(faceRegion) >= regionPriority(axisRegion)) {
            return faceRegion;
        }
    }

    if (faceRegion != FaceRegion::Unknown) {
        return faceRegion;
    }
    return axisRegion;
}

static double estimatePlanarDepthFromSides(const TopoGraph &graph,
                                           int bottomFace,
                                           const QVector<int> &sideFaces)
{
    const FaceNode &bottomNode = graph.nodes[bottomFace - 1];
    const QVector3D n = bottomNode.normal.normalized();
    if (n.lengthSquared() <= 1.0e-8f) {
        return 0.0;
    }

    const TopoDS_Face bottomTopoFace = TopoDS::Face(graph.faceMap(bottomFace));
    const QVector3D bottomCenter = estimateFaceCentroid(bottomTopoFace);
    const double bottomProj = QVector3D::dotProduct(bottomCenter, n);
    double maxDelta = 0.0;

    for (int sideIndex : sideFaces) {
        for (const auto &entry : graph.aag.value(sideIndex)) {
            const int neighborIndex = entry.first;
            if (neighborIndex == bottomFace) {
                continue;
            }
            const FaceNode &neighborNode = graph.nodes[neighborIndex - 1];
            if (neighborNode.surfType != GeomAbs_Plane) {
                continue;
            }
            const double alignment = std::abs(QVector3D::dotProduct(neighborNode.normal.normalized(), n));
            if (alignment < 0.90) {
                continue;
            }
            const QVector3D c = estimateFaceCentroid(TopoDS::Face(graph.faceMap(neighborIndex)));
            const double delta = QVector3D::dotProduct(c, n) - bottomProj;
            if (delta > 0.0) maxDelta = std::max(maxDelta, delta);
        }
    }
    return maxDelta;
}

static bool areCoplanarFaces(const TopoGraph &graph, int a, int b)
{
    if (a <= 0 || b <= 0 || a > graph.nodes.size() || b > graph.nodes.size()) {
        return false;
    }

    const FaceNode &na = graph.nodes[a - 1];
    const FaceNode &nb = graph.nodes[b - 1];
    if (na.surfType != GeomAbs_Plane || nb.surfType != GeomAbs_Plane) {
        return false;
    }

    const QVector3D an = na.normal.normalized();
    const QVector3D bn = nb.normal.normalized();
    if (an.lengthSquared() <= 1.0e-8f || bn.lengthSquared() <= 1.0e-8f) {
        return false;
    }

    if (std::abs(QVector3D::dotProduct(an, bn)) < 0.995f) {
        return false;
    }

    const QVector3D ca = estimateFaceCentroid(TopoDS::Face(graph.faceMap(a)));
    const QVector3D cb = estimateFaceCentroid(TopoDS::Face(graph.faceMap(b)));
    const double da = QVector3D::dotProduct(ca, an);
    const double db = QVector3D::dotProduct(cb, an);
    return std::abs(da - db) <= 0.25;
}

static QVector<int> collectCoplanarFaces(const TopoGraph &graph, int seedFaceIndex)
{
    QVector<int> group;
    if (seedFaceIndex <= 0 || seedFaceIndex > graph.nodes.size()) {
        return group;
    }
    if (graph.nodes[seedFaceIndex - 1].surfType != GeomAbs_Plane) {
        return group;
    }

    QSet<int> visited;
    QVector<int> stack;
    stack.append(seedFaceIndex);
    visited.insert(seedFaceIndex);

    while (!stack.isEmpty()) {
        const int faceIndex = stack.takeLast();
        group.append(faceIndex);

        for (const auto &entry : graph.aag.value(faceIndex)) {
            const int neighborIndex = entry.first;
            if (visited.contains(neighborIndex)) {
                continue;
            }
            if (!areCoplanarFaces(graph, seedFaceIndex, neighborIndex)) {
                continue;
            }
            visited.insert(neighborIndex);
            stack.append(neighborIndex);
        }
    }

    return uniqueIndices(group);
}

static FaceBox2D estimatePlanarFaceBox(const TopoGraph &graph,
                                       const QVector<int> &faceIndices,
                                       const QVector3D &normalHint)
{
    if (faceIndices.isEmpty()) {
        return {};
    }

    QVector3D normal = normalHint.normalized();
    if (normal.lengthSquared() <= 1.0e-8f) {
        const int faceIndex = faceIndices.first();
        if (faceIndex > 0 && faceIndex <= graph.nodes.size()) {
            normal = graph.nodes[faceIndex - 1].normal.normalized();
        }
    }
    if (normal.lengthSquared() <= 1.0e-8f) {
        return {};
    }

    QVector<QVector3D> points;

    for (int faceIndex : faceIndices) {
        if (faceIndex <= 0 || faceIndex > graph.faceMap.Extent()) {
            continue;
        }

        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(TopoDS::Face(graph.faceMap(faceIndex)), loc);
        if (tri.IsNull() || tri->NbNodes() <= 0) {
            continue;
        }

        const gp_Trsf trsf = loc.Transformation();
        for (Standard_Integer i = 1; i <= tri->NbNodes(); ++i) {
            const gp_Pnt p = tri->Node(i).Transformed(trsf);
            points.append(QVector3D(float(p.X()), float(p.Y()), float(p.Z())));
        }
    }

    return buildPlanarFaceBox(points, normal);
}

} // namespace

static QVector<MachiningFeature> mergeCounterbores(QVector<MachiningFeature> holes)
{
    const int n = holes.size();
    QVector<bool> merged(n, false);
    QVector<MachiningFeature> result;

    for (int i = 0; i < n; ++i) {
        if (merged[i]) continue;
        const MachiningFeature &a = holes[i];
        if (a.kind != FeatureKind::Hole && a.kind != FeatureKind::Thread) {
            result.append(a);
            continue;
        }

        int pairedWith = -1;
        for (int j = i + 1; j < n; ++j) {
            if (merged[j]) continue;
            const MachiningFeature &b = holes[j];
            if (b.kind != FeatureKind::Hole && b.kind != FeatureKind::Thread) continue;
            if (std::abs(a.radius - b.radius) < 0.05) continue;

            const float axisDot = std::abs(QVector3D::dotProduct(a.axis.normalized(), b.axis.normalized()));
            if (axisDot < 0.98f) continue;

            const QVector3D diff = b.center - a.center;
            const QVector3D axN = a.axis.normalized();
            const float axialDist = QVector3D::dotProduct(diff, axN);
            const QVector3D lateral = diff - axN * axialDist;
            if (lateral.length() > 0.5f) continue;

            const float aMin = QVector3D::dotProduct(a.center, axN);
            const float aMax = aMin + float(a.depth);
            const float bMin = QVector3D::dotProduct(b.center, axN);
            const float bMax = bMin + float(b.depth);
            const float gap = std::max(aMin, bMin) - std::min(aMax, bMax);
            if (gap > 1.0f) continue;

            pairedWith = j;
            break;
        }

        if (pairedWith < 0) {
            result.append(a);
            continue;
        }

        merged[pairedWith] = true;
        const MachiningFeature &b = holes[pairedWith];
        const bool aIsLarger = a.radius > b.radius;
        const MachiningFeature &outer = aIsLarger ? a : b;
        const MachiningFeature &inner = aIsLarger ? b : a;

        // Determine through/blind from the inner (narrower) hole
        const bool innerIsThrough = inner.subType.contains(QStringLiteral("through_hole"));

        // Compute opening-face position: the axial end of the combined bore
        // that is farthest from the cylinder midpoints (i.e. the entry side).
        // Cylinders store their center at the geometric midpoint, so the two
        // axial ends of each cylinder are center +/- depth/2 along the axis.
        const QVector3D axN = outer.axis.normalized();
        const float outerProj  = QVector3D::dotProduct(outer.center, axN);
        const float outerEnd1  = outerProj - float(outer.depth) * 0.5f;
        const float outerEnd2  = outerProj + float(outer.depth) * 0.5f;
        const float innerProj  = QVector3D::dotProduct(inner.center, axN);
        const float innerEnd1  = innerProj - float(inner.depth) * 0.5f;
        const float innerEnd2  = innerProj + float(inner.depth) * 0.5f;

        // Entry is the outermost projection end (max of all four endpoints)
        const float entryProj = std::max({outerEnd1, outerEnd2, innerEnd1, innerEnd2});
        // Deepest end is the minimum projection
        const float deepProj  = std::min({outerEnd1, outerEnd2, innerEnd1, innerEnd2});

        const gp_Ax1 outerAxis(gp_Pnt(double(outer.center.x()),
                                       double(outer.center.y()),
                                       double(outer.center.z())),
                                gp_Dir(double(axN.x()), double(axN.y()), double(axN.z())));
        const gp_Pnt entryPnt = pointOnAxisAtProjection(outerAxis, double(entryProj));

        MachiningFeature cb;
        cb.kind = FeatureKind::Hole;
        cb.subType = innerIsThrough ? QStringLiteral("counterbore_through_hole")
                                    : QStringLiteral("counterbore_blind_hole");
        cb.radius = inner.radius;
        cb.secondaryRadius = outer.radius;
        cb.depth = double(entryProj - deepProj);
        cb.center = QVector3D(float(entryPnt.X()), float(entryPnt.Y()), float(entryPnt.Z()));
        cb.axis = outer.axis;
        cb.region = outer.region;
        cb.faceIndices = outer.faceIndices;
        for (int fi : inner.faceIndices) {
            if (!cb.faceIndices.contains(fi))
                cb.faceIndices.append(fi);
        }
        result.append(cb);
    }
    return result;
}

QVector<MachiningFeature> FeatureRecognizer::recognize(const TopoGraph &graph) const
{
    QVector<MachiningFeature> features = mergeCounterbores(findHoles(graph));
    QSet<int> consumedFaces;
    for (const MachiningFeature &feature : features) {
        for (int faceIndex : feature.faceIndices) {
            consumedFaces.insert(faceIndex);
        }
    }

    const QVector<MachiningFeature> bossFeatures = findBosses(graph, consumedFaces);
    for (const MachiningFeature &boss : bossFeatures) {
        for (int faceIndex : boss.faceIndices) {
            consumedFaces.insert(faceIndex);
        }
        features.append(boss);
    }

    const QVector<MachiningFeature> slotFeatures = findSlots(graph);
    QSet<int> consumedBottomFaces;
    for (const MachiningFeature &slot : slotFeatures) {
        if (!slot.faceIndices.isEmpty()) {
            const QVector<int> bottomFaces = collectCoplanarFaces(graph, slot.faceIndices.first());
            for (int bottomFace : bottomFaces) {
                consumedBottomFaces.insert(bottomFace);
            }
        }
        for (int faceIndex : slot.faceIndices) {
            consumedFaces.insert(faceIndex);
        }
        features.append(slot);
    }
    const QVector<MachiningFeature> pockets = findPockets(graph, consumedBottomFaces);
    for (const MachiningFeature &pocket : pockets) {
        for (int faceIndex : pocket.faceIndices) {
            consumedFaces.insert(faceIndex);
        }
        features.append(pocket);
    }
    for (const MachiningFeature &chamfer : findChamfers(graph)) {
        for (int faceIndex : chamfer.faceIndices) {
            consumedFaces.insert(faceIndex);
        }
        features.append(chamfer);
    }
    for (const MachiningFeature &fillet : findFillets(graph)) {
        for (int faceIndex : fillet.faceIndices) {
            consumedFaces.insert(faceIndex);
        }
        features.append(fillet);
    }
    for (const MachiningFeature &flat : findFlatSurfaces(graph, consumedFaces)) {
        features.append(flat);
    }
    for (MachiningFeature &feature : features) {
        feature.region = machiningRegionForFeature(graph, feature, m_frontNormal);
    }
    return features;
}

void FeatureRecognizer::reclassifyRegions(TopoGraph &graph,
                                          QVector<MachiningFeature> &features) const
{
    const QVector3D fn = m_frontNormal.normalized();

    // Reclassify each face region from the current front normal.
    for (FaceNode &node : graph.nodes) {
        const QVector3D n = node.normal.normalized();
        if (n.lengthSquared() <= 1.0e-8f) {
            node.region = FaceRegion::Unknown;
            continue;
        }
        const float proj = QVector3D::dotProduct(n, fn);
        if (std::abs(proj) >= 0.65f) {
            node.region = proj >= 0.0f ? FaceRegion::Front : FaceRegion::Back;
        } else {
            node.region = FaceRegion::Side;
        }
    }

    // Compute the planar feature projection range before assigning top, bottom, and step subtypes.
    double minProj =  1e100;
    double maxProj = -1e100;
    for (const MachiningFeature &feature : features) {
        if (feature.kind != FeatureKind::FlatSurface) {
            continue;
        }
        const double proj = double(QVector3D::dotProduct(feature.center, fn));
        minProj = std::min(minProj, proj);
        maxProj = std::max(maxProj, proj);
    }

    // Assign machining regions and planar subtypes after the projection range is known.
    for (MachiningFeature &feature : features) {
        if (feature.kind == FeatureKind::FlatSurface) {
            if (!feature.faceIndices.isEmpty()) {
                const int fi = feature.faceIndices.first();
                if (fi > 0 && fi <= graph.nodes.size()) {
                    feature.region = graph.nodes[fi - 1].region;
                }
            }
            // Use the configured front-normal projection to classify top, bottom, and step surfaces.
            const double proj = double(QVector3D::dotProduct(feature.center, fn));
            feature.subType = planarSurfaceSubtype(feature.region, proj, minProj, maxProj);
        } else {
            feature.region = machiningRegionForFeature(graph, feature, m_frontNormal);
        }
    }
}

QVector<MachiningFeature> FeatureRecognizer::findHoles(const TopoGraph &graph) const
{
    QVector<MachiningFeature> features;
    FeatureClassifier classifier;

    for (int faceIndex = 1; faceIndex <= graph.faceMap.Extent(); ++faceIndex) {
        const TopoDS_Face face = TopoDS::Face(graph.faceMap(faceIndex));
        BRepAdaptor_Surface surface(face, false);
        if (surface.GetType() != GeomAbs_Cylinder) {
            continue;
        }

        const BRepAdaptor_Surface restrictedSurface(face, true);
        const double angularSpan =
            std::abs(restrictedSurface.LastUParameter() -
                     restrictedSurface.FirstUParameter());
        constexpr double fullCircleRadians = 6.283185307179586;
        if (!std::isfinite(angularSpan) ||
            angularSpan < fullCircleRadians - 0.05) {
            continue;
        }

        const auto neighbors = graph.aag.value(faceIndex);
        if (neighbors.isEmpty()) {
            continue;
        }

        bool hasConcaveNeighbor = false;
        int planarCaps = 0;
        int axialPlanarOpenings = 0;
        int alignedCones = 0;
        double maxConeHalfAngle = 0.0;
        double maxConeRadius = 0.0;
        QVector<int> featureFaceIndices;
        featureFaceIndices.append(faceIndex);
        QSet<int> visitedNeighbors;

        const gp_Cylinder cylinder = surface.Cylinder();
        const gp_Ax1 axis = cylinder.Axis();
        const gp_Dir axisDir = axis.Direction();
        const double radialAlignment = cylinderRadialNormalAlignment(face, axis);

        const FaceProjection cylinderProjection = estimateFaceProjection(face, axisDir);
        double capProjection = 0.0;
        bool hasCapProjection = false;

        for (const auto &neighbor : neighbors) {
            const int neighborIndex = neighbor.first;
            const EdgeAttr &edgeAttr = neighbor.second;
            if (visitedNeighbors.contains(neighborIndex)) {
                continue;
            }
            visitedNeighbors.insert(neighborIndex);

            if (edgeAttr.isConcave) {
                hasConcaveNeighbor = true;
            }

            const TopoDS_Face neighborFace = TopoDS::Face(graph.faceMap(neighborIndex));
            BRepAdaptor_Surface neighborSurface(neighborFace, false);
            if (neighborSurface.GetType() == GeomAbs_Plane) {
                const gp_Dir planeAxis = neighborSurface.Plane().Axis().Direction();
                // Neighbor planes nearly perpendicular to the cylinder axis are not bore end caps.
                if (std::abs(dotDir(planeAxis, axisDir)) < 0.3) {
                    continue;
                }
                if (std::abs(dotDir(planeAxis, axisDir)) < 0.92) {
                    continue;
                }
                ++axialPlanarOpenings;

                if (!isCompactPlanarCap(graph, neighborIndex, axis, cylinder.Radius())) {
                    continue;
                }

                ++planarCaps;
                featureFaceIndices.append(neighborIndex);
                const FaceProjection planeProjection = estimateFaceProjection(neighborFace, axisDir);
                if (!planeProjection.valid) {
                    continue;
                }
                if (!hasCapProjection) {
                    capProjection = planeProjection.avgProj;
                    hasCapProjection = true;
                } else if (cylinderProjection.valid &&
                           std::abs(planeProjection.avgProj - cylinderProjection.avgProj) >
                               std::abs(capProjection - cylinderProjection.avgProj)) {
                    capProjection = planeProjection.avgProj;
                }
            } else if (neighborSurface.GetType() == GeomAbs_Cone) {
                const gp_Cone cone = neighborSurface.Cone();
                if (std::abs(dotDir(cone.Axis().Direction(), axisDir)) >= 0.97) {
                    ++alignedCones;
                    featureFaceIndices.append(neighborIndex);
                    maxConeHalfAngle = std::max(maxConeHalfAngle, std::abs(cone.SemiAngle()));
                    maxConeRadius = std::max(maxConeRadius, cone.RefRadius());
                }
            }
        }

        // 2nd-hop: find planar caps separated from the cylinder by a fillet or chamfer.
        // When a blind hole has a fillet at its bottom edge, the bottom cap is only
        // reachable through the smooth-edge neighbor (the fillet face).
        for (const auto &neighbor : neighbors) {
            const int neighborIndex = neighbor.first;
            const EdgeAttr &edgeAttr = neighbor.second;
            if (!edgeAttr.isSmooth) continue;
            for (const auto &hop2 : graph.aag.value(neighborIndex)) {
                const int hop2Index = hop2.first;
                if (visitedNeighbors.contains(hop2Index)) continue;
                if (featureFaceIndices.contains(hop2Index)) continue;
                const TopoDS_Face hop2Face = TopoDS::Face(graph.faceMap(hop2Index));
                BRepAdaptor_Surface hop2Surface(hop2Face, false);
                if (hop2Surface.GetType() != GeomAbs_Plane) continue;
                const gp_Dir planeAxis = hop2Surface.Plane().Axis().Direction();
                if (std::abs(dotDir(planeAxis, axisDir)) < 0.92) continue;
                if (!isCompactPlanarCap(graph, hop2Index, axis, cylinder.Radius())) continue;
                visitedNeighbors.insert(hop2Index);
                ++planarCaps;
                ++axialPlanarOpenings;
                featureFaceIndices.append(hop2Index);
                const FaceProjection planeProjection = estimateFaceProjection(hop2Face, axisDir);
                if (!planeProjection.valid) continue;
                if (!hasCapProjection) {
                    capProjection = planeProjection.avgProj;
                    hasCapProjection = true;
                } else if (cylinderProjection.valid &&
                           std::abs(planeProjection.avgProj - cylinderProjection.avgProj) >
                               std::abs(capProjection - cylinderProjection.avgProj)) {
                    capProjection = planeProjection.avgProj;
                }
            }
        }

        const bool looksLikeThroughOpening = axialPlanarOpenings >= 2 && planarCaps == 0;
        if (radialAlignment > 0.25 && !hasConcaveNeighbor && !looksLikeThroughOpening) {
            continue;
        }
        if (!hasConcaveNeighbor && !looksLikeThroughOpening) {
            continue;
        }

        MachiningFeature feature;
        feature.kind = FeatureKind::Hole;
        feature.radius = cylinder.Radius();
        feature.secondaryRadius = maxConeRadius;
        feature.axis = toVector3D(axisDir);
        feature.coneAngle = maxConeHalfAngle * 2.0;
        feature.faceIndices = featureFaceIndices;

        if (cylinderProjection.valid) {
            const gp_Pnt center = pointOnAxisAtProjection(axis, cylinderProjection.avgProj);
            feature.center = QVector3D(float(center.X()), float(center.Y()), float(center.Z()));
            feature.depth = std::abs(cylinderProjection.maxProj - cylinderProjection.minProj);
            if (hasCapProjection) {
                feature.depth = std::max(std::abs(capProjection - cylinderProjection.minProj),
                                         std::abs(capProjection - cylinderProjection.maxProj));
            }
        } else {
            feature.center = QVector3D(float(axis.Location().X()),
                                       float(axis.Location().Y()),
                                       float(axis.Location().Z()));
        }

        Q_UNUSED(planarCaps)
        Q_UNUSED(alignedCones)
        classifier.classifyHole(feature, graph);
        if (feature.region == FaceRegion::Side) {
            const QString sideDir = sideDirectionSubtype(feature.axis);
            feature.subType = feature.subType.isEmpty()
                                  ? sideDir
                                  : feature.subType + QStringLiteral("_") + sideDir;
        }
        if (feature.kind == FeatureKind::Hole || feature.kind == FeatureKind::Thread) {
            features.append(feature);
        }
    }

    return features;
}

QVector<MachiningFeature> FeatureRecognizer::findFlatSurfaces(const TopoGraph &graph,
                                                              const QSet<int> &consumedFaces) const
{
    QVector<MachiningFeature> features;
    double minZ = 1e100;
    double maxZ = -1e100;
    QVector<QPair<int, FaceBox2D>> planarFaces;

    for (int faceIndex = 1; faceIndex <= graph.faceMap.Extent(); ++faceIndex) {
        if (consumedFaces.contains(faceIndex)) {
            continue;
        }
        const FaceNode &node = graph.nodes[faceIndex - 1];
        if (node.surfType != GeomAbs_Plane) {
            continue;
        }
        const FaceBox2D box = estimatePlanarFaceBox(TopoDS::Face(graph.faceMap(faceIndex)), node.normal);
        if (!box.valid) {
            continue;
        }
        planarFaces.append(qMakePair(faceIndex, box));
        minZ = std::min(minZ, double(box.center.z()));
        maxZ = std::max(maxZ, double(box.center.z()));
    }

    for (const auto &entry : planarFaces) {
        const int faceIndex = entry.first;
        const FaceBox2D &box = entry.second;
        const FaceNode &node = graph.nodes[faceIndex - 1];

        MachiningFeature feature;
        feature.kind = FeatureKind::FlatSurface;
        feature.subType = planarSurfaceSubtype(node.region, box.center.z(), minZ, maxZ);
        feature.center = box.center;
        feature.axis = box.normal;
        feature.width = box.width;
        feature.length = box.length;
        feature.angle = box.angle;
        if (feature.subType == QStringLiteral("circular_pocket")) {
            feature.radius = std::min(feature.width, feature.length) * 0.5;
        }
        feature.region = node.region;
        feature.faceIndices = {faceIndex};
        features.append(feature);
    }

    return features;
}

QVector<MachiningFeature> FeatureRecognizer::findBosses(const TopoGraph &graph,
                                                        const QSet<int> &consumedFaces) const
{
    QVector<MachiningFeature> features;

    for (int faceIndex = 1; faceIndex <= graph.faceMap.Extent(); ++faceIndex) {
        if (consumedFaces.contains(faceIndex)) {
            continue;
        }
        const TopoDS_Face face = TopoDS::Face(graph.faceMap(faceIndex));
        BRepAdaptor_Surface surface(face, false);
        if (surface.GetType() != GeomAbs_Cylinder) {
            continue;
        }

        const gp_Cylinder cylinder = surface.Cylinder();
        const gp_Ax1 axis = cylinder.Axis();
        const double radialAlignment = cylinderRadialNormalAlignment(face, axis);
        if (radialAlignment < 0.65) {
            continue;
        }

        QVector<int> supportFaces;
        QVector<int> capFaces;
        bool hasConvexEdge = false;
        for (const auto &entry : graph.aag.value(faceIndex)) {
            if (!entry.second.isConcave) {
                hasConvexEdge = true;
            }
            const int neighborIndex = entry.first;
            const FaceNode &neighborNode = graph.nodes[neighborIndex - 1];
            if (neighborNode.surfType == GeomAbs_Plane) {
                supportFaces.append(neighborIndex);
                if (isCompactPlanarCap(graph, neighborIndex, axis, cylinder.Radius())) {
                    capFaces.append(neighborIndex);
                }
            }
        }
        supportFaces = uniqueIndices(supportFaces);
        capFaces = uniqueIndices(capFaces);
        if (!hasConvexEdge || supportFaces.isEmpty() || capFaces.isEmpty()) {
            continue;
        }

        const FaceProjection projection = estimateFaceProjection(face, axis.Direction());
        MachiningFeature feature;
        feature.kind = FeatureKind::Boss;
        feature.subType = QStringLiteral("circular_boss");
        feature.center = estimateFaceCentroid(face);
        feature.axis = toVector3D(axis.Direction());
        feature.radius = cylinder.Radius();
        feature.secondaryRadius = cylinder.Radius();
        if (projection.valid) {
            feature.depth = std::abs(projection.maxProj - projection.minProj);
        }
        feature.faceIndices = {faceIndex};
        for (int capFace : capFaces) {
            feature.faceIndices.append(capFace);
        }
        features.append(feature);
    }

    return features;
}

QVector<MachiningFeature> FeatureRecognizer::findSlots(const TopoGraph &graph) const
{
    QVector<MachiningFeature> features;
    QSet<int> processedBottoms;

    for (int faceIndex = 1; faceIndex <= graph.faceMap.Extent(); ++faceIndex) {
        if (processedBottoms.contains(faceIndex)) {
            continue;
        }
        if (faceIndex <= 0 || faceIndex > graph.nodes.size()) {
            continue;
        }

        const FaceNode &seedNode = graph.nodes[faceIndex - 1];
        if (seedNode.surfType != GeomAbs_Plane) {
            continue;
        }

        const QVector<int> bottomFaces = collectCoplanarFaces(graph, faceIndex);
        if (bottomFaces.isEmpty()) {
            continue;
        }

        QSet<int> bottomFaceSet;
        for (int bottomFace : bottomFaces) {
            bottomFaceSet.insert(bottomFace);
        }

        const QVector3D bottomNormal = seedNode.normal.normalized();
        if (bottomNormal.lengthSquared() <= 1.0e-8f) {
            continue;
        }

        QVector<int> sideFaces;
        QSet<int> convexNeighbors;
        QSet<int> seenNeighbors;

        for (int bottomFace : bottomFaces) {
            for (const auto &entry : graph.aag.value(bottomFace)) {
                const int neighborIndex = entry.first;
                const EdgeAttr &edgeAttr = entry.second;
                if (bottomFaceSet.contains(neighborIndex) || seenNeighbors.contains(neighborIndex)) {
                    continue;
                }
                seenNeighbors.insert(neighborIndex);

                const FaceNode &neighborNode = graph.nodes[neighborIndex - 1];
                if (neighborNode.surfType != GeomAbs_Plane &&
                    neighborNode.surfType != GeomAbs_Cylinder) {
                    continue;
                }

                const TopoDS_Face neighborFace = TopoDS::Face(graph.faceMap(neighborIndex));
                BRepAdaptor_Surface neighborSurf(neighborFace, false);
                if (neighborNode.surfType == GeomAbs_Cylinder) {
                    const gp_Dir cylAxis = neighborSurf.Cylinder().Axis().Direction();
                    const double dot = cylAxis.X() * bottomNormal.x()
                                     + cylAxis.Y() * bottomNormal.y()
                                     + cylAxis.Z() * bottomNormal.z();
                    if (std::abs(dot) < 0.85) {
                        continue;
                    }
                } else {
                    const gp_Dir planeNormal = neighborSurf.Plane().Axis().Direction();
                    const double dot = planeNormal.X() * bottomNormal.x()
                                     + planeNormal.Y() * bottomNormal.y()
                                     + planeNormal.Z() * bottomNormal.z();
                    if (std::abs(dot) > 0.3) {
                        continue;
                    }
                }

                if (edgeAttr.isConcave) {
                    sideFaces.append(neighborIndex);
                } else {
                    convexNeighbors.insert(neighborIndex);
                }
            }
        }

        sideFaces = uniqueIndices(sideFaces);
        if (sideFaces.isEmpty()) {
            continue;
        }

        const FaceBox2D box = estimatePlanarFaceBox(graph, bottomFaces, seedNode.normal);
        if (!box.valid) {
            continue;
        }
        if (isNearModelBoundary(graph, box.center, bottomNormal)) {
            continue;
        }
        sideFaces = selectSlotWallFaces(graph, bottomFaces, sideFaces, box);
        const int convexNeighborCount = convexNeighbors.size();
        if (sideFaces.size() < 2 || sideFaces.size() > 4 ||
            convexNeighborCount > 2 ||
            sideFaces.size() < convexNeighborCount) {
            continue;
        }
        const double requiredAspect = convexNeighborCount > 0 ? 1.2 : 1.5;
        if (box.length < box.width * requiredAspect) {
            continue;
        }
        if (!isSlotSideLayoutConsistent(graph, sideFaces, box)) {
            continue;
        }
        if (!isReasonableRecessBottom(graph, bottomFaces.first(), sideFaces, box, 2.5)) {
            continue;
        }

        double depth = estimatePlanarDepthFromSides(graph, bottomFaces.first(), sideFaces);
        if (depth < 0.5) {
            depth = estimateFacesSpanAlongDirection(graph, sideFaces, bottomNormal);
        }
        if (depth < 0.5) {
            continue;
        }

        MachiningFeature feature;
        feature.kind = FeatureKind::Slot;
        const bool hasCylindricalSide = std::any_of(sideFaces.cbegin(), sideFaces.cend(), [&](int sideIndex) {
            return graph.nodes[sideIndex - 1].surfType == GeomAbs_Cylinder;
        });
        feature.subType = hasCylindricalSide ? QStringLiteral("arc_slot")
                                             : QStringLiteral("straight_slot");
        feature.center = box.center;
        feature.axis = box.normal;
        feature.width = box.width;
        feature.length = box.length;
        feature.angle = box.angle;
        feature.faceIndices = bottomFaces;
        for (int bottomFace : bottomFaces) {
            processedBottoms.insert(bottomFace);
        }
        for (int sideIndex : sideFaces) {
            if (!feature.faceIndices.contains(sideIndex)) {
                feature.faceIndices.append(sideIndex);
            }
        }
        feature.depth = depth;
        qDebug().noquote()
            << QStringLiteral("[slot-recognizer] subtype=%1 center=(%2,%3,%4) L=%5 W=%6 D=%7 A=%8 bottomFaces=%9 sideFaces=%10 allFaces=%11")
                  .arg(feature.subType)
                  .arg(feature.center.x(), 0, 'f', 3)
                  .arg(feature.center.y(), 0, 'f', 3)
                  .arg(feature.center.z(), 0, 'f', 3)
                  .arg(feature.length, 0, 'f', 3)
                  .arg(feature.width, 0, 'f', 3)
                  .arg(feature.depth, 0, 'f', 3)
                  .arg(feature.angle, 0, 'f', 3)
                  .arg(QStringList([&]() {
                      QStringList list;
                      for (int v : bottomFaces) list << QString::number(v);
                      return list;
                  }()).join(QStringLiteral(",")))
                  .arg(QStringList([&]() {
                      QStringList list;
                      for (int v : sideFaces) list << QString::number(v);
                      return list;
                  }()).join(QStringLiteral(",")))
                  .arg(QStringList([&]() {
                      QStringList list;
                      for (int v : feature.faceIndices) list << QString::number(v);
                      return list;
                  }()).join(QStringLiteral(",")));
        features.append(feature);
    }

    QSet<int> consumedSlotFaces;
    for (const MachiningFeature &feature : features) {
        for (int faceIndex : feature.faceIndices) {
            consumedSlotFaces.insert(faceIndex);
        }
    }

    QSet<int> processedWallGroups;
    for (int faceIndex = 1; faceIndex <= graph.faceMap.Extent(); ++faceIndex) {
        if (processedWallGroups.contains(faceIndex) || consumedSlotFaces.contains(faceIndex)) {
            continue;
        }
        if (faceIndex <= 0 || faceIndex > graph.nodes.size()) {
            continue;
        }

        const FaceNode &seedNode = graph.nodes[faceIndex - 1];
        if (seedNode.surfType != GeomAbs_Plane) {
            continue;
        }

        const QVector<int> wallGroup = collectConcaveConnectedPlanarFaces(graph, faceIndex);
        if (wallGroup.size() != 4) {
            for (int wallFace : wallGroup) {
                processedWallGroups.insert(wallFace);
            }
            continue;
        }

        bool overlapsKnownSlot = false;
        for (int wallFace : wallGroup) {
            processedWallGroups.insert(wallFace);
            if (consumedSlotFaces.contains(wallFace)) {
                overlapsKnownSlot = true;
            }
        }
        if (overlapsKnownSlot) {
            continue;
        }

        struct WallInfo {
            int faceIndex = 0;
            QVector3D normal;
            QVector3D center;
        };

        QVector<WallInfo> walls;
        for (int wallFace : wallGroup) {
            const QVector3D normal = graph.nodes[wallFace - 1].normal.normalized();
            if (normal.lengthSquared() <= 1.0e-8f) {
                walls.clear();
                break;
            }
            walls.append({wallFace, normal, estimateFaceCentroid(TopoDS::Face(graph.faceMap(wallFace)))});
        }
        if (walls.size() != 4) {
            continue;
        }

        QVector<QVector<int>> orientationGroups;
        QVector<QVector3D> groupRefs;
        for (const WallInfo &wall : walls) {
            bool matched = false;
            for (int i = 0; i < groupRefs.size(); ++i) {
                if (std::abs(QVector3D::dotProduct(wall.normal, groupRefs[i])) >= 0.98f) {
                    orientationGroups[i].append(wall.faceIndex);
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                groupRefs.append(wall.normal);
                QVector<int> newGroup;
                newGroup.append(wall.faceIndex);
                orientationGroups.append(newGroup);
            }
        }

        if (orientationGroups.size() != 2 ||
            orientationGroups[0].size() != 2 ||
            orientationGroups[1].size() != 2) {
            continue;
        }

        auto faceCenter = [&](int wallFace) {
            return estimateFaceCentroid(TopoDS::Face(graph.faceMap(wallFace)));
        };

        auto buildPairAxis = [&](const QVector<int> &pairFaces, bool *ok, double *span) {
            const QVector3D c0 = faceCenter(pairFaces[0]);
            const QVector3D c1 = faceCenter(pairFaces[1]);
            QVector3D axis = c1 - c0;
            *span = axis.length();
            if (*span <= 1.0e-6) {
                *ok = false;
                return QVector3D();
            }
            axis.normalize();
            const QVector3D n0 = graph.nodes[pairFaces[0] - 1].normal.normalized();
            const QVector3D n1 = graph.nodes[pairFaces[1] - 1].normal.normalized();
            if (std::abs(QVector3D::dotProduct(axis, n0)) < 0.85f &&
                std::abs(QVector3D::dotProduct(axis, n1)) < 0.85f) {
                *ok = false;
                return QVector3D();
            }
            *ok = true;
            return axis;
        };

        bool pairOkA = false;
        bool pairOkB = false;
        double spanA = 0.0;
        double spanB = 0.0;
        QVector3D axisA = buildPairAxis(orientationGroups[0], &pairOkA, &spanA);
        QVector3D axisB = buildPairAxis(orientationGroups[1], &pairOkB, &spanB);
        if (!pairOkA || !pairOkB) {
            continue;
        }
        if (std::abs(QVector3D::dotProduct(axisA, axisB)) > 0.25f) {
            continue;
        }

        QVector3D widthAxis = axisA;
        QVector3D lengthAxis = axisB;
        double width = spanA;
        double length = spanB;
        if (width > length) {
            std::swap(width, length);
            std::swap(widthAxis, lengthAxis);
        }
        if (length < width * 1.5 || width < 0.5) {
            continue;
        }

        const QVector3D openingAxis = QVector3D::crossProduct(widthAxis, lengthAxis).normalized();
        if (openingAxis.lengthSquared() <= 1.0e-8f) {
            continue;
        }
        if (hasPlanarBottomNeighborForWallGroup(graph, wallGroup, openingAxis)) {
            continue;
        }

        const double depth = estimateFacesSpanAlongDirection(graph, wallGroup, openingAxis);
        if (depth < 0.5) {
            continue;
        }

        QVector3D center;
        for (const WallInfo &wall : walls) {
            center += wall.center;
        }
        center /= float(walls.size());
        if (isNearModelBoundary(graph, center, openingAxis)) {
            continue;
        }

        MachiningFeature feature;
        feature.kind = FeatureKind::Slot;
        feature.subType = QStringLiteral("open_slot");
        feature.center = center;
        feature.axis = openingAxis;
        feature.width = width;
        feature.length = length;
        feature.depth = depth;
        feature.angle = std::atan2(lengthAxis.y(), lengthAxis.x()) * (180.0 / std::acos(-1.0));
        feature.faceIndices = wallGroup;
        features.append(feature);
    }

    return features;
}
QVector<MachiningFeature> FeatureRecognizer::findPockets(const TopoGraph &graph,
                                                         const QSet<int> &consumedBottomFaces) const
{
    QVector<MachiningFeature> features;

    for (int faceIndex = 1; faceIndex <= graph.faceMap.Extent(); ++faceIndex) {
        if (consumedBottomFaces.contains(faceIndex)) {
            continue;
        }

        const FaceNode &node = graph.nodes[faceIndex - 1];
        if (node.surfType != GeomAbs_Plane) {
            continue;
        }

        QVector<int> sideFaces;
        bool allConcave = true;
        for (const auto &entry : graph.aag.value(faceIndex)) {
            const int neighborIndex = entry.first;
            const EdgeAttr &edgeAttr = entry.second;
            if (edgeAttr.isSmooth) {
                continue;
            }

            const FaceNode &neighborNode = graph.nodes[neighborIndex - 1];
            if (neighborNode.surfType != GeomAbs_Plane &&
                neighborNode.surfType != GeomAbs_Cylinder) {
                continue;
            }

            // Validate neighbor orientation: walls must be perpendicular to the bottom
            // face normal. For cylinders, the axis must be parallel to the bottom normal.
            // For planes, the normal must be perpendicular to the bottom normal.
            {
                const TopoDS_Face neighborFace = TopoDS::Face(graph.faceMap(neighborIndex));
                BRepAdaptor_Surface neighborSurf(neighborFace, false);
                if (neighborNode.surfType == GeomAbs_Cylinder) {
                    const gp_Dir cylAxis = neighborSurf.Cylinder().Axis().Direction();
                    const double dot = cylAxis.X() * node.normal.x()
                                     + cylAxis.Y() * node.normal.y()
                                     + cylAxis.Z() * node.normal.z();
                    if (std::abs(dot) < 0.85) {
                        continue;
                    }
                } else {
                    const gp_Dir planeNormal = neighborSurf.Plane().Axis().Direction();
                    const double dot = planeNormal.X() * node.normal.x()
                                     + planeNormal.Y() * node.normal.y()
                                     + planeNormal.Z() * node.normal.z();
                    if (std::abs(dot) > 0.3) {
                        continue;
                    }
                }
            }

            if (!edgeAttr.isConcave) {
                allConcave = false;
            }
            sideFaces.append(neighborIndex);
        }

        sideFaces = uniqueIndices(sideFaces);
        if (!allConcave || sideFaces.size() < 3) {
            continue;
        }

        const TopoDS_Face face = TopoDS::Face(graph.faceMap(faceIndex));
        const FaceBox2D box = estimatePlanarFaceBox(face, node.normal);
        if (!box.valid) {
            continue;
        }
        if (!isReasonableRecessBottom(graph, faceIndex, sideFaces, box, 1.8)) {
            continue;
        }

        double depth = estimatePlanarDepthFromSides(graph, faceIndex, sideFaces);
        if (depth < 0.5) {
            continue;
        }

        MachiningFeature feature;
        feature.kind = FeatureKind::Pocket;
        int planarSideCount = 0;
        int cylindricalSideCount = 0;
        for (int sideIndex : sideFaces) {
            const GeomAbs_SurfaceType type = graph.nodes[sideIndex - 1].surfType;
            if (type == GeomAbs_Plane) ++planarSideCount;
            if (type == GeomAbs_Cylinder) ++cylindricalSideCount;
        }
        if (cylindricalSideCount > 0 && planarSideCount <= 1) {
            feature.subType = QStringLiteral("circular_pocket");
        } else if (planarSideCount == 4 && cylindricalSideCount == 0) {
            feature.subType = QStringLiteral("rectangular_pocket");
        } else {
            feature.subType = QStringLiteral("irregular_pocket");
        }
        feature.center = box.center;
        feature.axis = box.normal;
        feature.width = box.width;
        feature.length = box.length;
        feature.angle = box.angle;
        if (feature.subType == QStringLiteral("circular_pocket")) {
            feature.radius = std::min(feature.width, feature.length) * 0.5;
        }
        extractPocketLoops(face, feature.boundaryPoints, feature.islandBoundaries);
        feature.faceIndices = {faceIndex};
        for (int sideIndex : sideFaces) {
            feature.faceIndices.append(sideIndex);
        }

        feature.depth = depth;
        features.append(feature);
    }

    return features;
}

QVector<MachiningFeature> FeatureRecognizer::findChamfers(const TopoGraph &graph) const
{
    QVector<MachiningFeature> features;

    for (int faceIndex = 1; faceIndex <= graph.faceMap.Extent(); ++faceIndex) {
        const TopoDS_Face face = TopoDS::Face(graph.faceMap(faceIndex));
        BRepAdaptor_Surface surface(face, false);
        if (surface.GetType() != GeomAbs_Cone) {
            continue;
        }

        QVector<int> supportFaces;
        bool touchesConcave = false;
        for (const auto &entry : graph.aag.value(faceIndex)) {
            const int neighborIndex = entry.first;
            const EdgeAttr &edgeAttr = entry.second;
            const FaceNode &neighborNode = graph.nodes[neighborIndex - 1];
            if (neighborNode.surfType == GeomAbs_Cone) {
                continue;
            }
            if (!edgeAttr.isSmooth) {
                supportFaces.append(neighborIndex);
            }
            if (edgeAttr.isConcave) {
                touchesConcave = true;
            }
        }

        supportFaces = uniqueIndices(supportFaces);
        if (supportFaces.size() < 2) {
            continue;
        }

        const gp_Cone cone = surface.Cone();
        MachiningFeature feature;
        feature.kind = FeatureKind::Chamfer;
        feature.subType = touchesConcave ? QStringLiteral("inner_chamfer")
                                         : QStringLiteral("outer_chamfer");
        feature.center = estimateFaceCentroid(face);
        feature.axis = toVector3D(cone.Axis().Direction());
        feature.radius = cone.RefRadius();
        feature.coneAngle = std::abs(cone.SemiAngle()) * 2.0;
        feature.faceIndices = {faceIndex};

        const FaceProjection projection = estimateFaceProjection(face, cone.Axis().Direction());
        if (projection.valid) {
            feature.depth = std::abs(projection.maxProj - projection.minProj);
        }
        features.append(feature);
    }

    return features;
}

QVector<MachiningFeature> FeatureRecognizer::findFillets(const TopoGraph &graph) const
{
    QVector<MachiningFeature> features;

    for (int faceIndex = 1; faceIndex <= graph.faceMap.Extent(); ++faceIndex) {
        const TopoDS_Face face = TopoDS::Face(graph.faceMap(faceIndex));
        BRepAdaptor_Surface surface(face, false);
        const GeomAbs_SurfaceType type = surface.GetType();
        if (type != GeomAbs_Cylinder && type != GeomAbs_Torus) {
            continue;
        }

        int smoothNeighbors = 0;
        QVector<int> supportFaces;
        bool touchesConcave = false;
        for (const auto &entry : graph.aag.value(faceIndex)) {
            const int neighborIndex = entry.first;
            const EdgeAttr &edgeAttr = entry.second;
            if (edgeAttr.isSmooth) {
                ++smoothNeighbors;
                supportFaces.append(neighborIndex);
            }
            if (entry.second.isConcave) {
                touchesConcave = true;
            }
        }

        supportFaces = uniqueIndices(supportFaces);
        if (smoothNeighbors < 2 || supportFaces.size() < 2) {
            continue;
        }

        MachiningFeature feature;
        feature.kind = FeatureKind::Fillet;
        feature.subType = touchesConcave ? QStringLiteral("inner_fillet")
                                         : QStringLiteral("outer_fillet");
        feature.center = estimateFaceCentroid(face);
        feature.faceIndices = {faceIndex};

        if (type == GeomAbs_Cylinder) {
            const gp_Cylinder cylinder = surface.Cylinder();
            feature.axis = toVector3D(cylinder.Axis().Direction());
            feature.radius = cylinder.Radius();
            const FaceProjection projection = estimateFaceProjection(face, cylinder.Axis().Direction());
            if (projection.valid) {
                feature.length = std::abs(projection.maxProj - projection.minProj);
            }
        } else {
            const gp_Torus torus = surface.Torus();
            feature.axis = toVector3D(torus.Axis().Direction());
            feature.radius = torus.MinorRadius();
            feature.length = torus.MajorRadius() * 2.0;
        }

        features.append(feature);
    }

    return features;
}
#endif
