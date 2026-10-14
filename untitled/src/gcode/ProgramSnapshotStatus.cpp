#include "ProgramSnapshotStatus.h"
#include "../workspaces/process_design/operations/OperationWorkflow.h"

#include <QSet>

namespace {

// 函数说明：hasAllSourceOperations负责当前模块的核心处理。
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

// 函数说明：coversAllConfirmedOperations负责当前模块的核心处理。
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

// 函数说明：preservesConfirmedOperationOrder负责当前模块的核心处理。
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

// 函数说明：listSuffix负责当前模块的核心处理。
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

// 函数说明：evaluate负责当前模块的核心处理。
ProgramSnapshotStatus ProgramSnapshotStatus::evaluate(const QList<MachiningOperation> &operations,
                                                       const QStringList &sourceOperationIds,
                                                       const QString &storedFingerprint,
                                                       const QString &currentSetupFingerprint,
                                                       const QString &storedSetupFingerprint,
                                                       const QString &currentToolFingerprint,
                                                       const QString &storedToolFingerprint,
                                                       bool coordinatesVerified)
{
    ProgramSnapshotStatus status;
    if (!coordinatesVerified) {
        status.state = ProgramSnapshotState::Changed;
        status.message = QStringLiteral("Work-coordinate evidence is missing or unsupported; regenerate the program.");
        return status;
    }
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

    const auto activeOperations = enabledOperations(operations);
    for(const auto &operation:activeOperations)for(const auto &loop:operation.selectionEvidence.boundaryLoops) {
        if(loop.machiningRole==MachiningBoundaryRole::Outer && !openingEvidenceSupported(loop)) {
            status.state=ProgramSnapshotState::Changed;
            status.message=QStringLiteral("Opening geometry evidence is missing or unsupported; select the source boundary again.");
            return status;
        }
    }
    if (!coversAllConfirmedOperations(activeOperations, sourceOperationIds)) {
        status.state = ProgramSnapshotState::Incomplete;
        status.message = QStringLiteral("The program does not match the currently enabled operations.");
        return status;
    }

    if (!preservesConfirmedOperationOrder(activeOperations, sourceOperationIds)) {
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

    if (storedToolFingerprint.trimmed().isEmpty() || currentToolFingerprint.trimmed().isEmpty() ||
        storedToolFingerprint != currentToolFingerprint) {
        status.state = ProgramSnapshotState::Changed;
        status.message = QStringLiteral("Tool definitions changed or tool evidence is missing; regenerate the program.");
        return status;
    }

    if (storedFingerprint.trimmed().isEmpty()) {
        status.state = ProgramSnapshotState::Changed;
        status.message = QStringLiteral("Source operation evidence is missing; regenerate the program.");
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
