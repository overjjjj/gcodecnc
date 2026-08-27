#pragma once

#include "../StrategyBase.h"

ProcessParameterSchema contourMillingParameterSchema(bool closedContour);
StrategyParams contourMillingDefaultParams(bool closedContour);
QString validateContourMillingContract(const ContourFeature &feature,
                                       const ToolEntry &tool,
                                       const StrategyParams &params,
                                       bool closedContour);
double contourMillingEffectiveDepth(const ContourFeature &feature,
                                    const StrategyParams &params);
