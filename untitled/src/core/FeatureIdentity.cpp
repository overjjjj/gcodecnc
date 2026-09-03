#include "FeatureIdentity.h"

#include "../import/StepImporter.h"

#include <QCryptographicHash>
#include <QStringList>

#include <algorithm>

namespace {

void appendNumber(QByteArray &payload, double value)
{
    payload.append(QByteArray::number(value, 'g', 17));
    payload.append('|');
}

void appendVector(QByteArray &payload, const QVector3D &value)
{
    appendNumber(payload, value.x());
    appendNumber(payload, value.y());
    appendNumber(payload, value.z());
}

QString pointToken(const QVector3D &point)
{
    return QStringLiteral("%1,%2,%3")
        .arg(double(point.x()), 0, 'g', 17)
        .arg(double(point.y()), 0, 'g', 17)
        .arg(double(point.z()), 0, 'g', 17);
}

QString canonicalPath(const QVector<QVector3D> &points, bool closed)
{
    if (points.isEmpty()) {
        return QString();
    }

    QStringList tokens;
    tokens.reserve(points.size());
    for (const QVector3D &point : points) {
        tokens.append(pointToken(point));
    }

    QString best;
    const int startCount = closed ? tokens.size() : 1;
    for (int direction = 0; direction < 2; ++direction) {
        for (int start = 0; start < startCount; ++start) {
            QStringList candidate;
            candidate.reserve(tokens.size());
            for (int offset = 0; offset < tokens.size(); ++offset) {
                int index = direction == 0 ? start + offset : start - offset;
                index %= tokens.size();
                if (index < 0) {
                    index += tokens.size();
                }
                candidate.append(tokens.at(index));
            }
            const QString value = candidate.join(QLatin1Char(';'));
            if (best.isEmpty() || value < best) {
                best = value;
            }
        }
    }
    return best;
}

} // namespace

QString stableFeatureId(const MachiningFeature &feature)
{
    QByteArray payload;
    payload.reserve(256);
    payload.append(QByteArray::number(static_cast<int>(feature.kind)));
    payload.append('|');
    payload.append(feature.subType.toUtf8());
    payload.append('|');
    appendNumber(payload, feature.radius);
    appendNumber(payload, feature.depth);
    appendNumber(payload, feature.width);
    appendNumber(payload, feature.length);
    appendNumber(payload, feature.secondaryRadius);
    appendNumber(payload, feature.pitch);
    appendVector(payload, feature.center);
    appendVector(payload, feature.axis);
    appendNumber(payload, feature.coneAngle);
    appendNumber(payload, feature.angle);
    payload.append(QByteArray::number(static_cast<int>(feature.region)));
    payload.append('|');

    const QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    return QStringLiteral("feature:") + QString::fromLatin1(digest.toHex());
}

QString stableContourId(const ContourFeature &feature)
{
    QByteArray payload;
    payload.reserve(256);
    payload.append(feature.subType.toUtf8());
    payload.append('|');
    appendNumber(payload, feature.radius);
    appendNumber(payload, feature.depth);
    appendNumber(payload, feature.width);
    appendNumber(payload, feature.length);
    appendNumber(payload, feature.angle);
    appendNumber(payload, feature.openSide);
    appendVector(payload, feature.center);
    appendVector(payload, feature.axis);
    payload.append(QByteArray::number(static_cast<int>(feature.region)));
    payload.append('|');
    const bool closed = feature.subType != QStringLiteral("open_contour");
    payload.append(canonicalPath(feature.points, closed).toUtf8());

    QStringList islands;
    islands.reserve(feature.islands.size());
    for (const QVector<QVector3D> &island : feature.islands) {
        islands.append(canonicalPath(island, true));
    }
    std::sort(islands.begin(), islands.end());
    for (const QString &island : islands) {
        payload.append('|');
        payload.append(island.toUtf8());
    }

    const QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    return QStringLiteral("contour:") + QString::fromLatin1(digest.toHex());
}
