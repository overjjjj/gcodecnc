#include "../src/import/CompoundHoleRecognizer.h"

#include <QCoreApplication>

#include <cmath>
#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

MachiningFeature countersinkFeature()
{
    MachiningFeature feature;
    feature.kind = FeatureKind::Hole;
    feature.subType = QStringLiteral("countersunk_blind_hole");
    feature.radius = 3.0;
    feature.secondaryRadius = 5.0;
    feature.depth = 12.0;
    feature.coneAngle = 90.0;
    feature.center = QVector3D(10.0f, 20.0f, -8.0f);
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.region = FaceRegion::Front;
    return feature;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const CompoundHoleRecognitionResult countersink =
        CompoundHoleRecognizer::Recognize(countersinkFeature());
    if (!expect(countersink.ok && countersink.feature.layers.size() == 2 &&
                    countersink.feature.layers.first().kind ==
                        HoleLayerKind::ConicalCountersink &&
                    std::abs(countersink.feature.layers.first().depth() - 2.0) < 1.0e-6 &&
                    std::abs(countersink.feature.entryPoint.z()) < 1.0e-6,
                "verified countersink diameter and cone angle must produce traced conical and core layers")) {
        return 1;
    }

    MachiningFeature counterbore = countersinkFeature();
    counterbore.subType = QStringLiteral("counterbore_blind_hole");
    counterbore.secondaryDepth = 2.0;
    counterbore.coneAngle = 0.0;
    counterbore.center = QVector3D(10.0f, 20.0f, 0.0f);
    counterbore.depth = 14.0;
    const CompoundHoleRecognitionResult stepped =
        CompoundHoleRecognizer::Recognize(counterbore);
    if (!expect(stepped.ok && stepped.feature.layers.size() == 2 &&
                    stepped.feature.layers.first().kind == HoleLayerKind::Cylindrical &&
                    std::abs(stepped.feature.layers.first().depth() - 2.0) < 1.0e-6 &&
                    std::abs(stepped.feature.layers.last().depth() - 12.0) < 1.0e-6,
                "counterbore recognition requires and preserves the verified outer-layer depth")) {
        return 1;
    }

    MachiningFeature missingDepth = counterbore;
    missingDepth.secondaryDepth = 0.0;
    const CompoundHoleRecognitionResult rejected =
        CompoundHoleRecognizer::Recognize(missingDepth);
    if (!expect(!rejected.ok && !rejected.reasons.isEmpty(),
                "counterbore without an outer-layer depth must be rejected, not inferred")) {
        return 1;
    }
    return 0;
}
