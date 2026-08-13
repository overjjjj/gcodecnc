#include "FeatureClassifier.h"

#ifdef CNEXT_ENABLE_OCC
void FeatureClassifier::classifyHole(MachiningFeature &feature, const TopoGraph &graph) const
{
    int planarCaps = 0;
    bool hasAdjacentCone = false;

    for (const auto &entry : graph.aag.value(feature.faceIndices.value(0))) {
        const int neighborIndex = entry.first;
        const FaceNode &neighborNode = graph.nodes[neighborIndex - 1];

        if (!feature.faceIndices.contains(neighborIndex)) {
            continue;
        }
        if (neighborNode.surfType == GeomAbs_Plane) {
            ++planarCaps;
        } else if (neighborNode.surfType == GeomAbs_Cone) {
            hasAdjacentCone = true;
        }
    }

    feature.kind = FeatureKind::Hole;
    feature.pitch = 0.0;
    const bool hasCone = hasAdjacentCone &&
                         feature.secondaryRadius > feature.radius + 0.05;
    if (planarCaps == 0) {
        feature.subType = hasCone ? QStringLiteral("countersunk_through_hole")
                                  : QStringLiteral("through_hole");
    } else {
        feature.subType = hasCone ? QStringLiteral("countersunk_blind_hole")
                                  : QStringLiteral("blind_hole");
    }
}
#endif
