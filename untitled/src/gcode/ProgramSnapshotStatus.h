#pragma once

#include "../workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"

#include <QList>
#include <QString>
#include <QStringList>

enum class ProgramSnapshotState {
    Fresh,
    Incomplete,
    Changed,
    MissingSource,
    Unchecked
};

struct ProgramSnapshotStatus
{
    ProgramSnapshotState state = ProgramSnapshotState::Unchecked;
    QString message;

    // 只表示来源一致性检查允许继续；Unchecked 没有来源可核对，仍需独立执行文本安全检查。
    bool okForOutput() const { return state == ProgramSnapshotState::Fresh || state == ProgramSnapshotState::Unchecked; }
    QString listSuffix() const;

    static ProgramSnapshotStatus evaluate(const QList<MachiningOperation> &operations,
                                          const QStringList &sourceOperationIds,
                                          const QString &storedFingerprint,
                                          const QString &currentSetupFingerprint = QString(),
                                          const QString &storedSetupFingerprint = QString(),
                                          const QString &currentToolFingerprint = QString(),
                                          const QString &storedToolFingerprint = QString());
};
