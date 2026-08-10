#include "FeatureDisplayFilter.h"

bool matchesFeatureDisplayFilter(FeatureKind kind, FeatureDisplayFilter filter)
{
    switch (filter) {
    case FeatureDisplayFilter::All:
        return true;
    case FeatureDisplayFilter::Hole:
        return kind == FeatureKind::Hole || kind == FeatureKind::Thread;
    case FeatureDisplayFilter::SlotPocket:
        return kind == FeatureKind::Slot || kind == FeatureKind::Pocket;
    case FeatureDisplayFilter::Plane:
        return kind == FeatureKind::FlatSurface;
    case FeatureDisplayFilter::ContourOther:
        return kind != FeatureKind::Hole
            && kind != FeatureKind::Thread
            && kind != FeatureKind::Slot
            && kind != FeatureKind::Pocket
            && kind != FeatureKind::FlatSurface;
    }
    return true;
}

QVector<int> filteredFeatureIndices(const QVector<MachiningFeature> &features,
                                    FeatureDisplayFilter filter)
{
    QVector<int> indices;
    for (int index = 0; index < features.size(); ++index) {
        if (matchesFeatureDisplayFilter(features[index].kind, filter)) {
            indices.append(index);
        }
    }
    return indices;
}
