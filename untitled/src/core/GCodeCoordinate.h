#pragma once

#include <QString>

constexpr int kGCodeCoordinateDecimals = 6;

inline QString formatGCodeCoordinate(double value)
{
    return QString::number(value, 'f', kGCodeCoordinateDecimals);
}
