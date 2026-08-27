#include "ProgramSnapshotFingerprint.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

static QString operationTypeId(OperationType type)
{
    switch (type) {
    case OperationType::Roughing:   return QStringLiteral("roughing");
    case OperationType::SemiFinish: return QStringLiteral("semi_finish");
    case OperationType::Finish:     return QStringLiteral("finish");
    case OperationType::Hole:       return QStringLiteral("hole");
    case OperationType::Contour:    return QStringLiteral("contour");
    }
    return QStringLiteral("hole");
}

static QString operationStageId(OperationStage stage)
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

static QString faceRegionId(FaceRegion region)
{
    switch (region) {
    case FaceRegion::Front:   return QStringLiteral("front");
    case FaceRegion::Side:    return QStringLiteral("side");
    case FaceRegion::Back:    return QStringLiteral("back");
    case FaceRegion::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

static QJsonArray pointArray(const QVector3D &point)
{
    return QJsonArray{point.x(), point.y(), point.z()};
}

static QJsonArray pointLoopArray(const QVector<QVector3D> &loop)
{
    QJsonArray array;
    for (const QVector3D &point : loop) {
        array.append(pointArray(point));
    }
    return array;
}

static QJsonArray pointLoopsArray(const QVector<QVector<QVector3D>> &loops)
{
    QJsonArray array;
    for (const QVector<QVector3D> &loop : loops) {
        array.append(pointLoopArray(loop));
    }
    return array;
}

static QJsonObject serializeFeature(const MachiningFeature &feature)
{
    QJsonObject obj;
    obj[QStringLiteral("subType")] = feature.subType;
    obj[QStringLiteral("radius")] = feature.radius;
    obj[QStringLiteral("depth")] = feature.depth;
    obj[QStringLiteral("width")] = feature.width;
    obj[QStringLiteral("length")] = feature.length;
    obj[QStringLiteral("secondaryRadius")] = feature.secondaryRadius;
    obj[QStringLiteral("pitch")] = feature.pitch;
    obj[QStringLiteral("center")] = pointArray(feature.center);
    obj[QStringLiteral("axis")] = pointArray(feature.axis);
    obj[QStringLiteral("coneAngle")] = feature.coneAngle;
    obj[QStringLiteral("angle")] = feature.angle;
    obj[QStringLiteral("region")] = faceRegionId(feature.region);
    obj[QStringLiteral("boundaryPoints")] = pointLoopArray(feature.boundaryPoints);
    obj[QStringLiteral("islandBoundaries")] = pointLoopsArray(feature.islandBoundaries);
    return obj;
}

static QJsonObject serializeContourFeature(const ContourFeature &feature)
{
    QJsonObject obj;
    obj[QStringLiteral("subType")] = feature.subType;
    obj[QStringLiteral("center")] = pointArray(feature.center);
    obj[QStringLiteral("radius")] = feature.radius;
    obj[QStringLiteral("depth")] = feature.depth;
    obj[QStringLiteral("width")] = feature.width;
    obj[QStringLiteral("length")] = feature.length;
    obj[QStringLiteral("angle")] = feature.angle;
    obj[QStringLiteral("openSide")] = feature.openSide;
    obj[QStringLiteral("slopeStartLength")] = feature.slopeStartLength;
    obj[QStringLiteral("slopeEndLength")] = feature.slopeEndLength;
    obj[QStringLiteral("slopeMinWidth")] = feature.slopeMinWidth;
    obj[QStringLiteral("slopeMaxWidth")] = feature.slopeMaxWidth;
    obj[QStringLiteral("axis")] = pointArray(feature.axis);
    obj[QStringLiteral("region")] = faceRegionId(feature.region);

    QJsonArray points;
    for (const QVector3D &point : feature.points) {
        points.append(pointArray(point));
    }
    obj[QStringLiteral("points")] = points;
    obj[QStringLiteral("islands")] = pointLoopsArray(feature.islands);
    return obj;
}

static QJsonObject serializeOperation(const MachiningOperation *operation,
                                      const QString &sourceOperationId)
{
    QJsonObject obj;
    obj[QStringLiteral("sourceOperationId")] = sourceOperationId;
    if (!operation) {
        obj[QStringLiteral("missing")] = true;
        return obj;
    }

    obj[QStringLiteral("id")] = operation->id;
    obj[QStringLiteral("opType")] = operationTypeId(operation->opType);
    obj[QStringLiteral("stage")] = operationStageId(operation->stage);
    obj[QStringLiteral("featureRef")] = operation->featureRef;
    obj[QStringLiteral("strategyId")] = operation->strategyId;
    obj[QStringLiteral("toolId")] = operation->toolId;
    QJsonArray dependencies;
    for (const QString &dependency : operation->dependencyOperationIds) {
        dependencies.append(dependency);
    }
    obj[QStringLiteral("dependencyOperationIds")] = dependencies;

    QJsonObject params;
    for (auto it = operation->params.values.cbegin(); it != operation->params.values.cend(); ++it) {
        params[it.key()] = QJsonValue::fromVariant(it.value());
    }
    obj[QStringLiteral("params")] = params;

    if (operation->opType == OperationType::Hole) {
        obj[QStringLiteral("holeFeature")] = serializeFeature(operation->holeFeature);
    } else {
        obj[QStringLiteral("contourFeature")] = serializeContourFeature(operation->contourFeature);
    }
    return obj;
}

static const MachiningOperation *findOperation(const QList<MachiningOperation> &operations,
                                               const QString &operationId)
{
    for (const MachiningOperation &operation : operations) {
        if (operation.id == operationId) {
            return &operation;
        }
    }
    return nullptr;
}

} // namespace

QString ProgramSnapshotFingerprint::calculate(const QList<MachiningOperation> &operations,
                                               const QStringList &sourceOperationIds)
{
    if (sourceOperationIds.isEmpty()) {
        return QString();
    }

    QJsonArray sources;
    for (const QString &sourceOperationId : sourceOperationIds) {
        sources.append(serializeOperation(findOperation(operations, sourceOperationId), sourceOperationId));
    }

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("sources")] = sources;
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}
