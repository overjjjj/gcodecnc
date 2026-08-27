#include "CompoundHoleFeature.h"

#include <cmath>

namespace {

constexpr double kGeometryTolerance = 1.0e-4;

bool isFinite(double value)
{
    return std::isfinite(value);
}

} // namespace

double HoleLayer::depth() const
{
    return startZ - endZ;
}

QStringList CompoundHoleFeature::validateForFrontSetup() const
{
    QStringList errors;
    if (geometryRef.trimmed().isEmpty()) {
        errors.append(QStringLiteral("Compound hole requires a stable geometry reference."));
    }
    if (region != FaceRegion::Front) {
        errors.append(QStringLiteral("Compound hole requires the active front-face Setup."));
    }
    if (axis.lengthSquared() <= 1.0e-8f || axis.normalized().z() < 0.999f) {
        errors.append(QStringLiteral("Compound hole axis must align with the front Setup +Z axis."));
    }
    if (!isFinite(confidence) || confidence < 0.0 || confidence > 1.0) {
        errors.append(QStringLiteral("Compound-hole recognition confidence is invalid."));
    }
    if (layers.isEmpty()) {
        errors.append(QStringLiteral("Compound hole has no verified layers."));
        return errors;
    }

    double previousEndZ = entryPoint.z();
    for (int index = 0; index < layers.size(); ++index) {
        const HoleLayer &layer = layers.at(index);
        if (!isFinite(layer.diameter) || layer.diameter <= 0.0 ||
            !isFinite(layer.startZ) || !isFinite(layer.endZ) ||
            layer.depth() <= kGeometryTolerance) {
            errors.append(QStringLiteral("Compound-hole layer %1 has invalid diameter or depth.")
                              .arg(index + 1));
            continue;
        }
        if (std::abs(layer.startZ - previousEndZ) > kGeometryTolerance) {
            errors.append(QStringLiteral("Compound-hole layers must be contiguous and non-overlapping."));
        }
        if (index + 1 < layers.size() && layer.through) {
            errors.append(QStringLiteral("Only the deepest compound-hole layer may be through."));
        }
        previousEndZ = layer.endZ;
    }
    return errors;
}

double CompoundHoleFeature::totalDepth() const
{
    return layers.isEmpty() ? 0.0 : entryPoint.z() - layers.last().endZ;
}

bool CompoundHoleFeature::isThrough() const
{
    return !layers.isEmpty() && layers.last().through;
}
