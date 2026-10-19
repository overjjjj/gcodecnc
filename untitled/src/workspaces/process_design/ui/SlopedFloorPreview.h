#pragma once
#include "../selection/SlopedFloorBinding.h"
#include <QLabel>
#include <QPainter>
#include <QPainterPath>

inline void showSlopedFloorPreview(QLabel *label,const SlopedFloorBinding &binding)
{
    if(!binding.error.isEmpty() || binding.sourceOuterBoundary.size()<3){label->hide();return;}
    QPixmap image(440,190);image.fill(Qt::white);QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    double x0=1e100,y0=1e100,x1=-1e100,y1=-1e100;
    for(const auto &p:binding.sourceOuterBoundary){x0=std::min(x0,double(p.x()));x1=std::max(x1,double(p.x()));y0=std::min(y0,double(p.y()));y1=std::max(y1,double(p.y()));}
    const double scale=std::min(400/(x1-x0),110/(y1-y0));
    auto path=[&](const QVector<QVector3D> &points){
        QPainterPath result;
        for(int i=0;i<points.size();++i){const QPointF p(20+(points[i].x()-x0)*scale,140-(points[i].y()-y0)*scale);if(i)result.lineTo(p);else result.moveTo(p);}
        result.closeSubpath();return result;
    };
    const auto source=path(binding.sourceOuterBoundary),reachable=path(binding.feature.boundaryPoints);
    painter.setPen(Qt::black);painter.drawText(12,18,QStringLiteral("俯视：红色条带保留，蓝色区域内生成刀路"));
    painter.fillPath(source,QColor("#d73c35"));painter.fillPath(reachable,QColor("#d9eaf7"));
    painter.setPen(QPen(QColor("#136ca3"),1.5));painter.drawPath(reachable);
    for(const auto &hole:binding.feature.islandBoundaries){const auto region=path(hole);painter.fillPath(region,Qt::white);painter.setPen(QPen(Qt::black,1));painter.drawPath(region);}
    painter.setPen(Qt::black);painter.drawText(12,163,QStringLiteral("孔边、侧留量与底面 Z 向余量另行保留"));
    painter.drawText(12,183,QStringLiteral("遮挡侧壁：%1").arg(binding.overhangingWallIds.join(", ")));
    label->setPixmap(image);label->setObjectName("slopedFloorAccessPreview");
    label->setToolTip(QStringLiteral("真实底面与相邻侧壁的 +Z 投影；红色为当前装夹不可达条带。蓝色仅表示候选区域，完整刀具扫掠仍须通过。"));
    label->show();
}
