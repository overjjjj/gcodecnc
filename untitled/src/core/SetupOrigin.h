#pragma once

#include <QString>
#include <QVector3D>

enum class SetupOriginAnchor {
    TopMinXMinY,
    TopCenterXMinY,
    TopMaxXMinY,
    TopMinXCenterY,
    TopCenter,
    TopMaxXCenterY,
    TopMinXMaxY,
    TopCenterXMaxY,
    TopMaxXMaxY,
    CustomPoint
};

struct SetupOrigin
{
    SetupOriginAnchor anchor = SetupOriginAnchor::TopCenter;
    QVector3D customPoint;
    QVector3D offset;
    bool confirmed = false;

    QVector3D resolvedPoint(const QVector3D &bbMin, const QVector3D &bbMax) const;
    QString fingerprint(const QString &workOffset) const;
};

QString setupOriginAnchorId(SetupOriginAnchor anchor);
SetupOriginAnchor setupOriginAnchorFromId(const QString &id);
