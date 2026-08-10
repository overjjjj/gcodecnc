#pragma once

#include <QString>
#include <QVector3D>

struct StockBounds
{
    QVector3D minimum;
    QVector3D maximum;

    QVector3D size() const { return maximum - minimum; }
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
    StockBounds resolvedBounds(const QVector3D &partMin, const QVector3D &partMax) const;
    QString fingerprint() const;
};
