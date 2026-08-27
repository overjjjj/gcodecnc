#include "CompoundHoleRecognizer.h"

#include "../core/FeatureIdentity.h"

#include <cmath>

namespace {

constexpr double kGeometryTolerance = 1.0e-4;

bool isCountersink(const MachiningFeature &feature)
{
    return feature.subType.startsWith(QStringLiteral("countersunk_"));
}

bool isCounterbore(const MachiningFeature &feature)
{
    return feature.subType.startsWith(QStringLiteral("counterbore_"));
}

bool isThrough(const MachiningFeature &feature)
{
    return feature.subType.contains(QStringLiteral("through_hole"));
}

} // namespace

CompoundHoleRecognitionResult CompoundHoleRecognizer::Recognize(
    const MachiningFeature &feature)
{
    CompoundHoleRecognitionResult result;
    if (feature.kind != FeatureKind::Hole || feature.radius <= 0.0 ||
        feature.depth <= 0.0 || feature.axis.lengthSquared() <= 1.0e-8f) {
        result.reasons.append(QStringLiteral("Recognized feature is not a valid hole."));
        return result;
    }

    CompoundHoleFeature compound;
    compound.geometryRef = stableFeatureId(feature);
    compound.axis = feature.axis.normalized();
    compound.region = feature.region;
    compound.confidence = 0.90;
    compound.entryPoint = feature.center;
    const bool through = isThrough(feature);

    if (isCounterbore(feature)) {
        if (feature.secondaryRadius <= feature.radius + kGeometryTolerance ||
            feature.secondaryDepth <= kGeometryTolerance ||
            feature.secondaryDepth >= feature.depth - kGeometryTolerance) {
            result.reasons.append(
                QStringLiteral("Counterbore outer-layer diameter or depth is not verified."));
            return result;
        }
        compound.layers = {
            {HoleLayerKind::Cylindrical, feature.secondaryRadius * 2.0,
             compound.entryPoint.z(), compound.entryPoint.z() - feature.secondaryDepth, false},
            {HoleLayerKind::Cylindrical, feature.radius * 2.0,
             compound.entryPoint.z() - feature.secondaryDepth,
             compound.entryPoint.z() - feature.depth, through}};
    } else if (isCountersink(feature)) {
        const double radiusDifference = feature.secondaryRadius - feature.radius;
        const double halfAngle = feature.coneAngle * std::acos(-1.0) / 360.0;
        if (radiusDifference <= kGeometryTolerance || halfAngle <= kGeometryTolerance ||
            halfAngle >= std::acos(-1.0) * 0.5 - kGeometryTolerance) {
            result.reasons.append(
                QStringLiteral("Countersink diameter or cone angle is not verified."));
            return result;
        }
        const double countersinkDepth = radiusDifference / std::tan(halfAngle);
        if (!std::isfinite(countersinkDepth) || countersinkDepth <= kGeometryTolerance) {
            result.reasons.append(QStringLiteral("Countersink depth cannot be derived safely."));
            return result;
        }
        compound.entryPoint.setZ(float(feature.center.z() + feature.depth * 0.5 +
                                       countersinkDepth));
        compound.layers = {
            {HoleLayerKind::ConicalCountersink, feature.secondaryRadius * 2.0,
             compound.entryPoint.z(), compound.entryPoint.z() - countersinkDepth, false},
            {HoleLayerKind::Cylindrical, feature.radius * 2.0,
             compound.entryPoint.z() - countersinkDepth,
             compound.entryPoint.z() - countersinkDepth - feature.depth, through}};
    } else {
        compound.entryPoint.setZ(float(feature.center.z() + feature.depth * 0.5));
        compound.layers = {{HoleLayerKind::Cylindrical, feature.radius * 2.0,
                            compound.entryPoint.z(),
                            compound.entryPoint.z() - feature.depth, through}};
    }

    result.reasons = compound.validateForFrontSetup();
    if (!result.reasons.isEmpty()) {
        return result;
    }
    result.ok = true;
    result.feature = compound;
    return result;
}
