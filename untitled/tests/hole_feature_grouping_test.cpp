#include "src/ui/HoleFeatureGrouping.h"

#include <QCoreApplication>
#include <QTextStream>

#include <cstdlib>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << Qt::endl;
        std::exit(1);
    }
}

MachiningFeature hole(const QString &subType,
                      double diameter,
                      double depth,
                      double pitch = 0.0)
{
    MachiningFeature feature;
    feature.kind = pitch > 0.0 ? FeatureKind::Thread : FeatureKind::Hole;
    feature.subType = subType;
    feature.radius = diameter * 0.5;
    feature.depth = depth;
    feature.pitch = pitch;
    return feature;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const MachiningFeature throughA = hole(QStringLiteral("through_hole"), 10.0, 12.0);
    const MachiningFeature throughB = hole(QStringLiteral("through_hole"), 10.0, 12.0);
    const MachiningFeature smaller = hole(QStringLiteral("through_hole"), 8.0, 12.0);
    const MachiningFeature blind = hole(QStringLiteral("blind_hole"), 10.0, 12.0);
    const MachiningFeature tappedA = hole(QStringLiteral("tapped_hole"), 8.5, 16.0, 1.5);
    const MachiningFeature tappedB = hole(QStringLiteral("tapped_hole"), 8.5, 16.0, 1.25);

    require(holeFeatureGroupId(throughA) == holeFeatureGroupId(throughB),
            "same hole type and dimensions should share one group");
    require(holeFeatureGroupId(throughA) != holeFeatureGroupId(smaller),
            "diameter should split hole groups");
    require(holeFeatureGroupId(throughA) != holeFeatureGroupId(blind),
            "through and blind holes should not share a group");
    require(holeFeatureGroupId(tappedA) != holeFeatureGroupId(tappedB),
            "thread pitch should split tapped-hole groups");
    require(holeFeatureGroupLabel(tappedA, true).contains(QStringLiteral("螺距1.50")),
            "Chinese tapped-hole group label should expose pitch");
    require(holeFeaturesShareGroup({throughA, throughB}),
            "matching holes should be eligible for one batch");
    require(!holeFeaturesShareGroup({throughA, blind}),
            "mixed hole groups should not be eligible for one non-spot batch");

    MachiningFeature slot;
    slot.kind = FeatureKind::Slot;
    require(holeFeatureGroupId(slot).isEmpty(),
            "non-hole features must not receive a hole group id");
    require(!holeFeaturesShareGroup({throughA, slot}),
            "a non-hole feature should invalidate a hole batch");

    QTextStream(stdout) << "PASS hole_feature_grouping_test" << Qt::endl;
    return 0;
}
