#include "ProjectManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QVector3D>
#include <QVariant>

ProjectManager::ProjectManager(QObject *parent)
    : QObject(parent)
{}

void ProjectManager::setMesh(const MeshData &mesh)
{
    m_mesh = mesh;
    m_modified = true;
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
    m_modified = true;
    emit operationsChanged();
}

void ProjectManager::setSourceFilePath(const QString &path)
{
    m_sourceFilePath = path;
    m_modified = true;
}

// ---------- serialization helpers ----------

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
    o["region"]  = faceRegionName(f.region);
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
    f.region          = faceRegionFromId(o["region"].toString());
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
    QJsonArray pts;
    for (const QVector3D &p : f.points) {
        QJsonObject po;
        po["x"] = p.x(); po["y"] = p.y(); po["z"] = p.z();
        pts.append(po);
    }
    o["points"] = pts;
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
    for (const QJsonValue &v : o["points"].toArray()) {
        QJsonObject po = v.toObject();
        f.points.append(QVector3D(po["x"].toDouble(), po["y"].toDouble(), po["z"].toDouble()));
    }
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

bool ProjectManager::saveToFile(const QString &path)
{
    QJsonObject root;
    root["version"] = "1.2";
    root["sourceFilePath"] = m_sourceFilePath;
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
        fo["region"] = faceRegionName(f.region);
        featureArr.append(fo);
    }
    root["features"] = featureArr;

    QJsonArray opArr;
    for (const MachiningOperation &op : m_operations) {
        QJsonObject oo;
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
    m_sourceFilePath = root["sourceFilePath"].toString();
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
        f.region = faceRegionFromId(fo["region"].toString());
        m_features.append(f);
    }

    m_operations.clear();
    const QJsonArray opArray = root["operations"].toArray();
    for (const QJsonValue &v : opArray) {
        QJsonObject oo = v.toObject();
        MachiningOperation op;
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

    m_modified = false;
    emit projectChanged();
    if (!m_operations.isEmpty()) {
        emit operationsChanged();
    }
    return true;
}
