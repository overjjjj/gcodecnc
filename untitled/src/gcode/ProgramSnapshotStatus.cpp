#include "ProgramSnapshotStatus.h"
#include "../strategies/hole/HoleStrategyUtils.h"
#include "../strategies/mill/BlindSlotMillingStrategy.h"
#include "../strategies/mill/TaperedSlotMillingStrategy.h"

#include <QSet>

namespace {

// 先区分来源被删除和当前工序集合变多，两种情况需要不同的失效提示。
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

// 数量和 ID 集合都必须一致，同时拒绝当前工序重复 ID，防止误判为完整覆盖。
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

// 集合相同仍可能重排；加工先后改变也必须重新生成程序。
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

// 依次核对来源存在性、完整性、顺序和摘要；只判断快照是否仍对应当前输入。
ProgramSnapshotStatus ProgramSnapshotStatus::evaluate(const QList<MachiningOperation> &operations,
                                                       const QStringList &sourceOperationIds,
                                                       const QString &storedFingerprint,
                                                       const QString &currentSetupFingerprint,
                                                       const QString &storedSetupFingerprint,
                                                       const QString &currentToolFingerprint,
                                                       const QString &storedToolFingerprint)
{
    ProgramSnapshotStatus status;
    if (sourceOperationIds.isEmpty()) {
        if (!operations.isEmpty()) {
            status.state = ProgramSnapshotState::Incomplete;
            status.message = QStringLiteral("The program does not include the confirmed operations.");
            return status;
        }
        // 项目与程序都没有关联工序时无法比较来源，保留为 Unchecked，不能解释为安全已验证。
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

    // 缺少刀具证据与刀具发生变化同样处理，旧项目必须重新生成以补齐证据。
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
    for (const auto &operation : operations) {
        if (sourceOperationIds.contains(operation.id) && operation.opType == OperationType::Hole &&
            (!holeAxisAlignedWithZ(operation.holeFeature) || operation.holeFeature.region == FaceRegion::Side ||
             operation.holeFeature.region == FaceRegion::Back)) {
            status.state = ProgramSnapshotState::Changed;
            status.message = QStringLiteral("This program contains a hole incompatible with the current Z Setup. Correct the Setup and regenerate; export and machine send are blocked.");
            return status;
        }
        if (sourceOperationIds.contains(operation.id) &&
            operation.strategyId == QStringLiteral("mill_blind_slot") &&
            BlindSlotMillingStrategy::isUnsupportedG42(operation.params.get(QStringLiteral("compensation"), 0.0))) {
            status.state = ProgramSnapshotState::Changed;
            status.message = QStringLiteral("This program contains unsupported blind slot G42 compensation. Use CAM offset (compensation = 0), then recalculate and regenerate the program. Export and machine send are blocked.");
            return status;
        }
        if (sourceOperationIds.contains(operation.id) &&
            operation.strategyId == QStringLiteral("mill_tapered_slot") &&
            TaperedSlotMillingStrategy::isLinearDirectionProfile(operation.contourFeature)) {
            status.state = ProgramSnapshotState::Changed;
            status.message = QStringLiteral("This program contains a disabled linear-direction tapered slot floor: cutter envelope safety is not proven. Export and machine send are blocked.");
            return status;
        }
        if (sourceOperationIds.contains(operation.id) &&
            (operation.toolpathState == ToolpathState::Error || operation.toolpathState == ToolpathState::Stale)) {
            status.state = ProgramSnapshotState::Changed;
            status.message = QStringLiteral("A source toolpath is invalid; recalculate and regenerate the program.");
            return status;
        }
    }
    if (currentFingerprint == storedFingerprint) {
        status.state = ProgramSnapshotState::Fresh;
        return status;
    }

    status.state = ProgramSnapshotState::Changed;
    status.message = QStringLiteral("Source operations have changed since this G-code was generated.");
    return status;
}
