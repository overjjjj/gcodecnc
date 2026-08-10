#include "../src/gcode/ProgramSnapshotFingerprint.h"

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

static MachiningOperation samplePocketOperation()
{
    MachiningOperation operation;
    operation.id = QStringLiteral("pocket-op");
    operation.opType = OperationType::Roughing;
    operation.stage = OperationStage::RoughCut;
    operation.strategyId = QStringLiteral("mill_pocket_rough");
    operation.toolId = 3;
    operation.params.set(QStringLiteral("entryMode"), 0.0);
    operation.contourFeature.subType = QStringLiteral("irregular_pocket");
    operation.contourFeature.depth = 5.0;
    operation.contourFeature.points = {
        QVector3D(0.0f, 0.0f, 0.0f),
        QVector3D(30.0f, 0.0f, 0.0f),
        QVector3D(25.0f, 20.0f, 0.0f),
        QVector3D(0.0f, 15.0f, 0.0f)
    };
    operation.contourFeature.islands = {{
        QVector3D(10.0f, 7.0f, 0.0f),
        QVector3D(15.0f, 7.0f, 0.0f),
        QVector3D(15.0f, 11.0f, 0.0f),
        QVector3D(10.0f, 11.0f, 0.0f)
    }};
    return operation;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QList<MachiningOperation> operations{sampleOperation()};
    const QStringList sourceIds{QStringLiteral("op-1")};

    const QString original = ProgramSnapshotFingerprint::calculate(operations, sourceIds);
    const QString repeated = ProgramSnapshotFingerprint::calculate(operations, sourceIds);
    if (expect(!original.isEmpty(), "fingerprint should not be empty for sourced program")) {
        return 1;
    }
    if (expect(original == repeated, "same operation should produce stable fingerprint")) {
        return 1;
    }

    operations[0].toolId = 2;
    const QString changedTool = ProgramSnapshotFingerprint::calculate(operations, sourceIds);
    if (expect(original != changedTool, "changing source tool should change fingerprint")) {
        return 1;
    }

    operations[0] = sampleOperation();
    operations[0].params.set(QStringLiteral("feedRate"), 240.0);
    const QString changedParam = ProgramSnapshotFingerprint::calculate(operations, sourceIds);
    if (expect(original != changedParam, "changing source params should change fingerprint")) {
        return 1;
    }

    operations[0] = sampleOperation();
    operations[0].holeFeature.depth = 15.0;
    const QString changedFeature = ProgramSnapshotFingerprint::calculate(operations, sourceIds);
    if (expect(original != changedFeature, "changing source feature should change fingerprint")) {
        return 1;
    }

    QList<MachiningOperation> pocketOperations{samplePocketOperation()};
    const QString pocketOriginal = ProgramSnapshotFingerprint::calculate(
        pocketOperations, {QStringLiteral("pocket-op")});
    pocketOperations[0].contourFeature.islands[0][0].setX(11.0f);
    const QString changedIsland = ProgramSnapshotFingerprint::calculate(
        pocketOperations, {QStringLiteral("pocket-op")});
    if (expect(pocketOriginal != changedIsland,
               "changing a pocket island boundary should invalidate the program snapshot")) {
        return 1;
    }

    const QString missingOperation = ProgramSnapshotFingerprint::calculate(operations, {QStringLiteral("missing")});
    if (expect(original != missingOperation, "missing source operation should not match original fingerprint")) {
        return 1;
    }

    return 0;
}
