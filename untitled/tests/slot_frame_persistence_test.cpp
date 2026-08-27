#include "../src/core/ProjectManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    MachiningFeature source;
    source.kind = FeatureKind::Pocket;
    source.subType = QStringLiteral("irregular_pocket");
    source.depth = 6.0;
    source.region = FaceRegion::Front;
    source.axis = QVector3D(0.0f, 0.0f, 1.0f);
    source.boundaryPoints = {
        QVector3D(0.0f, 0.0f, 0.0f), QVector3D(60.0f, 0.0f, 0.0f),
        QVector3D(60.0f, 40.0f, 0.0f), QVector3D(0.0f, 40.0f, 0.0f)};
    source.islandBoundaries = {{
        QVector3D(25.0f, 15.0f, 0.0f), QVector3D(35.0f, 15.0f, 0.0f),
        QVector3D(35.0f, 25.0f, 0.0f), QVector3D(25.0f, 25.0f, 0.0f)}};

    MachiningOperation operation;
    operation.id = QStringLiteral("slot-frame-operation");
    operation.opType = OperationType::Roughing;
    operation.strategyId = QStringLiteral("mill_pocket_rough");
    operation.toolId = 5;
    operation.geometryRefs = QStringList{QStringLiteral("slot-frame:stable-pocket")};
    operation.contourFeature.subType = source.subType;
    operation.contourFeature.depth = source.depth;
    operation.contourFeature.region = source.region;
    operation.contourFeature.axis = source.axis;
    operation.contourFeature.points = source.boundaryPoints;
    operation.contourFeature.islands = source.islandBoundaries;
    operation.markToolpathValid();

    const QString path = QDir::temp().filePath(
        QStringLiteral("cnext_slot_frame_persistence_test.cnext"));
    QFile::remove(path);
    ProjectManager project;
    project.setSourceFileFingerprint(QStringLiteral("slot-frame-source-v1"));
    project.setFeatures({source});
    project.setOperations({operation});
    if (!project.saveToFile(path)) {
        std::cerr << "FAIL: slot-frame project should save\n";
        return 1;
    }

    ProjectManager reopened;
    if (!reopened.loadFromFile(path) || reopened.operations().size() != 1 ||
        reopened.operations().first().geometryRefs != operation.geometryRefs ||
        reopened.operations().first().contourFeature.points !=
            operation.contourFeature.points ||
        reopened.operations().first().contourFeature.islands !=
            operation.contourFeature.islands) {
        std::cerr << "FAIL: slot-frame geometry and protection loops should reopen intact\n";
        return 1;
    }

    MachiningFeature changed = source;
    changed.boundaryPoints[1].setX(61.0f);
    reopened.replaceChangedSource(
        MeshData(), {changed}, QStringLiteral("changed.step"),
        QStringLiteral("slot-frame-source-v2"));
    if (reopened.operations().first().toolpathState != ToolpathState::Stale) {
        std::cerr << "FAIL: changed source geometry should invalidate slot-frame operations\n";
        return 1;
    }

    QFile::remove(path);
    return 0;
}
