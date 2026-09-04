#include "SetupOrigin.h"

#include <QCryptographicHash>

namespace {

// 按位置选择包围盒轴向最小值、最大值或中点。
float axisValue(float minimum, float maximum, int position)
{
    if (position < 0) return minimum;
    if (position > 0) return maximum;
    return (minimum + maximum) * 0.5f;
}

} // namespace

// 根据锚点和偏移量解析 Setup 原点坐标。
QVector3D SetupOrigin::resolvedPoint(const QVector3D &bbMin, const QVector3D &bbMax) const
{
    if (anchor == SetupOriginAnchor::CustomPoint) {
        return customPoint + offset;
    }

    int xPosition = 0;
    int yPosition = 0;
    switch (anchor) {
    case SetupOriginAnchor::TopMinXMinY:      xPosition = -1; yPosition = -1; break;
    case SetupOriginAnchor::TopCenterXMinY:   xPosition =  0; yPosition = -1; break;
    case SetupOriginAnchor::TopMaxXMinY:      xPosition =  1; yPosition = -1; break;
    case SetupOriginAnchor::TopMinXCenterY:   xPosition = -1; yPosition =  0; break;
    case SetupOriginAnchor::TopCenter:        xPosition =  0; yPosition =  0; break;
    case SetupOriginAnchor::TopMaxXCenterY:   xPosition =  1; yPosition =  0; break;
    case SetupOriginAnchor::TopMinXMaxY:      xPosition = -1; yPosition =  1; break;
    case SetupOriginAnchor::TopCenterXMaxY:   xPosition =  0; yPosition =  1; break;
    case SetupOriginAnchor::TopMaxXMaxY:      xPosition =  1; yPosition =  1; break;
    case SetupOriginAnchor::CustomPoint:      break;
    }
    return QVector3D(axisValue(bbMin.x(), bbMax.x(), xPosition),
                     axisValue(bbMin.y(), bbMax.y(), yPosition),
                     bbMax.z()) + offset;
}

// 计算已确认 Setup 原点及工件坐标系的稳定指纹。
QString SetupOrigin::fingerprint(const QString &workOffset) const
{
    if (!confirmed) {
        return QString();
    }
    const QString payload = QStringLiteral("v1|%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(setupOriginAnchorId(anchor), workOffset.trimmed().toUpper())
        .arg(double(customPoint.x()), 0, 'f', 6)
        .arg(double(customPoint.y()), 0, 'f', 6)
        .arg(double(customPoint.z()), 0, 'f', 6)
        .arg(double(offset.x()), 0, 'f', 6)
        .arg(double(offset.y()), 0, 'f', 6)
        .arg(double(offset.z()), 0, 'f', 6);
    return QString::fromLatin1(QCryptographicHash::hash(payload.toUtf8(),
                                                        QCryptographicHash::Sha256).toHex());
}

// 将原点锚点枚举转换为持久化标识。
QString setupOriginAnchorId(SetupOriginAnchor anchor)
{
    switch (anchor) {
    case SetupOriginAnchor::TopMinXMinY:    return QStringLiteral("top_min_x_min_y");
    case SetupOriginAnchor::TopCenterXMinY: return QStringLiteral("top_center_x_min_y");
    case SetupOriginAnchor::TopMaxXMinY:    return QStringLiteral("top_max_x_min_y");
    case SetupOriginAnchor::TopMinXCenterY: return QStringLiteral("top_min_x_center_y");
    case SetupOriginAnchor::TopCenter:      return QStringLiteral("top_center");
    case SetupOriginAnchor::TopMaxXCenterY: return QStringLiteral("top_max_x_center_y");
    case SetupOriginAnchor::TopMinXMaxY:    return QStringLiteral("top_min_x_max_y");
    case SetupOriginAnchor::TopCenterXMaxY: return QStringLiteral("top_center_x_max_y");
    case SetupOriginAnchor::TopMaxXMaxY:    return QStringLiteral("top_max_x_max_y");
    case SetupOriginAnchor::CustomPoint:    return QStringLiteral("custom");
    }
    return QStringLiteral("top_center");
}

// 从持久化标识恢复原点锚点枚举。
SetupOriginAnchor setupOriginAnchorFromId(const QString &id)
{
    if (id == QStringLiteral("top_min_x_min_y")) return SetupOriginAnchor::TopMinXMinY;
    if (id == QStringLiteral("top_center_x_min_y")) return SetupOriginAnchor::TopCenterXMinY;
    if (id == QStringLiteral("top_max_x_min_y")) return SetupOriginAnchor::TopMaxXMinY;
    if (id == QStringLiteral("top_min_x_center_y")) return SetupOriginAnchor::TopMinXCenterY;
    if (id == QStringLiteral("top_max_x_center_y")) return SetupOriginAnchor::TopMaxXCenterY;
    if (id == QStringLiteral("top_min_x_max_y")) return SetupOriginAnchor::TopMinXMaxY;
    if (id == QStringLiteral("top_center_x_max_y")) return SetupOriginAnchor::TopCenterXMaxY;
    if (id == QStringLiteral("top_max_x_max_y")) return SetupOriginAnchor::TopMaxXMaxY;
    if (id == QStringLiteral("custom")) return SetupOriginAnchor::CustomPoint;
    return SetupOriginAnchor::TopCenter;
}
