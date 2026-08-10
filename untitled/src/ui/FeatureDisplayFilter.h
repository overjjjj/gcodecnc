#pragma once

#include "../import/StepImporter.h"

#include <QVector>

enum class FeatureDisplayFilter {
    All,
    Hole,
    SlotPocket,
    Plane,
    ContourOther
};

bool matchesFeatureDisplayFilter(FeatureKind kind, FeatureDisplayFilter filter);
QVector<int> filteredFeatureIndices(const QVector<MachiningFeature> &features,
                                    FeatureDisplayFilter filter);
