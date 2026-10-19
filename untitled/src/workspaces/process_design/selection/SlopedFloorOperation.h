#pragma once
#include "SlopedFloorBinding.h"
#include "../operations/MachiningInput.h"

inline QString slopedFloorOperationError(const SelectionChain &selection,const ContourFeature &feature,
                                        const QString &strategy,const MeshData *mesh)
{
    if(strategy!="mill_sloped_floor") {
        return feature.subType=="verified_sloped_floor"?QStringLiteral("SLOPED_FLOOR_STRATEGY_REQUIRED"):QString();
    }
    if(!mesh)return QStringLiteral("SLOPED_FLOOR_SOURCE_REQUIRED");
    const auto binding=bindSlopedFloor(*mesh,selection);
    if(!binding.error.isEmpty())return binding.error;
    const auto expected=machiningContour(binding.feature);
    if(feature.subType!=expected.subType || feature.points!=expected.points || feature.islands!=expected.islands ||
       feature.axis!=expected.axis || feature.region!=expected.region || !feature.planarBoundaryVerified ||
       !feature.precisePoints.isEmpty() || feature.center.x()!=expected.center.x() ||
       feature.center.y()!=expected.center.y() || feature.center.z()!=expected.center.z())
        return QStringLiteral("SLOPED_FLOOR_SOURCE_CACHE_MISMATCH");
    return {};
}
