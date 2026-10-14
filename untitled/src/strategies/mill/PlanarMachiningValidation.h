#pragma once
#include "../StrategyBase.h"
#include "../../core/StockDefinition.h"
#include <QObject>
#include <algorithm>
#include <cmath>
#include <limits>

inline bool isPlanarMillingStrategy(const QString &id)
{
    return id == QStringLiteral("mill_face") || id == QStringLiteral("mill_surface_finish");
}

inline double planarOutputCoordinate(double value)
{
    return QString::number(value, 'f', 3).toDouble();
}

inline double planarEntryCoordinate(double boundary, double radius)
{
    // Leave one output quantum beyond tangency before descending outside stock.
    return planarOutputCoordinate(boundary - radius - 0.001);
}

inline QString planarMachiningError(const ContourFeature &f, const ToolEntry &tool,
                                   const StrategyParams &p)
{
    if (!f.planarBoundaryVerified || !f.islands.isEmpty())
        return QObject::tr("请重新选择具有完整原始直线外边界、无内环的平面。");
    const double eps = 8.0 * std::numeric_limits<float>::epsilon();
    if (!std::isfinite(f.axis.x()) || !std::isfinite(f.axis.y()) || !std::isfinite(f.axis.z())
        || f.axis.z() <= 0 || std::hypot(f.axis.x(), f.axis.y()) > f.axis.z()*eps)
        return QObject::tr("平面必须与当前加工正Z方向一致。");
    if (!std::isfinite(f.length) || !std::isfinite(f.width) || f.length<=0 || f.width<=0
        || !std::isfinite(f.center.x()) || !std::isfinite(f.center.y()) || !std::isfinite(f.center.z())
        || !std::isfinite(f.angle) || std::abs(f.angle)>eps)
        return QObject::tr("仅支持明确的XY轴对齐矩形平面。");
    // Float uncertainty is bounded below one tenth of the 0.001 mm output quantum.
    // Large translated geometry that cannot meet that bound must be re-originated.
    double scale=1;
    for (const auto &v:f.points) {
        if (!std::isfinite(v.x()) || !std::isfinite(v.y()) || !std::isfinite(v.z()))
            return QObject::tr("平面边界包含非有限坐标。");
        scale=std::max(scale,std::max({std::abs(double(v.x())),std::abs(double(v.y())),std::abs(double(v.z()))}));
    }
    const double tol=std::max(1e-7,scale*std::numeric_limits<float>::epsilon());
    if (tol>0.0001 || std::min(f.length,f.width)<=tol*16)
        return QObject::tr("平面坐标精度不足，请调整原点或加工范围。");
    auto points=f.points;
    if (points.size()>1 && (points.first()-points.last()).length()<=tol) points.removeLast();
    if (points.size()!=4) return QObject::tr("原始平面边界必须是四角矩形。");
    int corners=0;
    for (int i=0;i<4;++i) {
        const auto &a=points[i];const auto &b=points[(i+1)%4];
        const double dx=std::abs(double(a.x())-b.x()),dy=std::abs(double(a.y())-b.y());
        if (std::abs(double(a.z())-f.center.z())>tol || !((dx<=tol && dy>tol)||(dy<=tol && dx>tol)))
            return QObject::tr("原始矩形边必须沿当前X/Y方向。");
        const double lx=f.center.x()-f.length/2,rx=f.center.x()+f.length/2;
        const double by=f.center.y()-f.width/2,ty=f.center.y()+f.width/2;
        const int x=std::abs(a.x()-lx)<=tol?0:std::abs(a.x()-rx)<=tol?1:-1;
        const int y=std::abs(a.y()-by)<=tol?0:std::abs(a.y()-ty)<=tol?1:-1;
        if (x<0 || y<0 || (corners&(1<<(x+2*y)))) return QObject::tr("平面尺寸与原边界不一致。");
        corners|=1<<(x+2*y);
    }
    if (p.values.contains("stepDown")) return QObject::tr("当前为单层平面扫掠，不支持stepDown，请明确移除该参数。");
    const double depth=p.get("depth",0),stock=p.get("stockToLeave",0);
    if (!std::isfinite(depth) || depth<=0 || !std::isfinite(f.depth) || depth!=f.depth
        || !std::isfinite(stock) || stock<0 || stock>=depth)
        return QObject::tr("请明确输入一致的去除深度，底余量须非负且小于深度。");
    if (tool.id<=0 || tool.type!="end_mill" || !std::isfinite(tool.diameter) || tool.diameter<=0
        || !std::isfinite(tool.fluteLen) || !std::isfinite(tool.totalLen))
        return QObject::tr("平面加工需要有效的立铣刀参数。");
    const double cut=depth-stock;
    if ((tool.fluteLen>0 && cut>tool.fluteLen+0.01) || (tool.totalLen>0 && cut>=tool.totalLen-0.01))
        return QObject::tr("实际去除深度超过刀具可达长度。");
    for (const auto &key:{"spindleSpeed","feedRate","plungeRate"}) {
        const double v=p.get(key,0);
        if (!std::isfinite(v) || v<1 || v>std::numeric_limits<int>::max())
            return QObject::tr("转速和进给必须能输出为正整数。");
    }
    const double safe=p.get("safeHeight",0),feed=p.get("feedHeight",0),step=p.get("stepover",0);
    const double top=f.center.z(),bottom=top-cut;
    if (!std::isfinite(safe) || !std::isfinite(feed) || feed<=0 || safe<=0 || safe<top+feed
        || !std::isfinite(top+feed) || !std::isfinite(bottom)
        || planarOutputCoordinate(safe)<=0 || planarOutputCoordinate(top+feed)<=planarOutputCoordinate(top)
        || planarOutputCoordinate(bottom)>=planarOutputCoordinate(top))
        return QObject::tr("安全高度、进给净空或单层切深无效。");
    if (!std::isfinite(step) || step<=0 || step>tool.diameter || planarOutputCoordinate(step)<=0)
        return QObject::tr("步距必须为正且不超过刀具有效直径。");
    const double count=std::ceil((f.width+tool.diameter)/step)+1;
    if (!std::isfinite(count) || count>=std::numeric_limits<int>::max())
        return QObject::tr("平面扫描行数超出可表示范围。");
    return {};
}

inline QString planarStockEntryError(const ContourFeature &f,const ToolEntry &tool,
                                     const StrategyParams &p,const StockBounds &stock)
{
    for(int i=0;i<3;++i)
        if(!std::isfinite(stock.minimum[i]) || !std::isfinite(stock.maximum[i])
            || stock.minimum[i]>=stock.maximum[i]) return QObject::tr("已确认毛坯范围无效。");
    const double radius=tool.diameter/2;
    const double x=planarEntryCoordinate(f.center.x()-f.length/2,radius);
    const double y=planarEntryCoordinate(f.center.y()-f.width/2,radius);
    const double dx=std::max({double(stock.minimum.x())-x,0.0,x-double(stock.maximum.x())});
    const double dy=std::max({double(stock.minimum.y())-y,0.0,y-double(stock.maximum.y())});
    if(std::hypot(dx,dy)<=radius || planarOutputCoordinate(p.get("safeHeight",0))<=stock.maximum.z())
        return QObject::tr("当前平面下刀圆盘未完全位于毛坯外，或安全高度未高于毛坯，请复核范围与净空。");
    return {};
}
