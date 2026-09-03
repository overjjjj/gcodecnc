#include "HoleFeatureGrouping.h"

namespace {

bool isHole(const MachiningFeature &feature)
{
    return feature.kind == FeatureKind::Hole || feature.kind == FeatureKind::Thread;
}

QString typeLabel(const MachiningFeature &feature, bool chinese)
{
    if (feature.subType == QStringLiteral("through_hole"))
        return chinese ? QStringLiteral("通孔") : QStringLiteral("Through hole");
    if (feature.subType == QStringLiteral("blind_hole"))
        return chinese ? QStringLiteral("盲孔") : QStringLiteral("Blind hole");
    if (feature.subType == QStringLiteral("countersunk_through_hole"))
        return chinese ? QStringLiteral("沉头通孔") : QStringLiteral("Countersunk through hole");
    if (feature.subType == QStringLiteral("countersunk_blind_hole"))
        return chinese ? QStringLiteral("沉头盲孔") : QStringLiteral("Countersunk blind hole");
    if (feature.subType == QStringLiteral("tapped_hole") || feature.kind == FeatureKind::Thread)
        return chinese ? QStringLiteral("螺纹孔") : QStringLiteral("Tapped hole");
    return chinese ? QStringLiteral("孔") : QStringLiteral("Hole");
}

} // namespace

QString holeFeatureGroupId(const MachiningFeature &feature)
{
    if (!isHole(feature)) {
        return QString();
    }
    return QStringLiteral("%1|d%2|z%3|major%4|p%5")
        .arg(feature.subType)
        .arg(feature.radius * 2.0, 0, 'f', 4)
        .arg(feature.depth, 0, 'f', 4)
        .arg(feature.secondaryRadius * 2.0, 0, 'f', 4)
        .arg(feature.pitch, 0, 'f', 4);
}

QString holeFeatureGroupLabel(const MachiningFeature &feature, bool chinese)
{
    if (!isHole(feature)) {
        return QString();
    }
    QString label = QStringLiteral("%1  D%2  %3%4")
        .arg(typeLabel(feature, chinese))
        .arg(feature.radius * 2.0, 0, 'f', 2)
        .arg(chinese ? QStringLiteral("深") : QStringLiteral("Depth "))
        .arg(feature.depth, 0, 'f', 2);
    if (feature.secondaryRadius > 0.0
        && feature.secondaryRadius > feature.radius + 1.0e-6) {
        label += QStringLiteral("  %1%2")
            .arg(chinese ? QStringLiteral("大径") : QStringLiteral("Major D"))
            .arg(feature.secondaryRadius * 2.0, 0, 'f', 2);
    }
    if (feature.pitch > 0.0) {
        label += QStringLiteral("  %1%2")
            .arg(chinese ? QStringLiteral("螺距") : QStringLiteral("Pitch "))
            .arg(feature.pitch, 0, 'f', 2);
    }
    return label;
}

bool holeFeaturesShareGroup(const QVector<MachiningFeature> &features)
{
    QString groupId;
    for (const MachiningFeature &feature : features) {
        const QString currentId = holeFeatureGroupId(feature);
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
