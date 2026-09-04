#include "StockDefinition.h"

#include <QCryptographicHash>

#include <algorithm>

// 将六个方向的毛坯余量下限限制为零，直接更新当前定义。
void StockDefinition::normalize()
{
    minusX = std::max(0.0, minusX);
    plusX = std::max(0.0, plusX);
    minusY = std::max(0.0, minusY);
    plusY = std::max(0.0, plusY);
    minusZ = std::max(0.0, minusZ);
    plusZ = std::max(0.0, plusZ);
}

// 用非负余量向外扩展零件包围盒，返回毛坯边界而不修改当前定义。
StockBounds StockDefinition::resolvedBounds(const QVector3D &partMin,
                                             const QVector3D &partMax) const
{
    StockDefinition value = *this;
    value.normalize();
    StockBounds bounds;
    bounds.minimum = partMin - QVector3D(float(value.minusX),
                                         float(value.minusY),
                                         float(value.minusZ));
    bounds.maximum = partMax + QVector3D(float(value.plusX),
                                         float(value.plusY),
                                         float(value.plusZ));
    return bounds;
}

// 按规范化余量生成已确认毛坯的 SHA-256 指纹；未确认时返回空串。
QString StockDefinition::fingerprint() const
{
    if (!confirmed) {
        return QString();
    }
    StockDefinition value = *this;
    value.normalize();
    const QString payload = QStringLiteral("rect-v1|%1|%2|%3|%4|%5|%6")
        .arg(value.minusX, 0, 'f', 6)
        .arg(value.plusX, 0, 'f', 6)
        .arg(value.minusY, 0, 'f', 6)
        .arg(value.plusY, 0, 'f', 6)
        .arg(value.minusZ, 0, 'f', 6)
        .arg(value.plusZ, 0, 'f', 6);
    return QString::fromLatin1(QCryptographicHash::hash(payload.toUtf8(),
                                                        QCryptographicHash::Sha256).toHex());
}
