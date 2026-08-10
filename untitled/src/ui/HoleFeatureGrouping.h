#pragma once

#include "../import/StepImporter.h"

#include <QString>
#include <QVector>

QString holeFeatureGroupId(const MachiningFeature &feature);
QString holeFeatureGroupLabel(const MachiningFeature &feature, bool chinese);
bool holeFeaturesShareGroup(const QVector<MachiningFeature> &features);
