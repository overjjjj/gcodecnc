#pragma once

#include "SetupOrigin.h"

#include <QQuaternion>
#include <QString>
#include <QStringList>

struct SetupContext {
    QString workOffset = QStringLiteral("G54");
    SetupOrigin origin;
    QQuaternion rotation;

    int workOffsetNumber() const;
    QStringList validate() const;
};

struct ProcessContext {
    SetupContext setup;
    int toolId = -1;
    QString materialId;
    QString toolTypeId;

    QStringList validate() const;
};
