#pragma once

#include "StepImporter.h"
#include "../core/ProcessContext.h"

#include <QStringList>

enum class SlotFrameKind {
    ClosedPocket,
    OpenSlot
};

enum class SlotFrameComplementType {
    None,
    DirectBridge,
    PerpendicularBridge,
    ExtendAndBridge
};

struct SlotFrameFreeEdge {
    QVector3D start;
    QVector3D end;
    int adjacentFaceCount = 0;
};

struct SlotFrameFeature {
    QString geometryRef;
    SetupContext setup;
    SlotFrameKind kind = SlotFrameKind::ClosedPocket;
    QVector3D axis = QVector3D(0.0f, 0.0f, 1.0f);
    FaceRegion region = FaceRegion::Unknown;
    QVector<int> bottomFaceIndices;
    QVector<int> sideFaceIndices;
    QVector<QVector3D> outerBoundary;
    QVector<SlotFrameFreeEdge> freeEdges;
    bool closed = false;
    SlotFrameComplementType complementType = SlotFrameComplementType::None;
    double depth = 0.0;
    QVector<QVector<QVector3D>> islands;
    QVector<QVector<QVector3D>> protectionRegions;
    double confidence = 0.0;
    QStringList warnings;
    QStringList rejectionReasons;
    ContourFeature machiningGeometry;
};

struct SlotFrameTopologyEvidence {
    bool bottomPlanar = false;
    bool sideWallsPlanar = false;
    bool bottomBoundaryComplete = false;
    bool intersecting = false;
    QVector<int> edgeFaceCounts;
    QVector<int> vertexEdgeCounts;
    QVector<SlotFrameFreeEdge> freeEdges;
    QVector<QVector<QVector3D>> protectionRegions;
};
