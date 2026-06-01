#pragma once

#include "StepImporter.h"

#ifdef CNEXT_ENABLE_OCC
class TopoDS_Shape;

class TopoAnalyzer
{
public:
    TopoGraph analyze(const TopoDS_Shape &shape) const;
};
#endif
