#include "ProjectManager.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QUuid>
#include <QVector3D>
#include <QVariant>

ProjectManager::ProjectManager(QObject *parent)
    : QObject(parent)
{}

static bool containsProgramId(const QList<ProgramEntry> &programs, const QString &programId)
{
    if (programId.trimmed().isEmpty()) {
        return false;
    }
    for (const ProgramEntry &program : programs) {
        if (program.id == programId) {
            return true;
        }
    }
    return false;
}

static void ensureOperationIds(QList<MachiningOperation> &operations)
{
    for (MachiningOperation &op : operations) {
        if (op.id.trimmed().isEmpty()) {
            op.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
    }
}

static MachineProfile normalizedMachineProfile(MachineProfile profile)
{
    const MachineProfile defaults;
    if (profile.id.trimmed().isEmpty()) profile.id = defaults.id;
    if (profile.version.trimmed().isEmpty()) profile.version = defaults.version;
    if (profile.controllerId.trimmed().isEmpty()) profile.controllerId = defaults.controllerId;
    if (profile.safeStartBlocks.isEmpty()) profile.safeStartBlocks = defaults.safeStartBlocks;
    return profile;
}

void ProjectManager::setMesh(const MeshData &mesh)
{
    m_mesh = mesh;
    m_modified = true;
    emit projectChanged();
}

void ProjectManager::setLoadedMesh(const MeshData &mesh)
{
    m_mesh = mesh;
    emit projectChanged();
}

static QString featureKindId(FeatureKind kind)
{
    switch (kind) {
    case FeatureKind::Hole:        return QStringLiteral("hole");
    case FeatureKind::Slot:        return QStringLiteral("slot");
    case FeatureKind::Pocket:      return QStringLiteral("pocket");
    case FeatureKind::Boss:        return QStringLiteral("boss");
    case FeatureKind::Chamfer:     return QStringLiteral("chamfer");
    case FeatureKind::Fillet:      return QStringLiteral("fillet");
    case FeatureKind::FlatSurface: return QStringLiteral("flat_surface");
    case FeatureKind::Thread:      return QStringLiteral("thread");
    }
    return QStringLiteral("hole");
}

static FeatureKind featureKindFromId(const QString &id)
{
    if (id == QStringLiteral("slot")) return FeatureKind::Slot;
    if (id == QStringLiteral("pocket")) return FeatureKind::Pocket;
    if (id == QStringLiteral("boss")) return FeatureKind::Boss;
    if (id == QStringLiteral("chamfer")) return FeatureKind::Chamfer;
    if (id == QStringLiteral("fillet")) return FeatureKind::Fillet;
    if (id == QStringLiteral("flat_surface")) return FeatureKind::FlatSurface;
    if (id == QStringLiteral("thread")) return FeatureKind::Thread;
    return FeatureKind::Hole;
}

static FaceRegion faceRegionFromId(const QString &id)
{
    if (id == QStringLiteral("front")) return FaceRegion::Front;
    if (id == QStringLiteral("side")) return FaceRegion::Side;
    if (id == QStringLiteral("back")) return FaceRegion::Back;
    return FaceRegion::Unknown;
}

void ProjectManager::setFeatures(const QVector<MachiningFeature> &features)
{
    m_features = features;
    m_modified = true;
    emit projectChanged();
}

void ProjectManager::setOperations(const QList<MachiningOperation> &operations)
{
    m_operations = operations;
    ensureOperationIds(m_operations);
    m_modified = true;
    emit operationsChanged();
}

void ProjectManager::setPrograms(const QList<ProgramEntry> &programs)
{
    m_programs = programs;
    if (m_programs.isEmpty()) {
        m_currentProgramId.clear();
    } else if (!containsProgramId(m_programs, m_currentProgramId)) {
        m_currentProgramId = m_programs.last().id;
    }
    m_modified = true;
    emit programsChanged();
}

void ProjectManager::setCurrentProgramId(const QString &programId)
{
    QString normalizedId = programId.trimmed();
    if (!containsProgramId(m_programs, normalizedId)) {
        normalizedId.clear();
    }
    if (normalizedId == m_currentProgramId) {
        return;
    }
    m_currentProgramId = normalizedId;
    m_modified = true;
}

void ProjectManager::setSourceFilePath(const QString &path)
{
    m_sourceFilePath = path;
    m_modified = true;
}

void ProjectManager::setSourceFileFingerprint(const QString &fingerprint)
{
    m_sourceFileFingerprint = fingerprint;
    m_modified = true;
}

void ProjectManager::setSetupRotation(const QQuaternion &rotation)
{
    m_setupRotation = rotation.isNull() ? QQuaternion() : rotation.normalized();
    m_modified = true;
}

void ProjectManager::setActiveRegion(FaceRegion region)
{
    m_activeRegion = region;
    m_modified = true;
}

void ProjectManager::setWorkOffset(const QString &workOffset)
{
    const QString normalized = workOffset.trimmed().toUpper();
    static const QStringList allowed = {
        QStringLiteral("G54"), QStringLiteral("G55"), QStringLiteral("G56"),
        QStringLiteral("G57"), QStringLiteral("G58"), QStringLiteral("G59")
    };
    m_workOffset = allowed.contains(normalized) ? normalized : QStringLiteral("G54");
    m_modified = true;
}

void ProjectManager::setSetupOrigin(const SetupOrigin &origin)
{
    m_setupOrigin = origin;
    m_modified = true;
    emit projectChanged();
}

void ProjectManager::setStockDefinition(const StockDefinition &stock)
{
    m_stockDefinition = stock;
    m_stockDefinition.normalize();
    m_modified = true;
    emit projectChanged();
}

QString ProjectManager::setupFingerprint() const
{
    const QString originFingerprint = m_setupOrigin.fingerprint(m_workOffset);
    const QString stockFingerprint = m_stockDefinition.fingerprint();
    if (originFingerprint.isEmpty() || stockFingerprint.isEmpty()) {
        return QString();
    }
    const QByteArray payload = originFingerprint.toLatin1() + '|'
                             + stockFingerprint.toLatin1();
    return QString::fromLatin1(QCryptographicHash::hash(payload,
                                                        QCryptographicHash::Sha256).toHex());
}

void ProjectManager::replaceChangedSource(const MeshData &mesh,
                                          const QVector<MachiningFeature> &features,
                                          const QString &sourceFilePath,
                                          const QString &sourceFileFingerprint)
{
    m_mesh = mesh;
    m_features = features;
    m_sourceFilePath = sourceFilePath;
    m_sourceFileFingerprint = sourceFileFingerprint;
    m_operations.clear();
    m_programs.clear();
    m_currentProgramId.clear();
    m_modified = true;
    emit projectChanged();
    emit operationsChanged();
    emit programsChanged();
}

void ProjectManager::setMachineProfile(const MachineProfile &profile)
{
    m_machineProfile = normalizedMachineProfile(profile);
    m_modified = true;
}

QString ProjectManager::upsertProgram(ProgramEntry program)
{
    if (program.id.trimmed().isEmpty()) {
        program.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (!program.createdAt.isValid()) {
        program.createdAt = QDateTime::currentDateTime();
    }
    if (program.machineProfileId.trimmed().isEmpty()) {
        program.machineProfileId = m_machineProfile.id;
    }
    if (program.machineProfileVersion.trimmed().isEmpty()) {
        program.machineProfileVersion = m_machineProfile.version;
    }
    if (program.safeStartBlocks.isEmpty()) {
        program.safeStartBlocks = m_machineProfile.safeStartBlocks;
    }
    program.lineCount = program.gcodeText.isEmpty() ? 0 : program.gcodeText.count(QLatin1Char('\n')) + 1;

    for (int i = 0; i < m_programs.size(); ++i) {
        if (m_programs[i].id == program.id) {
            m_programs[i] = program;
            if (!containsProgramId(m_programs, m_currentProgramId)) {
                m_currentProgramId = program.id;
            }
            m_modified = true;
            emit programsChanged();
            return program.id;
        }
    }

    m_programs.append(program);
    if (m_currentProgramId.trimmed().isEmpty()) {
        m_currentProgramId = program.id;
    }
    m_modified = true;
    emit programsChanged();
    return program.id;
}

ProgramEntry ProjectManager::programById(const QString &id) const
{
    for (const ProgramEntry &program : m_programs) {
        if (program.id == id) {
            return program;
        }
    }
    return {};
}

// ---------- serialization helpers ----------

static QJsonArray serializePointLoop(const QVector<QVector3D> &points)
{
    QJsonArray array;
    for (const QVector3D &point : points) {
        QJsonObject object;
        object[QStringLiteral("x")] = point.x();
        object[QStringLiteral("y")] = point.y();
        object[QStringLiteral("z")] = point.z();
        array.append(object);
    }
    return array;
}

static QVector<QVector3D> deserializePointLoop(const QJsonArray &array)
{
    QVector<QVector3D> points;
    points.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        points.append(QVector3D(float(object[QStringLiteral("x")].toDouble()),
                                float(object[QStringLiteral("y")].toDouble()),
                                float(object[QStringLiteral("z")].toDouble())));
    }
    return points;
}

static QJsonArray serializePointLoops(const QVector<QVector<QVector3D>> &loops)
{
    QJsonArray array;
    for (const QVector<QVector3D> &loop : loops) {
        array.append(serializePointLoop(loop));
    }
    return array;
}

static QVector<QVector<QVector3D>> deserializePointLoops(const QJsonArray &array)
{
    QVector<QVector<QVector3D>> loops;
    loops.reserve(array.size());
    for (const QJsonValue &value : array) {
        loops.append(deserializePointLoop(value.toArray()));
    }
    return loops;
}

static QJsonObject serializeHoleFeature(const HoleFeature &f)
{
    QJsonObject o;
    o["kind"]    = featureKindId(f.kind);
    o["type"]    = f.subType;
    o["radius"]  = f.radius;
    o["depth"]   = f.depth;
    o["width"]   = f.width;
    o["length"]  = f.length;
    o["secR"]    = f.secondaryRadius;
    o["pitch"]   = f.pitch;
    o["cx"]      = f.center.x();
    o["cy"]      = f.center.y();
    o["cz"]      = f.center.z();
    o["ax"]      = f.axis.x();
    o["ay"]      = f.axis.y();
    o["az"]      = f.axis.z();
    o["cone"]    = f.coneAngle;
    o["angle"]   = f.angle;
    o["region"]  = faceRegionName(f.region);
    QJsonArray faceIndices;
    for (int faceIndex : f.faceIndices) {
        faceIndices.append(faceIndex);
    }
    o["faceIndices"] = faceIndices;
    return o;
}

static HoleFeature deserializeHoleFeature(const QJsonObject &o)
{
    HoleFeature f;
    f.kind            = featureKindFromId(o["kind"].toString(QStringLiteral("hole")));
    f.subType         = o["type"].toString();
    f.radius          = o["radius"].toDouble();
    f.depth           = o["depth"].toDouble();
    f.width           = o["width"].toDouble();
    f.length          = o["length"].toDouble();
    f.secondaryRadius = o["secR"].toDouble();
    f.pitch           = o["pitch"].toDouble();
    f.center          = QVector3D(o["cx"].toDouble(), o["cy"].toDouble(), o["cz"].toDouble());
    f.axis            = QVector3D(float(o["ax"].toDouble(0.0)),
                                  float(o["ay"].toDouble(0.0)),
                                  float(o["az"].toDouble(1.0)));
    f.coneAngle       = o["cone"].toDouble();
    f.angle           = o["angle"].toDouble();
    f.region          = faceRegionFromId(o["region"].toString());
    for (const QJsonValue &value : o["faceIndices"].toArray()) {
        f.faceIndices.append(value.toInt());
    }
    return f;
}

static QJsonObject serializeContourFeature(const ContourFeature &f)
{
    QJsonObject o;
    o["type"]   = f.subType;
    o["cx"]     = f.center.x();
    o["cy"]     = f.center.y();
    o["cz"]     = f.center.z();
    o["radius"] = f.radius;
    o["depth"]  = f.depth;
    o["width"]  = f.width;
    o["length"] = f.length;
    o["angle"]  = f.angle;
    o["openSide"] = f.openSide;
    o["slopeStartLength"] = f.slopeStartLength;
    o["slopeEndLength"] = f.slopeEndLength;
    o["slopeMinWidth"] = f.slopeMinWidth;
    o["slopeMaxWidth"] = f.slopeMaxWidth;
    o["ax"]     = f.axis.x();
    o["ay"]     = f.axis.y();
    o["az"]     = f.axis.z();
    o["region"] = faceRegionName(f.region);
    o["points"] = serializePointLoop(f.points);
    o["islands"] = serializePointLoops(f.islands);
    return o;
}

static ContourFeature deserializeContourFeature(const QJsonObject &o)
{
    ContourFeature f;
    f.subType = o["type"].toString();
    f.center  = QVector3D(o["cx"].toDouble(), o["cy"].toDouble(), o["cz"].toDouble());
    f.radius  = o["radius"].toDouble();
    f.depth   = o["depth"].toDouble();
    f.width   = o["width"].toDouble();
    f.length  = o["length"].toDouble();
    f.angle   = o["angle"].toDouble();
    f.openSide = o["openSide"].toDouble();
    f.slopeStartLength = o["slopeStartLength"].toDouble();
    f.slopeEndLength = o["slopeEndLength"].toDouble();
    f.slopeMinWidth = o["slopeMinWidth"].toDouble();
    f.slopeMaxWidth = o["slopeMaxWidth"].toDouble();
    f.axis    = QVector3D(float(o["ax"].toDouble(0.0)),
                          float(o["ay"].toDouble(0.0)),
                          float(o["az"].toDouble(1.0)));
    f.region  = faceRegionFromId(o["region"].toString());
    f.points = deserializePointLoop(o["points"].toArray());
    f.islands = deserializePointLoops(o["islands"].toArray());
    return f;
}

static QString operationTypeName(OperationType t)
{
    switch (t) {
    case OperationType::Roughing:   return QStringLiteral("roughing");
    case OperationType::SemiFinish: return QStringLiteral("semi_finish");
    case OperationType::Finish:     return QStringLiteral("finish");
    case OperationType::Hole:       return QStringLiteral("hole");
    case OperationType::Contour:    return QStringLiteral("contour");
    }
    return QStringLiteral("hole");
}

static OperationType operationTypeFromName(const QString &s)
{
    if (s == QStringLiteral("roughing"))    return OperationType::Roughing;
    if (s == QStringLiteral("semi_finish")) return OperationType::SemiFinish;
    if (s == QStringLiteral("finish"))      return OperationType::Finish;
    if (s == QStringLiteral("contour"))     return OperationType::Contour;
    return OperationType::Hole;
}

static QString operationStageName(OperationStage stage)
{
    switch (stage) {
    case OperationStage::Setup:     return QStringLiteral("setup");
    case OperationStage::RoughCut:  return QStringLiteral("rough_cut");
    case OperationStage::FinishCut: return QStringLiteral("finish_cut");
    case OperationStage::DeepHole:  return QStringLiteral("deep_hole");
    case OperationStage::Cleanup:   return QStringLiteral("cleanup");
    }
    return QStringLiteral("rough_cut");
}

static OperationStage operationStageFromName(const QString &s)
{
    if (s == QStringLiteral("setup"))      return OperationStage::Setup;
    if (s == QStringLiteral("finish_cut")) return OperationStage::FinishCut;
    if (s == QStringLiteral("deep_hole"))  return OperationStage::DeepHole;
    if (s == QStringLiteral("cleanup"))    return OperationStage::Cleanup;
    return OperationStage::RoughCut;
}

static QJsonObject serializeProgramEntry(const ProgramEntry &program)
{
    QJsonObject obj;
    obj["id"] = program.id;
    obj["name"] = program.name;
    obj["postProcessorId"] = program.postProcessorId;
    obj["sourceOperationFingerprint"] = program.sourceOperationFingerprint;
    obj["setupFingerprint"] = program.setupFingerprint;
    obj["machineProfileId"] = program.machineProfileId;
    obj["machineProfileVersion"] = program.machineProfileVersion;
    obj["sourceSummary"] = program.sourceSummary;
    obj["gcodeText"] = program.gcodeText;
    obj["macroText"] = program.macroText;
    obj["lineCount"] = program.lineCount;
    obj["createdAt"] = program.createdAt.toString(Qt::ISODate);
    QJsonArray sourceOps;
    for (const QString &sourceOpId : program.sourceOperationIds) {
        sourceOps.append(sourceOpId);
    }
    obj["sourceOperationIds"] = sourceOps;
    QJsonArray safeStartBlocks;
    for (const QString &block : program.safeStartBlocks) {
        safeStartBlocks.append(block);
    }
    obj["safeStartBlocks"] = safeStartBlocks;
    if (!program.mainProgramFileName.isEmpty() || !program.packageFiles.isEmpty()) {
        QJsonObject package;
        package["mainProgramFileName"] = program.mainProgramFileName;
        QJsonArray files;
        for (const ProgramFileEntry &file : program.packageFiles) {
            QJsonObject fileObj;
            fileObj["kind"] = file.kind;
            fileObj["fileName"] = file.fileName;
            fileObj["content"] = file.content;
            fileObj["sha256"] = file.sha256;
            files.append(fileObj);
        }
        package["files"] = files;
        obj["programPackage"] = package;
    }
    return obj;
}

static ProgramEntry deserializeProgramEntry(const QJsonObject &obj)
{
    ProgramEntry program;
    program.id = obj["id"].toString();
    program.name = obj["name"].toString();
    program.postProcessorId = obj["postProcessorId"].toString();
    program.sourceOperationFingerprint = obj["sourceOperationFingerprint"].toString();
    program.setupFingerprint = obj["setupFingerprint"].toString();
    program.machineProfileId = obj["machineProfileId"].toString();
    program.machineProfileVersion = obj["machineProfileVersion"].toString();
    program.sourceSummary = obj["sourceSummary"].toString();
    program.gcodeText = obj["gcodeText"].toString();
    program.macroText = obj["macroText"].toString();
    program.lineCount = obj["lineCount"].toInt();
    program.createdAt = QDateTime::fromString(obj["createdAt"].toString(), Qt::ISODate);
    for (const QJsonValue &value : obj["sourceOperationIds"].toArray()) {
        program.sourceOperationIds.append(value.toString());
    }
    for (const QJsonValue &value : obj["safeStartBlocks"].toArray()) {
        program.safeStartBlocks.append(value.toString());
    }
    const QJsonObject package = obj["programPackage"].toObject();
    program.mainProgramFileName = package["mainProgramFileName"].toString();
    for (const QJsonValue &value : package["files"].toArray()) {
        const QJsonObject fileObj = value.toObject();
        ProgramFileEntry file;
        file.kind = fileObj["kind"].toString();
        file.fileName = fileObj["fileName"].toString();
        file.content = fileObj["content"].toString();
        file.sha256 = fileObj["sha256"].toString();
        program.packageFiles.append(file);
    }
    return program;
}

static bool hasValidProjectStructure(const QJsonObject &root)
{
    return root.value(QStringLiteral("version")).isString()
        && root.value(QStringLiteral("sourceFilePath")).isString()
        && root.value(QStringLiteral("features")).isArray()
        && root.value(QStringLiteral("operations")).isArray()
        && root.value(QStringLiteral("programs")).isArray();
}

bool ProjectManager::saveToFile(const QString &path)
{
    QJsonObject root;
    root["version"] = "2.0";
    root["sourceFilePath"] = m_sourceFilePath;
    root["sourceFileFingerprint"] = m_sourceFileFingerprint;
    root["currentProgramId"] = m_currentProgramId;
    QJsonObject setup;
    setup["rotationX"] = m_setupRotation.x();
    setup["rotationY"] = m_setupRotation.y();
    setup["rotationZ"] = m_setupRotation.z();
    setup["rotationW"] = m_setupRotation.scalar();
    setup["activeRegion"] = faceRegionName(m_activeRegion);
    setup["workOffset"] = m_workOffset;
    setup["originAnchor"] = setupOriginAnchorId(m_setupOrigin.anchor);
    setup["originCustomX"] = m_setupOrigin.customPoint.x();
    setup["originCustomY"] = m_setupOrigin.customPoint.y();
    setup["originCustomZ"] = m_setupOrigin.customPoint.z();
    setup["originOffsetX"] = m_setupOrigin.offset.x();
    setup["originOffsetY"] = m_setupOrigin.offset.y();
    setup["originOffsetZ"] = m_setupOrigin.offset.z();
    setup["originConfirmed"] = m_setupOrigin.confirmed;
    root["setup"] = setup;
    QJsonObject stock;
    stock["type"] = QStringLiteral("rectangular");
    stock["minusX"] = m_stockDefinition.minusX;
    stock["plusX"] = m_stockDefinition.plusX;
    stock["minusY"] = m_stockDefinition.minusY;
    stock["plusY"] = m_stockDefinition.plusY;
    stock["minusZ"] = m_stockDefinition.minusZ;
    stock["plusZ"] = m_stockDefinition.plusZ;
    stock["confirmed"] = m_stockDefinition.confirmed;
    root["stock"] = stock;
    QJsonObject machineProfile;
    machineProfile["id"] = m_machineProfile.id;
    machineProfile["version"] = m_machineProfile.version;
    machineProfile["controllerId"] = m_machineProfile.controllerId;
    QJsonArray profileSafeStartBlocks;
    for (const QString &block : m_machineProfile.safeStartBlocks) {
        profileSafeStartBlocks.append(block);
    }
    machineProfile["safeStartBlocks"] = profileSafeStartBlocks;
    root["machineProfile"] = machineProfile;
    QJsonArray featureArr;
    for (const auto &f : m_features) {
        QJsonObject fo;
        fo["kind"]   = featureKindId(f.kind);
        fo["type"]   = f.subType;
        fo["radius"] = f.radius;
        fo["depth"]  = f.depth;
        fo["width"]  = f.width;
        fo["length"] = f.length;
        fo["secondaryRadius"] = f.secondaryRadius;
        fo["pitch"] = f.pitch;
        fo["cx"]     = f.center.x();
        fo["cy"]     = f.center.y();
        fo["cz"]     = f.center.z();
        fo["ax"]     = f.axis.x();
        fo["ay"]     = f.axis.y();
        fo["az"]     = f.axis.z();
        fo["coneAngle"] = f.coneAngle;
        fo["angle"] = f.angle;
        fo["region"] = faceRegionName(f.region);
        QJsonArray faceIndices;
        for (int faceIndex : f.faceIndices) {
            faceIndices.append(faceIndex);
        }
        fo["faceIndices"] = faceIndices;
        fo["boundaryPoints"] = serializePointLoop(f.boundaryPoints);
        fo["islandBoundaries"] = serializePointLoops(f.islandBoundaries);
        featureArr.append(fo);
    }
    root["features"] = featureArr;

    QJsonArray opArr;
    for (const MachiningOperation &op : m_operations) {
        QJsonObject oo;
        oo["id"]         = op.id;
        oo["opType"]     = operationTypeName(op.opType);
        oo["stage"]      = operationStageName(op.stage);
        oo["featureRef"] = op.featureRef;
        oo["strategyId"] = op.strategyId;
        oo["toolId"]     = op.toolId;

        QJsonObject paramsObj;
        for (auto it = op.params.values.cbegin(); it != op.params.values.cend(); ++it) {
            paramsObj[it.key()] = it.value().toDouble();
        }
        oo["params"] = paramsObj;

        if (op.opType == OperationType::Hole) {
            oo["holeFeature"] = serializeHoleFeature(op.holeFeature);
        } else {
            oo["contourFeature"] = serializeContourFeature(op.contourFeature);
        }
        opArr.append(oo);
    }
    root["operations"] = opArr;

    QJsonArray programArr;
    for (const ProgramEntry &program : m_programs) {
        programArr.append(serializeProgramEntry(program));
    }
    root["programs"] = programArr;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson());
    m_modified = false;
    return true;
}

