#pragma once

#include "ToolEntry.h"
#include <cmath>
#include <limits>

// Ideal straight cone/frustum, measured from the flat tip plane.
inline double chamferConeHeight(const ToolEntry &tool)
{
    bool angleOk = false, tipOk = false;
    const double angle = tool.extra.value("angle").toDouble(&angleOk);
    const double tip = tool.extra.value("tipDiameter").toDouble(&tipOk);
    if (tool.type != "chamfer_mill" || !tool.modelPath.isEmpty() ||
        !angleOk || !tipOk || !std::isfinite(angle) || angle <= 0 || angle >= 180 ||
        !std::isfinite(tip) || tip < 0 || !std::isfinite(tool.diameter) || tool.diameter <= tip)
        return std::numeric_limits<double>::quiet_NaN();
    const double height = (tool.diameter - tip) * 0.5
        / std::tan(angle * 3.14159265358979323846 / 360.0);
    if (!std::isfinite(height) || height <= 0 || !std::isfinite(tool.fluteLen) ||
        tool.fluteLen < height || !std::isfinite(tool.totalLen) ||
        tool.totalLen <= height || tool.totalLen < tool.fluteLen)
        return std::numeric_limits<double>::quiet_NaN();
    return height;
}
