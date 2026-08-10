#include "src/ui/FeatureDisplayFilter.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

void require(bool condition, const QString &message)
{
    if (condition) {
        return;
    }
    QTextStream(stderr) << "FAIL: " << message << Qt::endl;
    ::exit(1);
}

MachiningFeature feature(FeatureKind kind)
{
    MachiningFeature value;
    value.kind = kind;
    value.region = FaceRegion::Front;
    return value;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QVector<MachiningFeature> features = {
        feature(FeatureKind::Hole),
        feature(FeatureKind::Thread),
        feature(FeatureKind::Slot),
        feature(FeatureKind::Pocket),
        feature(FeatureKind::FlatSurface),
        feature(FeatureKind::Boss),
        feature(FeatureKind::Chamfer)
    };

    require(filteredFeatureIndices(features, FeatureDisplayFilter::All).size() == 7,
            QStringLiteral("all filter must show every recognized feature"));

    require(filteredFeatureIndices(features, FeatureDisplayFilter::Hole) == QVector<int>({0, 1}),
            QStringLiteral("hole filter must include holes and threaded holes"));

    require(filteredFeatureIndices(features, FeatureDisplayFilter::SlotPocket) == QVector<int>({2, 3}),
            QStringLiteral("slot filter must include slots and pockets"));

    require(filteredFeatureIndices(features, FeatureDisplayFilter::Plane) == QVector<int>({4}),
            QStringLiteral("plane filter must include flat surfaces only"));

    require(filteredFeatureIndices(features, FeatureDisplayFilter::ContourOther) == QVector<int>({5, 6}),
            QStringLiteral("contour filter must contain remaining contour features"));

    QTextStream(stdout) << "PASS feature_list_filter_test" << Qt::endl;
    return 0;
}
