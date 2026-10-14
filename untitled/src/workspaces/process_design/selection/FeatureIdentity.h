#pragma once

#include <QString>

struct MachiningFeature;
struct ContourFeature;

QString stableFeatureId(const MachiningFeature &feature);
QString stableContourId(const ContourFeature &feature);
