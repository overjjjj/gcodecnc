#pragma once

#include "../../import/StepImporter.h"
#include "../../tool/ToolEntry.h"
#include <QString>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <limits>

struct HoleZRange {
    double entryZ = 0.0;
    double retractZ = 0.0;
    double bottomZ = 0.0;
    double direction = -1.0;
};

inline HoleZRange holeZRange(const HoleFeature &feature, double cutDepth, double feedHeight)
{
    HoleZRange range;
    const double machiningDepth = std::max(cutDepth, 0.0);
    const double holeDepth = feature.depth > 0.0 ? feature.depth : machiningDepth;
    const double halfHoleDepth = holeDepth * 0.5;

    // The current UI/G-code pipeline is a single Z setup. Back-face holes are
    // recognized for grouping, but fixture rotation is not applied yet, so the
    // generated toolpath must still drill from the current top toward negative Z.
    range.direction = -1.0;
    range.entryZ = feature.center.z() - range.direction * halfHoleDepth;
    range.retractZ = range.entryZ - range.direction * feedHeight;
    range.bottomZ = range.entryZ + range.direction * machiningDepth;
    return range;
}

// Returns the effective drill depth for a feature, adapting for hole subType.
// Through holes drill 1 mm past the bottom face to ensure clean breakthrough.
// Tapped holes and countersunk holes use the feature depth unchanged.
inline double effectiveDrillDepth(const HoleFeature &feature, double paramDepth)
{
    const double base = feature.depth > 0.0 ? feature.depth : paramDepth;
    if (feature.subType == QStringLiteral("through_hole") ||
        feature.subType == QStringLiteral("countersunk_through_hole")) {
        return base + 1.0; // breakthrough allowance
    }
    return base;
}

// Returns the recommended peck depth for a feature.
// Deep holes (depth/diameter > 3) use smaller pecks for better chip evacuation.
inline double effectivePeckDepth(const HoleFeature &feature, double paramPeck)
{
    if (feature.radius <= 0.0) return paramPeck;
    const double depthDiameterRatio = feature.depth / (feature.radius * 2.0);
    if (depthDiameterRatio > 5.0)
        return std::min(paramPeck, feature.radius * 0.5); // very deep: peck <= 0.5*D
    if (depthDiameterRatio > 3.0)
        return std::min(paramPeck, feature.radius);       // deep: peck <= 1*D
    return paramPeck;
}

inline double holeDiameter(const HoleFeature &feature)
{
    return feature.radius > 0.0 ? feature.radius * 2.0 : 0.0;
}

inline QString drillUndersizeComment(const HoleFeature &feature, const ToolEntry &tool)
{
    const double targetDia = holeDiameter(feature);
    if (targetDia <= 0.0 || tool.diameter <= 0.0) {
        return QString();
    }
    const double diff = targetDia - tool.diameter;
    if (diff > 0.5) {
        return QStringLiteral("; NOTE: Tool D%1 is smaller than target hole D%2. This operation drills a pilot/rough hole only.\n")
            .arg(tool.diameter, 0, 'f', 3)
            .arg(targetDia, 0, 'f', 3);
    }
    if (diff < -0.01) {
        return QStringLiteral("; WARNING: Tool D%1 is larger than target hole D%2.\n")
            .arg(tool.diameter, 0, 'f', 3)
            .arg(targetDia, 0, 'f', 3);
    }
    return QString();
}

inline QString validateReamerForHole(const HoleFeature &feature, const ToolEntry &tool)
{
    const double targetDia = holeDiameter(feature);
    if (targetDia <= 0.0 || tool.diameter <= 0.0) {
        return QStringLiteral("铰孔缺少有效孔径或刀具直径。");
    }
    const double diff = std::abs(targetDia - tool.diameter);
    if (diff > 0.05) {
        return QStringLiteral("铰孔刀具直径 D%1 与目标孔径 D%2 不匹配。铰孔应使用接近成品孔径的铰刀。")
            .arg(tool.diameter, 0, 'f', 3)
            .arg(targetDia, 0, 'f', 3);
    }
    return QString();
}

inline QString validateTapForHole(const HoleFeature &feature, const ToolEntry &tool)
{
    const double targetDia = holeDiameter(feature);
    if (tool.pitch <= 0.0) {
        return QStringLiteral("攻丝刀具缺少螺距信息。");
    }
    if (targetDia > 0.0 && tool.diameter > 0.0 && std::abs(targetDia - tool.diameter) > 0.2) {
        return QStringLiteral("攻丝刀具 D%1 与识别螺纹孔 D%2 不匹配。")
            .arg(tool.diameter, 0, 'f', 3)
            .arg(targetDia, 0, 'f', 3);
    }
    return QString();
}

// Reorder holes using a greedy nearest-neighbor heuristic starting from the
// first hole in the input list. Minimises total XY rapid travel distance.
inline QVector<HoleFeature> sortHolesByNearestNeighbor(QVector<HoleFeature> holes)
{
    if (holes.size() <= 1)
        return holes;

    QVector<HoleFeature> sorted;
    sorted.reserve(holes.size());
    sorted.append(holes.takeFirst());

    while (!holes.isEmpty()) {
        const QVector3D &last = sorted.last().center;
        float bestDist = std::numeric_limits<float>::max();
        int   bestIdx  = 0;

        for (int i = 0; i < holes.size(); ++i) {
            const QVector3D d = holes[i].center - last;
            const float dist  = d.x() * d.x() + d.y() * d.y(); // XY only
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx  = i;
            }
        }
        sorted.append(holes.takeAt(bestIdx));
    }
    return sorted;
}
