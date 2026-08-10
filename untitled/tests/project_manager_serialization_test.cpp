#include "../src/core/ProjectManager.h"
#include "../src/gcode/ProgramSnapshotFingerprint.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuaternion>
#include <QString>
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

static QJsonObject readJsonObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

static bool writeLegacyProject(const QString &path)
{
    QJsonObject operation;
    operation[QStringLiteral("id")] = QStringLiteral("op-legacy");
    operation[QStringLiteral("opType")] = QStringLiteral("hole");
    operation[QStringLiteral("stage")] = QStringLiteral("rough_cut");
    operation[QStringLiteral("featureRef")] = QStringLiteral("Legacy hole");
    operation[QStringLiteral("strategyId")] = QStringLiteral("spot_drill");
    operation[QStringLiteral("toolId")] = 1;
    operation[QStringLiteral("params")] = QJsonObject();
    operation[QStringLiteral("holeFeature")] = QJsonObject();

    QJsonArray sourceOperationIds;
    sourceOperationIds.append(QStringLiteral("op-legacy"));
    QJsonObject program;
    program[QStringLiteral("id")] = QStringLiteral("program-legacy");
    program[QStringLiteral("name")] = QStringLiteral("Legacy Program");
    program[QStringLiteral("postProcessorId")] = QStringLiteral("fanuc");
    program[QStringLiteral("sourceOperationIds")] = sourceOperationIds;
    program[QStringLiteral("sourceSummary")] = QStringLiteral("legacy");
    program[QStringLiteral("gcodeText")] = QStringLiteral("M30\n");
    program[QStringLiteral("macroText")] = QString();
    program[QStringLiteral("lineCount")] = 1;
    program[QStringLiteral("createdAt")] = QStringLiteral("2026-07-20T00:00:00");

    QJsonObject root;
    root[QStringLiteral("version")] = QStringLiteral("1.5");
    root[QStringLiteral("sourceFilePath")] = QStringLiteral("legacy.stp");
    root[QStringLiteral("currentProgramId")] = QStringLiteral("program-legacy");
    root[QStringLiteral("features")] = QJsonArray();
    root[QStringLiteral("operations")] = QJsonArray{operation};
    root[QStringLiteral("programs")] = QJsonArray{program};

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const QString projectPath =
        QDir::temp().filePath(QStringLiteral("cnext_project_manager_serialization_test.cnext"));
    QFile::remove(projectPath);

    const MachiningOperation operation = sampleOperation();
    const QList<MachiningOperation> operations{operation};
    const QStringList sourceIds{operation.id};

    ProgramEntry program;
    program.id = QStringLiteral("program-1");
    program.name = QStringLiteral("Snapshot Program");
    program.postProcessorId = QStringLiteral("siemens");
    program.sourceOperationIds = sourceIds;
    program.sourceOperationFingerprint =
        ProgramSnapshotFingerprint::calculate(operations, sourceIds);
    program.sourceSummary = QStringLiteral("unit-test");
    program.gcodeText = QStringLiteral("G17 G40 G49 G80\nM30\n");
    program.mainProgramFileName = QStringLiteral("WH250852_MAIN.MPF");
    ProgramFileEntry mainFile;
    mainFile.kind = QStringLiteral("main");
    mainFile.fileName = program.mainProgramFileName;
    mainFile.content = QStringLiteral("PROC WH250852_MAIN\nM30\n");
    mainFile.sha256 = QString(64, QLatin1Char('A'));
    ProgramFileEntry subprogramFile;
    subprogramFile.kind = QStringLiteral("subprogram");
    subprogramFile.fileName = QStringLiteral("SP_HOLE_CYCLE_81.SPF");
    subprogramFile.content = QStringLiteral("PROC SP_HOLE_CYCLE_81(REAL PX)\nRET\n");
    subprogramFile.sha256 = QString(64, QLatin1Char('B'));
    program.packageFiles = QList<ProgramFileEntry>{mainFile, subprogramFile};

    ProjectManager project;
    project.setSourceFilePath(QStringLiteral("sample.stp"));
    project.setSourceFileFingerprint(QStringLiteral("sha256:test-source"));
    const QQuaternion setupRotation =
        QQuaternion::fromAxisAndAngle(QVector3D(1.0f, 0.0f, 0.0f), 90.0f);
    project.setSetupRotation(setupRotation);
    project.setActiveRegion(FaceRegion::Front);
    project.setWorkOffset(QStringLiteral("G55"));
    SetupOrigin setupOrigin;
    setupOrigin.anchor = SetupOriginAnchor::TopMinXMaxY;
    setupOrigin.offset = QVector3D(1.0f, -2.0f, 0.5f);
    setupOrigin.confirmed = true;
    project.setSetupOrigin(setupOrigin);
    StockDefinition stock;
    stock.minusX = 1.0;
    stock.plusX = 2.0;
    stock.minusY = 3.0;
    stock.plusY = 4.0;
    stock.minusZ = 0.5;
    stock.plusZ = 6.0;
    stock.confirmed = true;
    project.setStockDefinition(stock);
    program.setupFingerprint = project.setupFingerprint();
    MachineProfile profile;
    profile.id = QStringLiteral("siemens-840d-shop");
    profile.version = QStringLiteral("3");
    profile.controllerId = QStringLiteral("siemens-840d");
    profile.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"),
        QStringLiteral("G21"),
        QStringLiteral("G90"),
        QStringLiteral("G55"),
        QStringLiteral("G94")
    };
    project.setMachineProfile(profile);
    MachiningFeature feature;
    feature.kind = FeatureKind::Slot;
    feature.subType = QStringLiteral("open_slot");
    feature.angle = 37.5;
    feature.faceIndices = {3, 7, 11};
    feature.boundaryPoints = {
        QVector3D(0.0f, 0.0f, 0.0f),
        QVector3D(20.0f, 0.0f, 0.0f),
        QVector3D(20.0f, 10.0f, 0.0f),
        QVector3D(0.0f, 10.0f, 0.0f)
    };
    feature.islandBoundaries = {{
        QVector3D(8.0f, 4.0f, 0.0f),
        QVector3D(12.0f, 4.0f, 0.0f),
        QVector3D(12.0f, 6.0f, 0.0f),
        QVector3D(8.0f, 6.0f, 0.0f)
    }};
    project.setFeatures({feature});
    project.setOperations(operations);
    project.upsertProgram(program);
    project.setCurrentProgramId(program.id);

    if (expect(project.saveToFile(projectPath), "project should save")) {
        return 1;
    }

    const QJsonObject savedRoot = readJsonObject(projectPath);
    if (expect(savedRoot[QStringLiteral("version")].toString() == QStringLiteral("2.0"),
               "project file version should be 2.0 after Program Package persistence")) {
        return 1;
    }
    if (expect(savedRoot[QStringLiteral("sourceFileFingerprint")].toString()
                   == QStringLiteral("sha256:test-source"),
               "source STEP fingerprint should be written")) {
        return 1;
    }
    const QJsonObject savedProgram =
        savedRoot[QStringLiteral("programs")].toArray().first().toObject();
    if (expect(savedProgram[QStringLiteral("sourceOperationFingerprint")].toString()
                   == program.sourceOperationFingerprint,
               "program fingerprint should be written")) {
        return 1;
    }
    const QJsonObject savedPackage =
        savedProgram[QStringLiteral("programPackage")].toObject();
    if (expect(savedPackage[QStringLiteral("mainProgramFileName")].toString()
                       == program.mainProgramFileName &&
                   savedPackage[QStringLiteral("files")].toArray().size() == 2,
               "complete MPF/SPF package metadata should be written")) {
        return 1;
    }

    ProjectManager loaded;
    if (expect(loaded.loadFromFile(projectPath), "project should load")) {
        return 1;
    }
    const ProgramEntry loadedProgram = loaded.programById(program.id);
    if (expect(loaded.currentProgramId() == program.id, "current program id should round-trip")) {
        return 1;
    }
    if (expect(loadedProgram.sourceOperationFingerprint == program.sourceOperationFingerprint,
               "program fingerprint should round-trip")) {
        return 1;
    }
    if (expect(loadedProgram.setupFingerprint == program.setupFingerprint,
               "program Setup fingerprint should round-trip")) {
        return 1;
    }
    if (expect(loadedProgram.mainProgramFileName == program.mainProgramFileName &&
                   loadedProgram.packageFiles.size() == 2 &&
                   loadedProgram.packageFiles.at(0).fileName == mainFile.fileName &&
                   loadedProgram.packageFiles.at(0).content == mainFile.content &&
                   loadedProgram.packageFiles.at(0).sha256 == mainFile.sha256 &&
                   loadedProgram.packageFiles.at(1).fileName == subprogramFile.fileName &&
                   loadedProgram.packageFiles.at(1).content == subprogramFile.content &&
                   loadedProgram.packageFiles.at(1).sha256 == subprogramFile.sha256,
               "complete MPF/SPF package should round-trip")) {
        return 1;
    }
    if (expect(!loaded.operations().isEmpty() && loaded.operations().first().id == operation.id,
               "operation id should round-trip")) {
        return 1;
    }
    if (expect(loaded.sourceFileFingerprint() == QStringLiteral("sha256:test-source"),
               "source STEP fingerprint should round-trip")) {
        return 1;
    }
    if (expect(loaded.features().size() == 1
                   && loaded.features().first().angle == feature.angle,
               "feature angle should round-trip")) {
        return 1;
    }
    if (expect(loaded.features().first().faceIndices == feature.faceIndices,
               "feature face indices should round-trip")) {
        return 1;
    }
    if (expect(loaded.features().first().boundaryPoints == feature.boundaryPoints,
               "recognized outer pocket boundary should round-trip")) {
        return 1;
    }
    if (expect(loaded.features().first().islandBoundaries == feature.islandBoundaries,
               "recognized pocket island boundaries should round-trip")) {
        return 1;
    }
    if (expect(qAbs(QQuaternion::dotProduct(loaded.setupRotation(), setupRotation)) > 0.9999f,
               "Setup rotation should round-trip")) {
        return 1;
    }
    if (expect(loaded.activeRegion() == FaceRegion::Front,
               "active Setup region should round-trip")) {
        return 1;
    }
    if (expect(loaded.workOffset() == QStringLiteral("G55"),
               "Setup work offset should round-trip")) {
        return 1;
    }
    if (expect(loaded.setupOrigin().confirmed
                   && loaded.setupOrigin().anchor == SetupOriginAnchor::TopMinXMaxY
                   && (loaded.setupOrigin().offset - setupOrigin.offset).length() < 1.0e-5f,
               "Setup origin anchor, offset and confirmation should round-trip")) {
        return 1;
    }
    if (expect(loaded.stockDefinition().confirmed
                   && loaded.stockDefinition().minusX == stock.minusX
                   && loaded.stockDefinition().plusY == stock.plusY
                   && loaded.stockDefinition().plusZ == stock.plusZ,
               "rectangular stock allowances and confirmation should round-trip")) {
        return 1;
    }
    if (expect(loaded.setupFingerprint() == project.setupFingerprint(),
               "Setup fingerprint should reflect the restored origin, WCS and stock")) {
        return 1;
    }
    if (expect(loaded.machineProfile().id == profile.id
                   && loaded.machineProfile().version == profile.version
                   && loaded.machineProfile().controllerId == profile.controllerId
                   && loaded.machineProfile().safeStartBlocks == profile.safeStartBlocks,
               "Machine Profile should round-trip")) {
        return 1;
    }
    if (expect(loadedProgram.machineProfileId == profile.id
                   && loadedProgram.machineProfileVersion == profile.version
                   && loadedProgram.safeStartBlocks == profile.safeStartBlocks,
               "Program Snapshot should capture the active Machine Profile")) {
        return 1;
    }

    MeshData replacementMesh;
    replacementMesh.triangles.append(Triangle());
    MachiningFeature replacementFeature;
    replacementFeature.kind = FeatureKind::Hole;
    replacementFeature.subType = QStringLiteral("blind_hole");
    ProjectManager relinked;
    if (expect(relinked.loadFromFile(projectPath), "relink fixture should load")) {
        return 1;
    }
    relinked.replaceChangedSource(replacementMesh,
                                  QVector<MachiningFeature>{replacementFeature},
                                  QStringLiteral("replacement.step"),
                                  QStringLiteral("sha256:replacement"));
    if (expect(relinked.sourceFilePath() == QStringLiteral("replacement.step")
                   && relinked.sourceFileFingerprint() == QStringLiteral("sha256:replacement"),
               "changed source replacement should update source identity")) {
        return 1;
    }
    if (expect(relinked.operations().isEmpty() && relinked.programs().isEmpty()
                   && relinked.currentProgramId().isEmpty(),
               "changed source replacement should invalidate derived operations and programs")) {
        return 1;
    }
    if (expect(!relinked.mesh().isEmpty() && relinked.features().size() == 1,
               "changed source replacement should install the re-recognized model")) {
        return 1;
    }
    if (expect(qAbs(QQuaternion::dotProduct(relinked.setupRotation(), setupRotation)) > 0.9999f
                   && relinked.workOffset() == QStringLiteral("G55"),
               "changed source replacement should preserve the confirmed Setup")) {
        return 1;
    }

    const QString corruptPath =
        QDir::temp().filePath(QStringLiteral("cnext_project_manager_corrupt_test.cnext"));
    QFile::remove(corruptPath);
    {
        QFile corruptFile(corruptPath);
        if (expect(corruptFile.open(QIODevice::WriteOnly), "corrupt project fixture should open")) {
            return 1;
        }
        corruptFile.write("{not-json");
    }
    if (expect(!loaded.loadFromFile(corruptPath), "corrupt project should fail to load")) {
        return 1;
    }
    if (expect(loaded.sourceFilePath() == QStringLiteral("sample.stp")
                   && loaded.sourceFileFingerprint() == QStringLiteral("sha256:test-source")
                   && loaded.operations().size() == 1,
               "failed load should not mutate the active project")) {
        return 1;
    }

    const QString invalidStructurePath =
        QDir::temp().filePath(QStringLiteral("cnext_project_manager_invalid_structure_test.cnext"));
    QFile::remove(invalidStructurePath);
    {
        QFile invalidFile(invalidStructurePath);
        if (expect(invalidFile.open(QIODevice::WriteOnly), "invalid project fixture should open")) {
            return 1;
        }
        invalidFile.write("{}");
    }
    if (expect(!loaded.loadFromFile(invalidStructurePath),
               "JSON without the project structure should fail to load")) {
        return 1;
    }
    if (expect(loaded.sourceFilePath() == QStringLiteral("sample.stp")
                   && loaded.operations().size() == 1,
               "invalid project structure should not mutate the active project")) {
        return 1;
    }

    const QString legacyPath =
        QDir::temp().filePath(QStringLiteral("cnext_project_manager_legacy_test.cnext"));
    QFile::remove(legacyPath);
    if (expect(writeLegacyProject(legacyPath), "legacy project fixture should be written")) {
        return 1;
    }

    ProjectManager legacy;
    if (expect(legacy.loadFromFile(legacyPath), "legacy project should load")) {
        return 1;
    }
    const ProgramEntry legacyProgram = legacy.programById(QStringLiteral("program-legacy"));
    if (expect(legacyProgram.id == QStringLiteral("program-legacy"), "legacy program should load")) {
        return 1;
    }
    if (expect(legacyProgram.sourceOperationFingerprint.isEmpty(),
               "legacy project missing fingerprint should remain compatible")) {
        return 1;
    }
    if (expect(legacyProgram.mainProgramFileName.isEmpty() &&
                   legacyProgram.packageFiles.isEmpty(),
               "legacy single-file program should load without a package")) {
        return 1;
    }
    if (expect(legacy.sourceFileFingerprint().isEmpty(),
               "legacy project missing source fingerprint should remain compatible")) {
        return 1;
    }
    if (expect(legacy.setupRotation().isIdentity()
                   && legacy.activeRegion() == FaceRegion::Unknown
                   && legacy.workOffset() == QStringLiteral("G54")
                   && !legacy.setupOrigin().confirmed
                   && !legacy.stockDefinition().confirmed,
               "legacy project should receive safe Setup defaults")) {
        return 1;
    }
    if (expect(legacy.machineProfile().id == QStringLiteral("siemens-840d-default")
                   && !legacy.machineProfile().safeStartBlocks.isEmpty(),
               "legacy project should receive the default Siemens Machine Profile")) {
        return 1;
    }

    QFile::remove(projectPath);
    QFile::remove(legacyPath);
    QFile::remove(corruptPath);
    QFile::remove(invalidStructurePath);
    return 0;
}
