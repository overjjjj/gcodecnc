#include "src/ui/ContourFeatureGrouping.h"

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

MachiningFeature slot(const QString &subType,
                      double length,
                      double width,
                      double depth,
                      double angle,
                      FaceRegion region = FaceRegion::Front)
{
    MachiningFeature feature;
    feature.kind = FeatureKind::Slot;
    feature.subType = subType;
    feature.length = length;
    feature.width = width;
    feature.depth = depth;
    feature.angle = angle;
    feature.region = region;
    return feature;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const MachiningFeature slotA = slot(QStringLiteral("straight_slot"), 40.0, 8.0, 5.0, 0.0);
    const MachiningFeature slotB = slot(QStringLiteral("straight_slot"), 40.0, 8.0, 5.0, 0.0);
    const MachiningFeature rotated = slot(QStringLiteral("straight_slot"), 40.0, 8.0, 5.0, 90.0);
    const MachiningFeature open = slot(QStringLiteral("open_slot"), 40.0, 8.0, 5.0, 0.0);
    const MachiningFeature deeper = slot(QStringLiteral("straight_slot"), 40.0, 8.0, 8.0, 0.0);
    const MachiningFeature otherSetup = slot(QStringLiteral("straight_slot"), 40.0, 8.0, 5.0, 0.0,
                                               FaceRegion::Side);

    require(contourFeatureGroupId(slotA) == contourFeatureGroupId(slotB),
            "matching slots should share one stable batch group");
    require(contourFeatureGroupId(slotA) != contourFeatureGroupId(rotated),
            "slot angle should split batch groups");
    require(contourFeatureGroupId(slotA) != contourFeatureGroupId(open),
            "open state should split batch groups");
    require(contourFeatureGroupId(slotA) != contourFeatureGroupId(deeper),
            "slot dimensions should split batch groups");
    require(contourFeatureGroupId(slotA) != contourFeatureGroupId(otherSetup),
            "machining region should split batch groups");
    require(contourFeatureGroupLabel(slotA, false).contains(QStringLiteral("L40.00")),
            "slot group label should expose length");
    require(contourFeatureGroupLabel(slotA, false).contains(QStringLiteral("A0.00")),
            "slot group label should expose angle");
    require(contourFeaturesShareGroup({slotA, slotB}),
            "matching slots should be eligible for one batch");
    require(!contourFeaturesShareGroup({slotA, rotated}),
            "mixed-angle slots should be rejected as one batch");

    MachiningFeature pocket;
    pocket.kind = FeatureKind::Pocket;
    pocket.subType = QStringLiteral("rectangular_pocket");
    pocket.length = 30.0;
    pocket.width = 20.0;
    pocket.depth = 6.0;
    require(!contourFeatureGroupId(pocket).isEmpty(),
            "pockets should receive a stable display group");
    require(contourFeatureGroupId(slotA) != contourFeatureGroupId(pocket),
            "slots and pockets must never share a batch group");

    MachiningFeature irregularA = pocket;
    irregularA.subType = QStringLiteral("irregular_pocket");
    irregularA.boundaryPoints = {
        QVector3D(0, 0, 0), QVector3D(30, 0, 0),
        QVector3D(24, 20, 0), QVector3D(0, 20, 0)
    };
    MachiningFeature irregularB = irregularA;
    irregularB.boundaryPoints[2].setX(20.0f);
    require(contourFeatureGroupId(irregularA) != contourFeatureGroupId(irregularB),
            "irregular pockets with different boundaries must not share a batch group");

    irregularB = irregularA;
    irregularB.islandBoundaries = {{
        QVector3D(10, 8, 0), QVector3D(14, 8, 0),
        QVector3D(14, 12, 0), QVector3D(10, 12, 0)
    }};
    require(contourFeatureGroupId(irregularA) != contourFeatureGroupId(irregularB),
            "pockets with different island topology must not share a batch group");

    MachiningFeature hole;
    hole.kind = FeatureKind::Hole;
    require(contourFeatureGroupId(hole).isEmpty(),
            "holes must not receive a contour group id");
    require(!contourFeaturesShareGroup({slotA, hole}),
            "a hole should invalidate a contour batch");

    QTextStream(stdout) << "PASS contour_feature_grouping_test" << Qt::endl;
    return 0;
}
