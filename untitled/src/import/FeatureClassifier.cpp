#include "FeatureClassifier.h"

#ifdef CNEXT_ENABLE_OCC
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <Precision.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Cone.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>
#include <cmath>

namespace {
bool hasApexVertex(const TopoDS_Face &face, const gp_Pnt &apex)
{
    for (TopExp_Explorer it(face, TopAbs_VERTEX); it.More(); it.Next()) {
        const auto vertex = TopoDS::Vertex(it.Current());
        const double tolerance = BRep_Tool::Tolerance(vertex) +
                                 BRep_Tool::Tolerance(face) + Precision::Confusion();
        if (BRep_Tool::Pnt(vertex).Distance(apex) <= tolerance) return true;
    }
    return false;
}
}

// 函数说明：classifyHole负责当前模块的核心处理。
void FeatureClassifier::classifyHole(MachiningFeature &feature, const TopoGraph &graph) const
{
    int planarCaps = 0;
    bool hasAdjacentCone = false;
    bool hasConicalCap = false;
    int cappedEnds = 0;
    const gp_Pnt center(feature.center.x(), feature.center.y(), feature.center.z());
    const gp_Vec axis(feature.axis.x(), feature.axis.y(), feature.axis.z());
    auto recordCap = [&](double projection) {
        if (projection > Precision::Confusion()) cappedEnds |= 1;
        else if (projection < -Precision::Confusion()) cappedEnds |= 2;
    };

    // The recognizer stores compact planar caps and aligned cones from all
    // cylinder pieces, including caps across a bottom fillet.
    for (int neighborIndex : feature.faceIndices) {
        const FaceNode &neighborNode = graph.nodes[neighborIndex - 1];
        const auto face = TopoDS::Face(graph.faceMap(neighborIndex));
        BRepAdaptor_Surface surface(face);
        if (neighborNode.surfType == GeomAbs_Plane) {
            ++planarCaps;
            const auto plane = surface.Plane();
            const gp_Vec normal(plane.Axis().Direction());
            const double alignment = axis.Dot(normal);
            if (std::abs(alignment) > 0.9)
                recordCap(gp_Vec(center, plane.Location()).Dot(normal) / alignment);
        } else if (neighborNode.surfType == GeomAbs_Cone) {
            hasAdjacentCone = true;
            const auto apex = surface.Cone().Apex();
            if (hasApexVertex(face, apex)) {
                hasConicalCap = true;
                recordCap(gp_Vec(center, apex).Dot(axis));
            }
        }
    }

    feature.kind = FeatureKind::Hole;
    feature.pitch = 0.0;
    if (cappedEnds == 3) {
        feature.subType = QStringLiteral("enclosed_hole");
        return;
    }
    // A cylinder's parameter-axis sign is arbitrary. For a conical end cap,
    // orient it toward the closed end so its opposite identifies the opening.
    if (hasConicalCap && cappedEnds == 2) feature.axis = -feature.axis;
    const bool hasCone = hasAdjacentCone &&
                         feature.secondaryRadius > feature.radius + 0.05;
    if (planarCaps == 0 && !hasConicalCap) {
        feature.subType = hasCone ? QStringLiteral("countersunk_through_hole")
                                  : QStringLiteral("through_hole");
    } else {
        feature.subType = hasCone ? QStringLiteral("countersunk_blind_hole")
                                  : QStringLiteral("blind_hole");
    }
}
#endif
