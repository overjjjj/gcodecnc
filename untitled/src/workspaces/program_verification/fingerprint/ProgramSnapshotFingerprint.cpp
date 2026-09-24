#include "ProgramSnapshotFingerprint.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// 摘要使用稳定文本标签，不依赖枚举在内存中的数值。
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

// 保留边界点顺序；方向或起点变化应反映到工序摘要中。
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

// 将工序参数、几何及选择证据编码进摘要；缺失工序保留显式 missing 标记，不能静默略过。
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
    if (operation->strategyId == QStringLiteral("mill_slot") ||
        operation->strategyId == QStringLiteral("mill_blind_slot") ||
        operation->strategyId == QStringLiteral("mill_tapered_slot")) {
        obj[QStringLiteral("implementationRevision")] = QStringLiteral("slot-b2v-1");
    }
    obj[QStringLiteral("toolId")] = operation->toolId;
    const SelectionChain &selection = operation->selectionEvidence;
    obj[QStringLiteral("selectionId")] = selection.id;
    obj[QStringLiteral("selectionSourceFingerprint")] = selection.sourceFingerprint;
    obj[QStringLiteral("selectionSetupFingerprint")] = selection.setupFingerprint;
    obj[QStringLiteral("selectionCoordinateSystem")] = selection.coordinateSystemId;
    obj[QStringLiteral("selectionGeometryIds")] = QJsonArray::fromStringList(
        selection.orderedGeometryIds);
    obj[QStringLiteral("selectionNormal")] = QStringLiteral("%1,%2,%3")
        .arg(selection.selectedSurfaceNormal.x(), 0, 'g', 9)
        .arg(selection.selectedSurfaceNormal.y(), 0, 'g', 9)
        .arg(selection.selectedSurfaceNormal.z(), 0, 'g', 9);
    obj[QStringLiteral("selectionToolAxis")] = QStringLiteral("%1,%2,%3")
        .arg(selection.toolAxis.x(), 0, 'g', 9)
        .arg(selection.toolAxis.y(), 0, 'g', 9)
        .arg(selection.toolAxis.z(), 0, 'g', 9);
    obj[QStringLiteral("selectionClosed")] = selection.closed;
    obj[QStringLiteral("selectionOuterLoopPointCount")] = selection.outerLoopPointCount;
    obj[QStringLiteral("selectionIslandCount")] = selection.islandCount;
    obj[QStringLiteral("selectionFreeEndCount")] = selection.freeEndCount;
    obj[QStringLiteral("selectionExplicit")] = selection.explicitUserSelection;

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

// 使用刀具几何和加工资料，不包含显示名称，单纯改名不会使程序过期。
QString ProgramSnapshotFingerprint::toolFingerprint(const ToolEntry &tool)
{
    if (tool.id <= 0 || tool.diameter <= 0.0) return QString();
    QJsonObject obj;
    obj["id"] = tool.id;
    obj["type"] = tool.type;
    obj["diameter"] = tool.diameter;
    obj["fluteLen"] = tool.fluteLen;
    obj["totalLen"] = tool.totalLen;
    obj["shankDia"] = tool.shankDia;
    obj["flutes"] = tool.flutes;
    obj["pointAngle"] = tool.pointAngle;
    obj["pitch"] = tool.pitch;
    obj["material"] = tool.material;
    obj["coating"] = tool.coating;
    obj["modelPath"] = tool.modelPath;
    obj["extra"] = QJsonObject::fromVariantMap(tool.extra);
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(obj).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

// 按刀具 ID 去重；任一源工序或刀具无法核对时返回空值，交给快照状态检查阻断。
// 此摘要不表达工序顺序，顺序由工序摘要及 ProgramSnapshotStatus 单独检查。
QString ProgramSnapshotFingerprint::calculateTools(
    const QList<MachiningOperation> &operations, const QStringList &sourceOperationIds,
    const std::function<ToolEntry(int)> &toolLookup)
{
    if (sourceOperationIds.isEmpty() || !toolLookup) return QString();
    QJsonObject tools;
    for (const QString &id : sourceOperationIds) {
        const MachiningOperation *operation = findOperation(operations, id);
        if (!operation) return QString();
        const ToolEntry tool = toolLookup(operation->toolId);
        const QString fingerprint = toolFingerprint(tool);
        if (tool.id != operation->toolId || fingerprint.isEmpty()) return QString();
        tools[QString::number(tool.id)] = fingerprint;
    }
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(tools).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

// 按来源 ID 的既定顺序序列化并散列，因此工序内容变化和顺序变化都会改变结果。
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
