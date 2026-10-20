#pragma once

#include "SelectionChainController.h"
#include "../../../import/StepImporter.h"
#include "../../../strategies/StrategyBase.h"
#include "../../../strategies/mill/PocketAllowances.h"
#include <QSet>
#include <algorithm>
#include <limits>

// Checks a bounded prismatic region against the source solid under the OCC numeric contract.
inline bool isPocketRegionStrategy(const QString &strategy)
{
    return strategy=="mill_pocket_rough" || strategy=="mill_pocket_finish" || strategy=="mill_pocket_floor_finish";
}
bool verifyPocketOpening(const MeshData &mesh, const MeshBoundaryLoop &source,
                         SelectionBoundaryLoop *evidence, MachiningFeature *feature,
                         QString *error = nullptr);
// Separate source proof for contact with one horizontal floor; maximumDepth stays unchanged.
bool verifyPocketFlatFloor(const MeshData &mesh, const MeshBoundaryLoop &source,
                          const SelectionBoundaryLoop &opening, double *depth,
                          QString *error = nullptr, double *floorZ = nullptr);
bool verifyPocketThroughExit(const MeshData &mesh, const MeshBoundaryLoop &source,
                             const SelectionBoundaryLoop &opening, double *depth, QString *error = nullptr);
inline QVector3D machiningBoundaryCenter(const QVector<QVector3D> &points)
{
    QVector3D lo=points.first(),hi=lo;
    for(const auto &p:points)for(int axis=0;axis<3;++axis){lo[axis]=qMin(lo[axis],p[axis]);hi[axis]=qMax(hi[axis],p[axis]);}
    return (lo+hi)*.5f;
}

QString pocketOpeningOperationError(const SelectionChain &selection, const ContourFeature &feature,
                                           const StrategyParams &params, const QString &strategy,
                                           const MeshData *sourceMesh);
