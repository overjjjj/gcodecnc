#include "ProgramSnapshotStatus.h"

#include <QSet>

namespace {

static bool hasAllSourceOperations(const QList<MachiningOperation> &operations,
                                   const QStringList &sourceOperationIds)
{
    for (const QString &sourceOperationId : sourceOperationIds) {
        bool found = false;
        for (const MachiningOperation &operation : operations) {
            if (operation.id == sourceOperationId) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

static bool coversAllConfirmedOperations(const QList<MachiningOperation> &operations,
                                         const QStringList &sourceOperationIds)
{
    if (operations.size() != sourceOperationIds.size()) {
        return false;
    }

    QSet<QString> operationIds;
    for (const MachiningOperation &operation : operations) {
        operationIds.insert(operation.id);
    }
    return operationIds.size() == operations.size() &&
           QSet<QString>(sourceOperationIds.cbegin(), sourceOperationIds.cend()) == operationIds;
}

static bool preservesConfirmedOperationOrder(const QList<MachiningOperation> &operations,
                                             const QStringList &sourceOperationIds)
{
    if (operations.size() != sourceOperationIds.size()) {
        return false;
    }
    for (int index = 0; index < operations.size(); ++index) {
        if (operations[index].id != sourceOperationIds[index]) {
            return false;
        }
    }
    return true;
}

} // namespace

QString ProgramSnapshotStatus::listSuffix() const
{
    switch (state) {
    case ProgramSnapshotState::Incomplete:
        return QStringLiteral(" [incomplete]");
    case ProgramSnapshotState::Changed:
        return QStringLiteral(" [stale]");
    case ProgramSnapshotState::MissingSource:
        return QStringLiteral(" [missing source]");
    case ProgramSnapshotState::Fresh:
    case ProgramSnapshotState::Unchecked:
        return QString();
    }
    return QString();
}

ProgramSnapshotStatus ProgramSnapshotStatus::evaluate(const QList<MachiningOperation> &operations,
                                                       const QStringList &sourceOperationIds,
                                                       const QString &storedFingerprint,
                                                       const QString &currentSetupFingerprint,
                                                       const QString &storedSetupFingerprint)
{
    ProgramSnapshotStatus status;
    if (sourceOperationIds.isEmpty()) {
        if (!operations.isEmpty()) {
            status.state = ProgramSnapshotState::Incomplete;
            status.message = QStringLiteral("The program does not include the confirmed operations.");
            return status;
        }
        status.state = ProgramSnapshotState::Unchecked;
        return status;
    }

    if (!hasAllSourceOperations(operations, sourceOperationIds)) {
        status.state = ProgramSnapshotState::MissingSource;
        status.message = QStringLiteral("One or more source operations are missing.");
        return status;
    }

    if (!coversAllConfirmedOperations(operations, sourceOperationIds)) {
        status.state = ProgramSnapshotState::Incomplete;
        status.message = QStringLiteral("The program does not include every confirmed operation.");
        return status;
    }

    if (!preservesConfirmedOperationOrder(operations, sourceOperationIds)) {
        status.state = ProgramSnapshotState::Changed;
        status.message = QStringLiteral(
            "Confirmed operation order has changed since this program was generated.");
        return status;
    }

    if (currentSetupFingerprint != storedSetupFingerprint) {
        status.state = ProgramSnapshotState::Changed;
        status.message = QStringLiteral(
            "Setup origin, work offset or confirmation has changed since this program was generated.");
        return status;
    }

    if (storedFingerprint.trimmed().isEmpty()) {
        status.state = ProgramSnapshotState::Unchecked;
        return status;
    }

    const QString currentFingerprint =
        ProgramSnapshotFingerprint::calculate(operations, sourceOperationIds);
    if (currentFingerprint == storedFingerprint) {
        status.state = ProgramSnapshotState::Fresh;
        return status;
    }

    status.state = ProgramSnapshotState::Changed;
    status.message = QStringLiteral("Source operations have changed since this G-code was generated.");
    return status;
}
