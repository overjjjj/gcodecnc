#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "../strategies/MachiningOperation.h"

class ProgramSnapshotFingerprint
{
public:
    static QString calculate(const QList<MachiningOperation> &operations,
                             const QStringList &sourceOperationIds);
};
