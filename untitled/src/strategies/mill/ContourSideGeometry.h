#pragma once
#include "../StrategyBase.h"
#include "../../workspaces/process_design/selection/SelectionChainController.h"
#include <QObject>
#include <algorithm>
#include <cmath>

inline double contourCross(const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    return (double(b.x())-a.x())*(double(c.y())-a.y())
        - (double(b.y())-a.y())*(double(c.x())-a.x());
}

// Automatic inside/outside is bounded to simple, convex, planar XY polygons.
inline int convexContourOrientation(const QVector<QVector3D> &points)
{
    if (points.size() < 3) return 0;
    double area = 0;
    for (int i=0; i<points.size(); ++i) {
        const auto &a=points[i]; const auto &b=points[(i+1)%points.size()];
        if (!std::isfinite(a.x()) || !std::isfinite(a.y()) || !std::isfinite(a.z()) ||
            std::abs(double(a.z())-points.first().z())>1e-5 ||
            std::hypot(double(b.x())-a.x(),double(b.y())-a.y())<1e-6) return 0;
        area += contourCross(points.first(),a,b);
        for (int j=0; j<i; ++j)
            if (std::hypot(double(a.x())-points[j].x(),double(a.y())-points[j].y())<1e-6) return 0;
    }
    if (!std::isfinite(area) || std::abs(area)<1e-6) return 0;
    const int orientation=area>0 ? 1 : -1;
    // Every vertex must be in every oriented edge half-plane. This also
    // rejects crossing polygons without assuming their input order is valid.
    for (int i=0; i<points.size(); ++i) {
        const auto &a=points[i]; const auto &b=points[(i+1)%points.size()];
        for (const auto &point:points)
            if (orientation*contourCross(a,b,point)<-1e-6) return 0;
        const auto &c=points[(i+2)%points.size()];
        if (std::abs(contourCross(a,b,c))<1e-6 &&
            QVector3D::dotProduct(b-a,c-b)<=0) return 0;
    }
    return orientation;
}

inline bool contourContainsWithClearance(const QVector<QVector3D> &polygon,
                                        const QVector3D &point, double clearance, int orientation = 0)
{
    if (!orientation) orientation=convexContourOrientation(polygon);
    if (!orientation) return false;
    for (int i=0; i<polygon.size(); ++i) {
        const auto &a=polygon[i]; const auto &b=polygon[(i+1)%polygon.size()];
        const double length=std::hypot(double(b.x())-a.x(),double(b.y())-a.y());
        if (orientation*contourCross(a,b,point)/length < clearance-1e-6) return false;
    }
    return true;
}

inline QString contourMachiningSideError(const ContourFeature &feature, bool closed,
                                        ChainMachiningSide side)
{
    if (!feature.islands.isEmpty())
        return QObject::tr("当前轮廓侧向加工尚未验证岛屿避让，请选择无岛区域。");
    if (side==ChainMachiningSide::Mixed) return {};
    if (side!=ChainMachiningSide::Inside && side!=ChainMachiningSide::Outside)
        return QObject::tr("加工侧无效，请重新选择。");
    if (!closed) return QObject::tr("开放链没有唯一内外侧，请返回选择混合侧并明确左/右刀补。");
    if (!convexContourOrientation(feature.points))
        return QObject::tr("自动内外侧当前需要明确、简单、凸的XY平面闭合轮廓；凹域或退化边界未支持。");
    return {};
}

inline double contourPointSegmentDistance(const QVector3D &point, const QVector3D &a, const QVector3D &b)
{
    const double dx=double(b.x())-a.x(), dy=double(b.y())-a.y();
    const double length2=dx*dx+dy*dy;
    const double t=length2>0 ? std::max(0.0,std::min(1.0,
        ((double(point.x())-a.x())*dx+(double(point.y())-a.y())*dy)/length2)) : 0;
    return std::hypot(double(point.x())-a.x()-t*dx,double(point.y())-a.y()-t*dy);
}

inline bool contourSegmentClearsBoundary(const QVector3D &a, const QVector3D &b,
                                         const QVector<QVector3D> &polygon, double clearance)
{
    for (int i=0; i<polygon.size(); ++i) {
        const auto &c=polygon[i]; const auto &d=polygon[(i+1)%polygon.size()];
        const double abC=contourCross(a,b,c), abD=contourCross(a,b,d);
        const double cdA=contourCross(c,d,a), cdB=contourCross(c,d,b);
        if (abC*abD<0 && cdA*cdB<0) return false;
        const double distance=std::min({contourPointSegmentDistance(a,c,d),contourPointSegmentDistance(b,c,d),
            contourPointSegmentDistance(c,a,b),contourPointSegmentDistance(d,a,b)});
        if (distance<clearance-1e-6) return false;
    }
    return true;
}

inline int contourCutterSide(const ContourFeature &feature, ChainMachiningSide side)
{
    return convexContourOrientation(feature.points)*(side==ChainMachiningSide::Inside ? 1 : -1);
}

inline QString contourSideOperationError(const SelectionChain &selection,
                                         const ContourFeature &feature,
                                         const StrategyParams &params,
                                         const QString &strategyId)
{
    if (strategyId!="mill_closed_contour" && strategyId!="mill_open_contour") return {};
    const auto side=selection.machiningSide;
    const QString error=contourMachiningSideError(feature,strategyId=="mill_closed_contour",side);
    if (!error.isEmpty()) return error;
    const double comp=params.get("compensation",1);
    const double stock=params.get("stockToLeave",0);
    if (!std::isfinite(stock) || stock<0 || (comp!=0 && stock!=0))
        return QObject::tr("余量必须非负；正余量请使用CAM几何偏置，机床刀补当前不自动加入余量。");
    if (side==ChainMachiningSide::Mixed) return {};
    const double expected=contourCutterSide(feature,side);
    if (comp==0 ? params.get("camOffsetSide",1)!=expected : comp!=expected)
        return QObject::tr("刀补方向与已确认内外侧或点序冲突，请重新确认加工侧。");
    return {};
}
