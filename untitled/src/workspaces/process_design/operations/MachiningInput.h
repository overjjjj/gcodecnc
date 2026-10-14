#pragma once
#include "../../../import/StepImporter.h"
#include "../../../strategies/StrategyBase.h"
#include "../selection/SelectionChainController.h"
#include <QSet>
#include <algorithm>
#include <cmath>
#include "DesignFeatureCatalog.h"

struct MachiningRequirements {
    bool closedBoundary = false;
    bool edgePickingOnly = false;
};
inline MachiningRequirements machiningRequirements(const QString &id)
{
    const auto *entry = designFeatureForStrategy(id);
    return entry ? MachiningRequirements{entry->closedBoundary, entry->edgePickingOnly}
                 : MachiningRequirements{};
}
inline double machiningTopZ(const MachiningFeature &feature)
{
    if (feature.kind == FeatureKind::Slot && feature.depth > 0) {
        const bool side = feature.region == FaceRegion::Side || std::abs(feature.axis.z()) < .5f;
        return feature.center.z() + (side && feature.width > 0 ? feature.width : feature.depth)*.5;
    }
    return feature.center.z();
}
inline double contourMachiningDepth(const MachiningFeature &feature)
{
    if (feature.kind == FeatureKind::Slot &&
        (feature.region == FaceRegion::Side || std::abs(feature.axis.z()) < .5f) && feature.width > 0)
        return std::min(feature.depth > 0 ? feature.depth : feature.width, feature.width);
    return feature.depth;
}
inline ContourFeature machiningContour(const MachiningFeature &feature)
{
    ContourFeature contour;
    contour.subType=feature.subType;contour.center=feature.center;
    contour.center.setZ(float(machiningTopZ(feature)));
    contour.radius=feature.radius;contour.depth=contourMachiningDepth(feature);
    contour.width=feature.width;contour.length=feature.length;contour.angle=feature.angle;
    contour.axis=feature.axis;contour.region=feature.region;
    contour.points=feature.boundaryPoints;contour.islands=feature.islandBoundaries;
    contour.planarBoundaryVerified=feature.planarBoundaryVerified;
    if(feature.subType=="open_slot")contour.openSide=1;
    return contour;
}
inline ContourFeature applyMachiningParameters(ContourFeature feature,const StrategyParams &params)
{
    if(params.values.contains("depth"))feature.depth=params.get("depth",feature.depth);
    if(params.values.contains("slotLength"))feature.length=params.get("slotLength",feature.length);
    if(params.values.contains("slotWidth"))feature.width=params.get("slotWidth",feature.width);
    if(params.values.contains("angle"))feature.angle=params.get("angle",feature.angle);
    return feature;
}

// Recognition may propose a region, but face membership alone cannot bind it.
inline bool selectionMatchesContour(const SelectionChain &selection,const ContourFeature &feature,const MeshData &mesh)
{
    auto samplesFor=[&](const QStringList &ids,const QVector<bool> &forward){
        QVector<QVector3D> points;
        for(int i=0;i<ids.size();++i){
            const MeshPickEdge *edge=nullptr;
            for(const auto &candidate:mesh.pickEdges)if(candidate.id==ids[i]){edge=&candidate;break;}
            if(!edge)return QVector<QVector3D>();
            auto samples=edge->points;
            if(!forward.value(i,true))std::reverse(samples.begin(),samples.end());
            if(!points.isEmpty() && !samples.isEmpty() && (points.last()-samples.first()).length()>.002)return QVector<QVector3D>();
            for(const auto &point:samples)if(points.isEmpty()||(points.last()-point).length()>.00001f)points.append(point);
        }
        return points;
    };
    auto matches=[](const QVector<QVector3D> &a,const QVector<QVector3D> &b,bool closed){
        if(a.size()<2 || b.size()<2)return false;
        auto follows=[closed](const QVector<QVector3D>&samples,const QVector<QVector3D>&path){
            const int segments=closed?path.size():path.size()-1;
            for(const auto &point:samples){
                double closest=1e30;
                for(int i=0;i<segments;++i){
                    const auto start=path[i],delta=path[(i+1)%path.size()]-start;
                    const double t=delta.lengthSquared()>0?qBound(0.0,double(QVector3D::dotProduct(point-start,delta))/delta.lengthSquared(),1.0):0;
                    closest=qMin(closest,double((point-start-delta*float(t)).length()));
                }
                if(closest>.002)return false;
            }
            return true;
        };
        return follows(a,b)&&follows(b,a);
    };
    QStringList selectedIds;QVector<bool> selectedDirections;
    for(const auto &ref:selection.orderedTopologyRefs)if(ref.kind==SelectionTopologyKind::Edge){selectedIds.append(ref.id);selectedDirections.append(ref.forward);}
    if(feature.subType=="open_contour"){
        if(selection.closed || selectedIds.size()!=selection.orderedTopologyRefs.size() || !feature.islands.isEmpty())return false;
        return matches(samplesFor(selectedIds,selectedDirections),feature.points,false);
    }
    const QSet<QString> selected(selectedIds.cbegin(),selectedIds.cend());
    const bool faceSelection=selection.orderedTopologyRefs.size()==1 && selection.orderedTopologyRefs.first().kind==SelectionTopologyKind::Face;
    for(const auto &loop:selection.boundaryLoops){
        if(!loop.outer)continue;
        const QSet<QString> ids(loop.orderedEdgeIds.cbegin(),loop.orderedEdgeIds.cend());
        if(faceSelection ? selection.orderedTopologyRefs.first().id!=loop.faceId : selected!=ids)continue;
        QVector<QVector3D> expected=feature.points;
        if(expected.isEmpty() && feature.length>0 && feature.width>0){
            const double angle=feature.angle*3.14159265358979323846/180;
            const QVector3D u(std::cos(angle),std::sin(angle),0),v(-std::sin(angle),std::cos(angle),0);
            for(const auto &sign:QVector<QVector3D>{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}})
                expected.append(feature.center.toVector3D()+u*float(sign.x()*feature.length*.5)+v*float(sign.y()*feature.width*.5));
        }
        if(expected.size()<3 || !matches(samplesFor(loop.orderedEdgeIds,loop.orderedForward),expected,true))continue;
        QSet<int> matchedIslands;bool islandsMatch=true;int sourceIslands=0;
        for(const auto &inner:mesh.boundaryLoops){
            if(inner.faceId!=loop.faceId || inner.outer)continue;
            ++sourceIslands;int match=-1;
            const auto actual=samplesFor(inner.orderedEdgeIds,inner.orderedForward);
            for(int i=0;i<feature.islands.size();++i)if(!matchedIslands.contains(i)&&matches(actual,feature.islands[i],true)){match=i;break;}
            if(match<0){islandsMatch=false;break;}
            matchedIslands.insert(match);
        }
        if(islandsMatch && sourceIslands==feature.islands.size())return true;
    }
    return false;
}
