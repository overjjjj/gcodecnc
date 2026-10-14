#pragma once

#include <QString>
#include "Coordinate3D.h"

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
    Coordinate3D customPoint;
    Coordinate3D offset;
    bool confirmed = false;

    Coordinate3D resolvedPoint(const Coordinate3D &bbMin, const Coordinate3D &bbMax) const;
    QString fingerprint(const QString &workOffset) const;
};

QString setupOriginAnchorId(SetupOriginAnchor anchor);
SetupOriginAnchor setupOriginAnchorFromId(const QString &id);
