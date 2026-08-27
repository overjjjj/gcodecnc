#pragma once

#include "../core/CompoundHoleFeature.h"

struct CompoundHoleRecognitionResult {
    bool ok = false;
    CompoundHoleFeature feature;
    QStringList reasons;
};

class CompoundHoleRecognizer
{
public:
    static CompoundHoleRecognitionResult Recognize(const MachiningFeature &feature);
};
