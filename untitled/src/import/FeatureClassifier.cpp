#include "FeatureClassifier.h"

#ifdef CNEXT_ENABLE_OCC
#include <cmath>

namespace {

static bool isMetricThreadDiameter(double diameter)
{
    static const double metricDiameters[] = {
        1.0, 1.2, 1.4, 1.6, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0, 12.0, 14.0,
        16.0, 18.0, 20.0, 22.0, 24.0, 27.0, 30.0, 36.0, 42.0, 48.0
    };
    for (double standardDiameter : metricDiameters) {
        if (std::abs(diameter - standardDiameter) <= 0.05) {
            return true;
        }
    }
    return false;
}

static double defaultMetricThreadPitch(double diameter)
{
    struct Entry { double diameter; double pitch; };
    static const Entry coarseThreads[] = {
        {1.0, 0.25}, {1.2, 0.25}, {1.4, 0.3}, {1.6, 0.35}, {2.0, 0.40}, {2.5, 0.45},
        {3.0, 0.5}, {4.0, 0.7}, {5.0, 0.8}, {6.0, 1.0},
        {8.0, 1.25}, {10.0, 1.5}, {12.0, 1.75}, {14.0, 2.0},
        {16.0, 2.0}, {18.0, 2.5}, {20.0, 2.5}, {22.0, 2.5},
        {24.0, 3.0}, {27.0, 3.0}, {30.0, 3.5},
        {36.0, 4.0}, {42.0, 4.5}, {48.0, 5.0}
    };
    for (const Entry &entry : coarseThreads) {
        if (std::abs(diameter - entry.diameter) <= 0.08) {
            return entry.pitch;
        }
    }
    return 0.0;
}

} // namespace

void FeatureClassifier::classifyHole(MachiningFeature &feature, const TopoGraph &graph) const
{
    int planarCaps = 0;
    bool hasCone = false;

    for (const auto &entry : graph.aag.value(feature.faceIndices.value(0))) {
        const int neighborIndex = entry.first;
        const FaceNode &neighborNode = graph.nodes[neighborIndex - 1];

        if (!feature.faceIndices.contains(neighborIndex)) {
            continue;
        }
        if (neighborNode.surfType == GeomAbs_Plane) {
            ++planarCaps;
        } else if (neighborNode.surfType == GeomAbs_Cone) {
            hasCone = true;
        }
    }

    if (isMetricThreadDiameter(feature.radius * 2.0) && feature.depth >= feature.radius * 2.0) {
        feature.kind = FeatureKind::Thread;
        feature.subType = QStringLiteral("tapped_hole");
        feature.pitch = defaultMetricThreadPitch(feature.radius * 2.0);
        return;
    }

    feature.kind = FeatureKind::Hole;
    if (planarCaps == 0) {
        feature.subType = hasCone ? QStringLiteral("countersunk_through_hole")
                                  : QStringLiteral("through_hole");
    } else {
        feature.subType = hasCone ? QStringLiteral("countersunk_blind_hole")
                                  : QStringLiteral("blind_hole");
    }
}
#endif
