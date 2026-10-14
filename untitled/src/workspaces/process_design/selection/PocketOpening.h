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
inline QVector3D machiningBoundaryCenter(const QVector<QVector3D> &points)
{
    QVector3D lo=points.first(),hi=lo;
    for(const auto &p:points)for(int axis=0;axis<3;++axis){lo[axis]=qMin(lo[axis],p[axis]);hi[axis]=qMax(hi[axis],p[axis]);}
    return (lo+hi)*.5f;
}

inline QString pocketOpeningOperationError(const SelectionChain &selection, const ContourFeature &feature,
                                           const StrategyParams &params, const QString &strategy,
                                           const MeshData *sourceMesh)
{
    const SelectionBoundaryLoop *opening = nullptr;
    for (const auto &loop : selection.boundaryLoops) if ((loop.machiningRole == MachiningBoundaryRole::Outer)) {
        if (opening || !selectionContainsCompleteOpening(selection,loop) || !openingEvidenceSupported(loop))
            return QStringLiteral("POCKET_OPENING_EVIDENCE_INVALID");
        opening = &loop;
    }
    if (!opening) return feature.subType == QStringLiteral("verified_slot_opening")
        ? QStringLiteral("POCKET_OPENING_EVIDENCE_REQUIRED") : QString();
    bool sourceVerified = false;
    const MeshBoundaryLoop *sourceOpening = nullptr;
    if (sourceMesh) for (const auto &source : sourceMesh->boundaryLoops) {
        if (source.faceId != opening->faceId || source.orderedEdgeIds != opening->orderedEdgeIds ||
            source.orderedForward != opening->orderedForward) continue;
        SelectionBoundaryLoop fresh;
        sourceVerified = verifyPocketOpening(*sourceMesh,source,&fresh,nullptr) && fresh == *opening;
        if(sourceVerified)sourceOpening=&source;
        break;
    }
    if (!sourceVerified) return QStringLiteral("POCKET_OPENING_SOURCE_PROOF_MISMATCH");
    const bool finishing=strategy=="mill_pocket_finish" || strategy=="mill_pocket_floor_finish";
    if ((strategy != QStringLiteral("mill_pocket_rough") && !finishing) || feature.subType != QStringLiteral("verified_slot_opening") ||
        feature.points != opening->machiningBoundary || !feature.planarBoundaryVerified || !feature.islands.isEmpty() ||
        (feature.axis-QVector3D(0,0,1)).length() > 1e-5f)
        return QStringLiteral("POCKET_OPENING_REGION_MISMATCH");
    const auto center = machiningBoundaryCenter(opening->machiningBoundary);
    if ((feature.center-center).length() > .001f) return QStringLiteral("POCKET_OPENING_REGION_MISMATCH");
    if(finishing && (!params.values.contains("sideStockToLeave") || !params.values.contains("floorStockToLeave") ||
                     params.get("entryMode",-1)!=0))
        return QStringLiteral("POCKET_FINISH_ALLOWANCES_AND_VERTICAL_ENTRY_REQUIRED");
    if (!pocketRoughingAllowances(params).valid(feature.depth))
        return QStringLiteral("POCKET_OPENING_NEGATIVE_ALLOWANCE");
    const double depth = params.get(QStringLiteral("depth"),0);
    double maximumDepth=opening->maximumDepth;
    if(finishing || (std::isfinite(depth) && depth>maximumDepth)) {
        double floorDepth=0;
        QString floorError;
        if(verifyPocketFlatFloor(*sourceMesh,*sourceOpening,*opening,&floorDepth,&floorError))maximumDepth=floorDepth;
        else if(finishing)return floorError;
        // A three-decimal depth may stop less than one output increment above the
        // source floor. Never round the authorized depth into material.
        if(strategy=="mill_pocket_floor_finish" && (depth>floorDepth || depth<=floorDepth-.001))
            return QObject::tr("底面精加工深度须到达已验证底深 %1 mm（允许向浅处取整不足 0.001 mm）；底面余量单独设置。").arg(floorDepth,0,'g',17);
    }
    if (!std::isfinite(depth) || depth <= 0 || depth > maximumDepth ||
        !std::isfinite(feature.depth) || std::abs(feature.depth-depth) > 1e-6)
        return QObject::tr("槽口加工深度必须明确输入，且不超过已验证深度 %1 mm；更深的斜底/下层区域未获验证。")
            .arg(maximumDepth,0,'f',3);
    return {};
}
