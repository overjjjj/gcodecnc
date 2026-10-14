#pragma once
#include "ProjectManager.h"
#include "../strategies/hole/HoleStrategyUtils.h"
#include <cmath>
#include "GCodeCoordinate.h"

inline QString workHeightError(double top, const StrategyParams &params, int outputDecimals = 3)
{
    const double safe=params.get("safeHeight",50), clearance=params.get("feedHeight",3);
    const auto output=[outputDecimals](double z){return QString::number(z,'f',outputDecimals).toDouble();};
    if (!std::isfinite(top) || !std::isfinite(safe) || !std::isfinite(clearance)
        || !std::isfinite(top+clearance) || safe<=0 || clearance<=0
        || output(top+clearance)<=output(top) || output(safe)<output(top+clearance))
        return QObject::tr("工作坐标安全Z必须不低于加工顶面加进给净空，请核对原点与高度。");
    return {};
}

inline QString workHeightError(const ContourFeature &feature, const StrategyParams &params)
{
    return workHeightError(feature.center.z(),params);
}

inline QString workHeightError(const HoleFeature &feature, const StrategyParams &params, const QString &strategyId)
{
    return workHeightError(holeZRange(feature,holeCuttingDepth(strategyId,feature,params),params.get("feedHeight",3)).entryZ,params,kGCodeCoordinateDecimals);
}

// Source geometry/evidence remains in model coordinates. Only strategy inputs
// and resolved stock bounds are translated; dimensions and directions are not.
inline StockBounds machiningModelBounds(const ProjectManager *project)
{
    if (!project) return {};
    if (project->sourceType() != ProjectSourceType::Sketch)
        return {Coordinate3D(project->mesh().bbMin),Coordinate3D(project->mesh().bbMax)};
    const auto &document=project->sketchDocument();
    const auto points=document.boundingPoints();
    if (points.isEmpty()) return {};
    StockBounds bounds{{points.first().x(),points.first().y(),document.planeZ},
                       {points.first().x(),points.first().y(),document.planeZ}};
    for (const auto &point : points) {
        bounds.minimum.setX(std::min(bounds.minimum.x(),point.x()));
        bounds.minimum.setY(std::min(bounds.minimum.y(),point.y()));
        bounds.maximum.setX(std::max(bounds.maximum.x(),point.x()));
        bounds.maximum.setY(std::max(bounds.maximum.y(),point.y()));
    }
    return bounds;
}

inline Coordinate3D machiningOrigin(const ProjectManager *project)
{
    const auto bounds=machiningModelBounds(project);
    return project && project->setupOrigin().confirmed
        ? project->setupOrigin().resolvedPoint(bounds.minimum,bounds.maximum)
        : Coordinate3D();
}

inline StockBounds machiningStockBounds(const ProjectManager *project)
{
    if (!project) return {};
    const auto bounds=machiningModelBounds(project);
    return project->stockDefinition().resolvedBounds(bounds.minimum,bounds.maximum);
}

inline ContourFeature inWorkCoordinates(ContourFeature feature, const Coordinate3D &origin)
{
    feature.center -= origin;
    for (auto &point : feature.points) point = (Coordinate3D(point) - origin).toVector3D();
    for (auto &point : feature.precisePoints) point -= origin;
    for (auto &loop : feature.islands) for (auto &point : loop) point = (Coordinate3D(point) - origin).toVector3D();
    return feature;
}

inline HoleFeature inWorkCoordinates(HoleFeature feature, const Coordinate3D &origin)
{
    feature.center -= origin;
    for (auto &point : feature.boundaryPoints) point = (Coordinate3D(point) - origin).toVector3D();
    for (auto &loop : feature.islandBoundaries) for (auto &point : loop) point = (Coordinate3D(point) - origin).toVector3D();
    return feature;
}

inline StockBounds inWorkCoordinates(StockBounds stock, const Coordinate3D &origin)
{
    stock.minimum -= origin;
    stock.maximum -= origin;
    return stock;
}
