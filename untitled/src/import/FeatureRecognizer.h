#pragma once
#include "StepImporter.h"
#include <QSet>
#include <QVector3D>

#ifdef CNEXT_ENABLE_OCC
class FeatureRecognizer
{
public:
    // frontNormal 默认 (0,0,1)，即以 +Z 为正面。
    // 调用 setFrontNormal() 后再调用 recognize() 可按新正面方向重新分区。
    void setFrontNormal(const QVector3D &n) { m_frontNormal = n.normalized(); }
    QVector3D frontNormal() const { return m_frontNormal; }

    QVector<MachiningFeature> recognize(const TopoGraph &graph) const;

    // 仅重新计算已识别特征的 region（不重跑几何识别），适合切换正面后快速更新。
    void reclassifyRegions(TopoGraph &graph,
                           QVector<MachiningFeature> &features) const;

private:
    QVector3D m_frontNormal = {0, 0, 1};

    QVector<MachiningFeature> findHoles(const TopoGraph &graph) const;
    QVector<MachiningFeature> findFlatSurfaces(const TopoGraph &graph,
                                               const QSet<int> &consumedFaces) const;
    QVector<MachiningFeature> findBosses(const TopoGraph &graph,
                                         const QSet<int> &consumedFaces) const;
    QVector<MachiningFeature> findSlots(const TopoGraph &graph) const;
    QVector<MachiningFeature> findPockets(const TopoGraph &graph,
                                          const QSet<int> &consumedBottomFaces) const;
    QVector<MachiningFeature> findChamfers(const TopoGraph &graph) const;
    QVector<MachiningFeature> findFillets(const TopoGraph &graph) const;
};
#endif
