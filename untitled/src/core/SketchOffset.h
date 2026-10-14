#pragma once
#include "SketchDocument.h"

struct SketchOffsetResult { QString error; SketchDocument document; };
SketchOffsetResult sketchOffset(const SketchDocument &document,int target,double distance,const QString &side);
