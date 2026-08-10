#pragma once

#include "ProgramSnapshotFingerprint.h"

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

    bool okForOutput() const { return state == ProgramSnapshotState::Fresh || state == ProgramSnapshotState::Unchecked; }
    QString listSuffix() const;

    static ProgramSnapshotStatus evaluate(const QList<MachiningOperation> &operations,
                                          const QStringList &sourceOperationIds,
                                          const QString &storedFingerprint,
                                          const QString &currentSetupFingerprint = QString(),
                                          const QString &storedSetupFingerprint = QString());
};
