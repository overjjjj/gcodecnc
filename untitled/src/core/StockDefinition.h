#pragma once

#include <QString>
#include "Coordinate3D.h"

struct StockBounds
{
    Coordinate3D minimum;
    Coordinate3D maximum;

    Coordinate3D size() const { return maximum - minimum; }
};

struct StockDefinition
{
    double minusX = 0.0;
    double plusX = 0.0;
    double minusY = 0.0;
    double plusY = 0.0;
    double minusZ = 0.0;
    double plusZ = 0.0;
    bool confirmed = false;

    void normalize();
    StockBounds resolvedBounds(const Coordinate3D &partMin, const Coordinate3D &partMax) const;
    QString fingerprint() const;
};
