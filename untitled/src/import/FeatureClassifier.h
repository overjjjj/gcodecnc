#pragma once

#include "StepImporter.h"

#ifdef CNEXT_ENABLE_OCC
class FeatureClassifier
{
public:
    void classifyHole(MachiningFeature &feature, const TopoGraph &graph) const;
};
#endif
