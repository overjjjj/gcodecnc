#pragma once

#include "SelectionChainController.h"

struct SelectionCandidate
{
    QString geometryId;
    QString label;
    QVector3D referencePoint;
    QVector3D surfaceNormal = QVector3D(0, 0, 1);
    ChainGeometrySource source = ChainGeometrySource::Entity;
    bool closed = false;
    int outerLoopPointCount = 0;
    int islandCount = 0;
    int freeEndCount = 0;
    QVector<QVector3D> points;
    QStringList branchIds;
};
