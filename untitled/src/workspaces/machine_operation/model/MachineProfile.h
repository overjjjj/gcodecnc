#pragma once

#include <QString>
#include <QStringList>

struct MachineProfile
{
    QString id = QStringLiteral("siemens-840d-default");
    QString version = QStringLiteral("1");
    QString controllerId = QStringLiteral("siemens-840d");
    QStringList safeStartBlocks = {
        QStringLiteral("G17 G40 G49 G80"),
        QStringLiteral("G21"),
        QStringLiteral("G90"),
        QStringLiteral("G54"),
        QStringLiteral("G94")
    };
};
