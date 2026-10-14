#pragma once

#include "SelectionChainController.h"
#include "../../../import/StepImporter.h"
#include <QSet>

inline QVector<int> machiningPlaneFaceIndices(const MeshData &mesh,
                                               const QVector<int> &faceIndices,
                                               const QVector3D &toolAxis)
{
    QVector<int> result;
    if (toolAxis.lengthSquared() <= 1.0e-8f) return result;
    const QVector3D axis = toolAxis.normalized();
    for (int faceIndex : faceIndices) {
        if (faceIndex <= 0 || result.contains(faceIndex)) continue;
        bool facesTool = false;
        for (const Triangle &triangle : mesh.triangles) {
            if (triangle.faceIndex != faceIndex || triangle.normal.lengthSquared() <= 1.0e-8f)
                continue;
            if (QVector3D::dotProduct(triangle.normal.normalized(), axis) >= 0.65f) {
                facesTool = true;
                break;
            }
        }
        if (facesTool) result.append(faceIndex);
    }
    return result;
}

inline QSet<QString> machiningPlaneBoundaryEdgeIds(const MeshData &mesh,
                                                    const QVector<int> &faceIndices)
{
    QSet<int> faces;
    for (int faceIndex : faceIndices) faces.insert(faceIndex);
    QSet<QString> result;
    for (const MeshBoundaryLoop &loop : mesh.boundaryLoops) {
        if (!faces.contains(loop.faceIndex)) continue;
        for (const QString &edgeId : loop.orderedEdgeIds) result.insert(edgeId);
    }
    return result;
}
