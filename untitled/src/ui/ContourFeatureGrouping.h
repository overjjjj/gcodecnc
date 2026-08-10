#pragma once

#include "../import/StepImporter.h"

#include <QString>
#include <QVector>

QString contourFeatureGroupId(const MachiningFeature &feature);
QString contourFeatureGroupLabel(const MachiningFeature &feature, bool chinese);
bool contourFeaturesShareGroup(const QVector<MachiningFeature> &features);
