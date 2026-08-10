#include "../src/gcode/ProgramSnapshotStatus.h"

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector3D>
#include <iostream>

static int expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << "\n";
        return 1;
    }
    return 0;
}

static MachiningOperation sampleOperation()
{
    MachiningOperation operation;
    operation.id = QStringLiteral("op-1");
    operation.opType = OperationType::Hole;
    operation.stage = OperationStage::RoughCut;
    operation.featureRef = QStringLiteral("Hole D10 Z-12");
    operation.strategyId = QStringLiteral("peck_drill");
    operation.toolId = 1;
    operation.params.set(QStringLiteral("safeHeight"), 50.0);
    operation.params.set(QStringLiteral("feedRate"), 120.0);
    operation.holeFeature.radius = 5.0;
    operation.holeFeature.depth = 12.0;
    operation.holeFeature.center = QVector3D(10.0f, 20.0f, 0.0f);
    return operation;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const QList<MachiningOperation> operations{sampleOperation()};
    const QStringList sourceIds{QStringLiteral("op-1")};
    const QString fingerprint =
        ProgramSnapshotFingerprint::calculate(operations, sourceIds);

    const ProgramSnapshotStatus fresh =
        ProgramSnapshotStatus::evaluate(operations, sourceIds, fingerprint);
    if (expect(fresh.state == ProgramSnapshotState::Fresh, "matching fingerprint should be fresh")) {
        return 1;
    }
    if (expect(fresh.message.isEmpty(), "fresh snapshot should not have an error message")) {
        return 1;
    }

    const ProgramSnapshotStatus setupFresh = ProgramSnapshotStatus::evaluate(
        operations, sourceIds, fingerprint, QStringLiteral("setup-a"), QStringLiteral("setup-a"));
    if (expect(setupFresh.state == ProgramSnapshotState::Fresh,
               "matching Setup fingerprint should remain fresh")) {
        return 1;
    }
    const ProgramSnapshotStatus setupChanged = ProgramSnapshotStatus::evaluate(
        operations, sourceIds, fingerprint, QStringLiteral("setup-b"), QStringLiteral("setup-a"));
    if (expect(setupChanged.state == ProgramSnapshotState::Changed
                   && setupChanged.message.contains(QStringLiteral("Setup")),
               "changing Setup origin or WCS should stale the program")) {
        return 1;
    }

    MachiningOperation secondOperation = sampleOperation();
    secondOperation.id = QStringLiteral("op-2");
    secondOperation.holeFeature.center.setX(30.0f);
    const QList<MachiningOperation> expandedOperations{sampleOperation(), secondOperation};
    const ProgramSnapshotStatus incomplete =
        ProgramSnapshotStatus::evaluate(expandedOperations, sourceIds, fingerprint);
    if (expect(incomplete.state == ProgramSnapshotState::Incomplete,
               "a snapshot that omits a confirmed operation should be incomplete")) {
        return 1;
    }
    if (expect(!incomplete.okForOutput(),
               "an incomplete snapshot must not be eligible for output")) {
        return 1;
    }
    if (expect(incomplete.listSuffix() == QStringLiteral(" [incomplete]"),
               "an incomplete snapshot should have a visible list suffix")) {
        return 1;
    }

    const QStringList expandedSourceIds{QStringLiteral("op-1"), QStringLiteral("op-2")};
    const QString expandedFingerprint =
        ProgramSnapshotFingerprint::calculate(expandedOperations, expandedSourceIds);
    const ProgramSnapshotStatus expandedFresh =
        ProgramSnapshotStatus::evaluate(expandedOperations,
                                        expandedSourceIds,
                                        expandedFingerprint);
    if (expect(expandedFresh.state == ProgramSnapshotState::Fresh,
               "a snapshot covering every confirmed operation should be fresh")) {
        return 1;
    }

    const QStringList reversedSourceIds{QStringLiteral("op-2"), QStringLiteral("op-1")};
    const QString reversedFingerprint =
        ProgramSnapshotFingerprint::calculate(expandedOperations, reversedSourceIds);
    const ProgramSnapshotStatus reordered =
        ProgramSnapshotStatus::evaluate(expandedOperations,
                                        reversedSourceIds,
                                        reversedFingerprint);
    if (expect(reordered.state == ProgramSnapshotState::Changed,
               "changing confirmed-operation order should stale the old program")) {
        return 1;
    }

    const ProgramSnapshotStatus unsourcedProgram =
        ProgramSnapshotStatus::evaluate(expandedOperations, {}, QString());
    if (expect(unsourcedProgram.state == ProgramSnapshotState::Incomplete,
               "an unsourced program must not bypass confirmed operations")) {
        return 1;
    }

    QList<MachiningOperation> changedOperations{sampleOperation()};
    changedOperations[0].params.set(QStringLiteral("feedRate"), 240.0);
    const ProgramSnapshotStatus stale =
        ProgramSnapshotStatus::evaluate(changedOperations, sourceIds, fingerprint);
    if (expect(stale.state == ProgramSnapshotState::Changed, "changed operation should be reported")) {
        return 1;
    }
    if (expect(stale.message.contains(QStringLiteral("changed")), "changed message should explain the cause")) {
        return 1;
    }
    if (expect(stale.listSuffix() == QStringLiteral(" [stale]"), "changed snapshot should have stale list suffix")) {
        return 1;
    }

    const ProgramSnapshotStatus missing =
        ProgramSnapshotStatus::evaluate({}, sourceIds, fingerprint);
    if (expect(missing.state == ProgramSnapshotState::MissingSource, "missing operation should be reported")) {
        return 1;
    }
    if (expect(missing.message.contains(QStringLiteral("missing")), "missing message should explain the cause")) {
        return 1;
    }
    if (expect(missing.listSuffix() == QStringLiteral(" [missing source]"),
               "missing source snapshot should have missing-source list suffix")) {
        return 1;
    }

    const ProgramSnapshotStatus unchecked =
        ProgramSnapshotStatus::evaluate(operations, sourceIds, QString());
    if (expect(unchecked.state == ProgramSnapshotState::Unchecked, "missing stored fingerprint should be unchecked")) {
        return 1;
    }
    if (expect(unchecked.listSuffix().isEmpty(), "unchecked snapshot should not add a list suffix")) {
        return 1;
    }

    return 0;
}
