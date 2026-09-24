#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <functional>
#include "../../../tool/ToolEntry.h"

#include "../../process_design/operations/MachiningOperation.h"

class ProgramSnapshotFingerprint
{
public:
    static QString calculate(const QList<MachiningOperation> &operations,
                             const QStringList &sourceOperationIds);
    static QString toolFingerprint(const ToolEntry &tool);
    static QString calculateTools(const QList<MachiningOperation> &operations,
                                  const QStringList &sourceOperationIds,
                                  const std::function<ToolEntry(int)> &toolLookup);
};
