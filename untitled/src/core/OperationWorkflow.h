#pragma once

#include "../strategies/MachiningOperation.h"

#include <QList>

inline QList<MachiningOperation> enabledOperations(
    const QList<MachiningOperation> &operations)
{
    QList<MachiningOperation> enabled;
    enabled.reserve(operations.size());
    for (const MachiningOperation &operation : operations) {
        if (operation.enabled) {
            enabled.append(operation);
        }
    }
    return enabled;
}

inline void markOperationsStale(QList<MachiningOperation> &operations,
                                const QString &reason)
{
    for (MachiningOperation &operation : operations) {
        operation.markToolpathStale(reason);
    }
}
