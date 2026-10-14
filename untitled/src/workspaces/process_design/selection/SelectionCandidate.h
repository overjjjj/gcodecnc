#pragma once

#include "SelectionChainController.h"
#include "../../../import/StepImporter.h"
#include <cmath>

struct SelectionSourcePlane
{
    bool verified = false;
    QVector3D normal;
    double offset = 0.0;
};

struct SelectionCandidate
{
    QString geometryId;
    QStringList faceIds;
    QString label;
    QVector3D referencePoint;
    QVector3D surfaceNormal = QVector3D(0, 0, 1);
    SelectionSourcePlane sourcePlane;
    ChainGeometrySource source = ChainGeometrySource::Entity;
    bool closed = false;
    bool topologyOnly = false;
    int outerLoopPointCount = 0;
    int islandCount = 0;
    int freeEndCount = 0;
    QVector<QVector3D> points;
    QStringList branchIds;
    QVector<MeshPickEdge> topologyEdges;
    QVector<MeshPickVertex> topologyVertices;
    QVector<MeshBoundaryLoop> boundaryLoops;
    QList<SelectionBoundaryLoop> machiningBoundaries;
    QMap<QString,QString> regionErrors;
};