bool ProjectManager::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return false;
    QJsonObject root = doc.object();
    if (!hasValidProjectStructure(root)) return false;
    m_sourceFilePath = root["sourceFilePath"].toString();
    m_sourceFileFingerprint = root["sourceFileFingerprint"].toString();
    m_currentProgramId = root["currentProgramId"].toString();
    const QJsonObject setup = root["setup"].toObject();
    if (setup.isEmpty()) {
        m_setupRotation = QQuaternion();
        m_activeRegion = FaceRegion::Unknown;
        m_workOffset = QStringLiteral("G54");
        m_setupOrigin = SetupOrigin();
    } else {
        m_setupRotation = QQuaternion(float(setup["rotationW"].toDouble(1.0)),
                                      float(setup["rotationX"].toDouble()),
                                      float(setup["rotationY"].toDouble()),
                                      float(setup["rotationZ"].toDouble())).normalized();
        m_activeRegion = faceRegionFromId(setup["activeRegion"].toString());
        const QString storedWorkOffset = setup["workOffset"].toString(QStringLiteral("G54"));
        static const QStringList allowedWorkOffsets = {
            QStringLiteral("G54"), QStringLiteral("G55"), QStringLiteral("G56"),
            QStringLiteral("G57"), QStringLiteral("G58"), QStringLiteral("G59")
        };
        m_workOffset = allowedWorkOffsets.contains(storedWorkOffset)
                           ? storedWorkOffset
                           : QStringLiteral("G54");
        m_setupOrigin.anchor = setupOriginAnchorFromId(
            setup["originAnchor"].toString(QStringLiteral("top_center")));
        m_setupOrigin.customPoint = QVector3D(float(setup["originCustomX"].toDouble()),
                                              float(setup["originCustomY"].toDouble()),
                                              float(setup["originCustomZ"].toDouble()));
        m_setupOrigin.offset = QVector3D(float(setup["originOffsetX"].toDouble()),
                                         float(setup["originOffsetY"].toDouble()),
                                         float(setup["originOffsetZ"].toDouble()));
        m_setupOrigin.confirmed = setup["originConfirmed"].toBool(false);
    }
    const QJsonObject stock = root["stock"].toObject();
    if (stock.isEmpty() || stock["type"].toString() != QStringLiteral("rectangular")) {
        m_stockDefinition = StockDefinition();
    } else {
        m_stockDefinition.minusX = stock["minusX"].toDouble();
        m_stockDefinition.plusX = stock["plusX"].toDouble();
        m_stockDefinition.minusY = stock["minusY"].toDouble();
        m_stockDefinition.plusY = stock["plusY"].toDouble();
        m_stockDefinition.minusZ = stock["minusZ"].toDouble();
        m_stockDefinition.plusZ = stock["plusZ"].toDouble();
        m_stockDefinition.confirmed = stock["confirmed"].toBool(false);
        m_stockDefinition.normalize();
    }
    const QJsonObject machineProfile = root["machineProfile"].toObject();
    if (machineProfile.isEmpty()) {
        m_machineProfile = MachineProfile();
    } else {
        MachineProfile loadedProfile;
        loadedProfile.id = machineProfile["id"].toString();
        loadedProfile.version = machineProfile["version"].toString();
        loadedProfile.controllerId = machineProfile["controllerId"].toString();
        loadedProfile.safeStartBlocks.clear();
        for (const QJsonValue &value : machineProfile["safeStartBlocks"].toArray()) {
            loadedProfile.safeStartBlocks.append(value.toString());
        }
        m_machineProfile = normalizedMachineProfile(loadedProfile);
    }
    m_features.clear();
    const QJsonArray featureArray = root["features"].toArray();
    for (const QJsonValue &v : featureArray) {
        QJsonObject fo = v.toObject();
        MachiningFeature f;
        f.kind   = featureKindFromId(fo["kind"].toString(QStringLiteral("hole")));
        f.subType = fo["type"].toString();
        f.radius = fo["radius"].toDouble();
        f.depth  = fo["depth"].toDouble();
        f.width  = fo["width"].toDouble();
        f.length = fo["length"].toDouble();
        f.secondaryRadius = fo["secondaryRadius"].toDouble();
        f.pitch = fo["pitch"].toDouble();
        f.center = QVector3D(fo["cx"].toDouble(), fo["cy"].toDouble(), fo["cz"].toDouble());
        f.axis   = QVector3D(float(fo["ax"].toDouble(0.0)),
                             float(fo["ay"].toDouble(0.0)),
                             float(fo["az"].toDouble(1.0)));
        f.coneAngle = fo["coneAngle"].toDouble();
        f.angle = fo["angle"].toDouble();
        f.region = faceRegionFromId(fo["region"].toString());
        for (const QJsonValue &faceIndex : fo["faceIndices"].toArray()) {
            f.faceIndices.append(faceIndex.toInt());
        }
        f.boundaryPoints = deserializePointLoop(fo["boundaryPoints"].toArray());
        f.islandBoundaries = deserializePointLoops(fo["islandBoundaries"].toArray());
        m_features.append(f);
    }

    m_operations.clear();
    const QJsonArray opArray = root["operations"].toArray();
    for (const QJsonValue &v : opArray) {
        QJsonObject oo = v.toObject();
        MachiningOperation op;
        op.id         = oo["id"].toString();
        op.opType     = operationTypeFromName(oo["opType"].toString());
        op.stage      = operationStageFromName(oo["stage"].toString(QStringLiteral("rough_cut")));
        op.featureRef = oo["featureRef"].toString();
        op.strategyId = oo["strategyId"].toString();
        op.toolId     = oo["toolId"].toInt(-1);

        const QJsonObject paramsObj = oo["params"].toObject();
        for (auto it = paramsObj.begin(); it != paramsObj.end(); ++it) {
            op.params.set(it.key(), it.value().toDouble());
        }

        if (op.opType == OperationType::Hole) {
            op.holeFeature = deserializeHoleFeature(oo["holeFeature"].toObject());
        } else {
            op.contourFeature = deserializeContourFeature(oo["contourFeature"].toObject());
        }
        m_operations.append(op);
    }
    ensureOperationIds(m_operations);

    m_programs.clear();
    const QJsonArray programArray = root["programs"].toArray();
    for (const QJsonValue &value : programArray) {
        m_programs.append(deserializeProgramEntry(value.toObject()));
    }
    if (!containsProgramId(m_programs, m_currentProgramId)) {
        m_currentProgramId = m_programs.isEmpty() ? QString() : m_programs.last().id;
    }

    m_modified = false;
    emit projectChanged();
    if (!m_operations.isEmpty()) {
        emit operationsChanged();
    }
    emit programsChanged();
    return true;
}
