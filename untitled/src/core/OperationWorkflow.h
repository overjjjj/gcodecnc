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

inline QList<MachiningOperation> executableOperations(
    const QList<MachiningOperation> &operations)
{
    QList<MachiningOperation> executable;
    executable.reserve(operations.size());
    for (const MachiningOperation &operation : operations) {
        if (operation.enabled && operation.toolpathState == ToolpathState::Valid) {
            executable.append(operation);
        }
    }
    return executable;
}

inline void markOperationsStale(QList<MachiningOperation> &operations,
                                const QString &reason)
{
    for (MachiningOperation &operation : operations) {
        operation.markToolpathStale(reason);
    }
}

inline bool updateOperationParameters(
    QList<MachiningOperation> *operations,
    const QString &operationId,
    const StrategyParams &params)
{
    if (!operations || operationId.trimmed().isEmpty()) {
        return false;
    }
    for (MachiningOperation &operation : *operations) {
        if (operation.id != operationId) {
            continue;
        }
        for (auto it = params.values.cbegin(); it != params.values.cend(); ++it) {
            if (!operation.params.values.contains(it.key()) ||
                operation.params.values.value(it.key()) != it.value()) {
                operation.parameterSources.insert(
                    it.key(), ProcessParameterSource::ManualOverride);
            }
        }
        operation.params = params;
        operation.markToolpathStale(QStringLiteral("parameters changed"));
        return true;
    }
    return false;
}
