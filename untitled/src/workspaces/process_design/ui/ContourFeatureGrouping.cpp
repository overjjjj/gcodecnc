#include "ContourFeatureGrouping.h"

#include <QStringList>

namespace {

QString loopKey(const QVector<QVector3D> &loop)
{
    QStringList values;
    values.reserve(loop.size());
    for (const QVector3D &point : loop) {
        values.append(QStringLiteral("%1,%2,%3")
                          .arg(point.x(), 0, 'f', 4)
                          .arg(point.y(), 0, 'f', 4)
                          .arg(point.z(), 0, 'f', 4));
    }
    return values.join(QLatin1Char(';'));
}

QString topologyKey(const MachiningFeature &feature)
{
    QStringList values{loopKey(feature.boundaryPoints)};
    for (const QVector<QVector3D> &island : feature.islandBoundaries) {
        values.append(loopKey(island));
    }
    return values.join(QLatin1Char('|'));
}

bool isContourFeature(const MachiningFeature &feature)
{
    return feature.kind != FeatureKind::Hole && feature.kind != FeatureKind::Thread;
}

QString typeLabel(const MachiningFeature &feature, bool chinese)
{
    if (feature.subType == QStringLiteral("straight_slot"))
        return chinese ? QStringLiteral("直线槽") : QStringLiteral("Straight slot");
    if (feature.subType == QStringLiteral("arc_slot"))
        return chinese ? QStringLiteral("圆弧槽") : QStringLiteral("Arc slot");
    if (feature.subType == QStringLiteral("open_slot"))
        return chinese ? QStringLiteral("开口槽") : QStringLiteral("Open slot");
    if (feature.subType == QStringLiteral("blind_slot"))
        return chinese ? QStringLiteral("盲槽") : QStringLiteral("Blind slot");
    if (feature.subType == QStringLiteral("rectangular_pocket"))
        return chinese ? QStringLiteral("矩形腔") : QStringLiteral("Rectangular pocket");
    if (feature.subType == QStringLiteral("circular_pocket"))
        return chinese ? QStringLiteral("圆形腔") : QStringLiteral("Circular pocket");
    if (feature.subType == QStringLiteral("irregular_pocket"))
        return chinese ? QStringLiteral("异形腔") : QStringLiteral("Irregular pocket");
    if (!feature.subType.isEmpty())
        return feature.subType;
    return chinese ? QStringLiteral("轮廓特征") : QStringLiteral("Contour feature");
}

} // namespace

QString contourFeatureGroupId(const MachiningFeature &feature)
{
    if (!isContourFeature(feature)) {
        return QString();
    }
    return QStringLiteral("k%1|%2|l%3|w%4|d%5|r%6|a%7|region%8|topology%9")
        .arg(int(feature.kind))
        .arg(feature.subType)
        .arg(feature.length, 0, 'f', 4)
        .arg(feature.width, 0, 'f', 4)
        .arg(feature.depth, 0, 'f', 4)
        .arg(feature.radius, 0, 'f', 4)
        .arg(feature.angle, 0, 'f', 4)
        .arg(int(feature.region))
        .arg(topologyKey(feature));
}

QString contourFeatureGroupLabel(const MachiningFeature &feature, bool chinese)
{
    if (!isContourFeature(feature)) {
        return QString();
    }

    QString label = typeLabel(feature, chinese);
    if (feature.length > 0.0)
        label += QStringLiteral("  L%1").arg(feature.length, 0, 'f', 2);
    if (feature.width > 0.0)
        label += QStringLiteral("  W%1").arg(feature.width, 0, 'f', 2);
    if (feature.radius > 0.0)
        label += QStringLiteral("  D%1").arg(feature.radius * 2.0, 0, 'f', 2);
    if (feature.depth > 0.0)
        label += QStringLiteral("  %1%2")
            .arg(chinese ? QStringLiteral("深") : QStringLiteral("Depth "))
            .arg(feature.depth, 0, 'f', 2);
    if (feature.kind == FeatureKind::Slot)
        label += QStringLiteral("  A%1°").arg(feature.angle, 0, 'f', 2);
    return label;
}

bool contourFeaturesShareGroup(const QVector<MachiningFeature> &features)
{
    QString groupId;
    for (const MachiningFeature &feature : features) {
        const QString currentId = contourFeatureGroupId(feature);
        if (currentId.isEmpty()) {
            return false;
        }
        if (groupId.isEmpty()) {
            groupId = currentId;
        } else if (currentId != groupId) {
            return false;
        }
    }
    return !groupId.isEmpty();
}
