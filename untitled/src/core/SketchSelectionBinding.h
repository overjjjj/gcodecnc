#pragma once

#include "src/core/SketchDocument.h"
#include "src/core/StrategyParams.h"
#include "src/workspaces/process_design/selection/SelectionChainController.h"
#include "src/workspaces/program_verification/simulation/SketchClearance.h"

struct SketchSelectionBinding
{
    QVector<Coordinate3D> points;
    QString error;
    QVector<SelectionAnchor> anchors;
};

inline SketchSelectionBinding bindSketchSelection(const SketchDocument &document,
                                                  const SelectionChain &selection,
                                                  const StrategyParams &params,
                                                  const QString &setupFingerprint,
                                                  const QString &workOffset)
{
    auto reject=[](const QString &reason){return SketchSelectionBinding{{},reason};};
    if (document.contourKind!="rectangle" && document.contourKind!="polyline")
        return reject(QStringLiteral("当前草图加工只支持直线闭合轮廓，直线、圆和圆弧尚无加工证明。"));
    const int orientation=SketchClearance::convexOrientation(document.points);
    if (!document.additionalContours.isEmpty() || !document.validationError().isEmpty() || !orientation || document.isEmpty())
        return reject(QStringLiteral("草图加工需要有效的单一凸闭合轮廓。"));
    if (selection.sourceFingerprint!=document.fingerprint() || setupFingerprint.isEmpty() ||
        selection.setupFingerprint!=setupFingerprint || selection.coordinateSystemId!=workOffset)
        return reject(QStringLiteral("草图选区与当前来源或设置不一致，请重新选择。"));
    const auto positiveZ=[](const QVector3D &v){return v.x()==0 && v.y()==0 && v.z()==1;};
    if (!positiveZ(selection.toolAxis) || !positiveZ(selection.selectedSurfaceNormal) ||
        selection.geometrySource!=ChainGeometrySource::Wire || !selection.explicitUserSelection ||
        !selection.closed || selection.freeEndCount!=0 || selection.islandCount!=0 ||
        selection.machiningSide!=ChainMachiningSide::Outside || params.get("compensation",1)!=0)
        return reject(QStringLiteral("草图加工仅支持固定 +Z、完整线框、外侧与 G40 CAM 偏置。"));
    const int count=document.points.size();
    const QString prefix="sketch:"+document.id+":"+document.contourId;
    const auto edgeId=[&](int i){return prefix+":edge:"+QString::number(i);};
    const auto vertexId=[&](int i){return prefix+":vertex:"+QString::number(i);};
    if (selection.orderedTopologyRefs.size()!=count || selection.orderedGeometryIds.size()!=count)
        return reject(QStringLiteral("必须选择当前轮廓的全部边。"));
    int firstEdge=-1;
    for(int i=0;i<count;++i) if(selection.orderedTopologyRefs.first().id==edgeId(i)) firstEdge=i;
    if(firstEdge<0) return reject(QStringLiteral("草图边身份无效。"));
    const int step=selection.orderedTopologyRefs.first().forward?1:-1;
    QVector<int> vertices;
    for(int i=0;i<count;++i) {
        const int edge=(firstEdge+step*i+count)%count;
        const auto &ref=selection.orderedTopologyRefs[i];
        if(ref.kind!=SelectionTopologyKind::Edge || ref.id!=edgeId(edge) || ref.forward!=(step>0) ||
            selection.orderedGeometryIds[i]!=ref.id)
            return reject(QStringLiteral("草图边必须具有完整、唯一且连续的顺序与方向。"));
        vertices.append(step>0?edge:(edge+1)%count);
    }
    if(params.get("camOffsetSide",1)!=-orientation*step)
        return reject(QStringLiteral("CAM 偏置方向与外侧及点序不一致，请重新确认。"));
    if(!selection.hasStartAnchor) return reject(QStringLiteral("请选择当前轮廓的精确起点。"));
    const auto &anchor=selection.startAnchor;
    int startVertex=-1, startEdge=-1;
    for(int i=0;i<count;++i) {
        if(anchor.topologyId==vertexId(i)) startVertex=i;
        if(anchor.topologyId==edgeId(i)) startEdge=i;
    }
    QPointF start;
    int startIndex=-1;
    if(startVertex>=0) {
        start=document.points[startVertex]; startIndex=vertices.indexOf(startVertex);
    } else if(startEdge>=0 && anchor.hasCurveParameter && std::isfinite(anchor.curveParameter) &&
              anchor.curveParameter>=0 && anchor.curveParameter<=1) {
        const auto &a=document.points[startEdge], &b=document.points[(startEdge+1)%count];
        const double t=anchor.curveParameter;
        start=t==0?a:t==1?b:QPointF(a.x()+(b.x()-a.x())*t,a.y()+(b.y()-a.y())*t);
        startIndex=vertices.indexOf(step>0?(startEdge+1)%count:startEdge);
    } else return reject(QStringLiteral("起点必须绑定当前顶点，或当前边及有效参数。"));
    SketchSelectionBinding result;
    result.points.append({start.x(),start.y(),document.planeZ});
    result.anchors.append(anchor);
    for(int i=0;i<count;++i) {
        const auto &point=document.points[vertices[(startIndex+i)%count]];
        if(SketchClearance::exactlyEqual(point,start)) continue;
        result.points.append({point.x(),point.y(),document.planeZ});
        SelectionAnchor vertexAnchor;
        vertexAnchor.topologyId=vertexId(vertices[(startIndex+i)%count]);
        vertexAnchor.point=QVector3D(point.x(),point.y(),document.planeZ);
        result.anchors.append(vertexAnchor);
    }
    return result;
}
