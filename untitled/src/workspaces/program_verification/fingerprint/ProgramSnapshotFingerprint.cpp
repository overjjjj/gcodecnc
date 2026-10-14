#include "ProgramSnapshotFingerprint.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// 函数说明：operationTypeId负责当前模块的核心处理。
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

// 函数说明：operationStageId负责当前模块的核心处理。
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

// 函数说明：faceRegionId负责当前模块的核心处理。
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

// 函数说明：pointArray负责当前模块的核心处理。
static QJsonArray pointArray(const Coordinate3D &point)
{
    return QJsonArray{point.x(), point.y(), point.z()};
}

// 函数说明：pointLoopArray负责当前模块的核心处理。
static QJsonArray pointLoopArray(const QVector<QVector3D> &loop)
{
    QJsonArray array;
    for (const QVector3D &point : loop) {
        array.append(pointArray(point));
    }
    return array;
}

// 函数说明：pointLoopsArray负责当前模块的核心处理。
static QJsonArray pointLoopsArray(const QVector<QVector<QVector3D>> &loops)
{
    QJsonArray array;
    for (const QVector<QVector3D> &loop : loops) {
        array.append(pointLoopArray(loop));
    }
    return array;
}

// 函数说明：serializeFeature负责当前模块的核心处理。
static QJsonObject serializeFeature(const MachiningFeature &feature)
{
    QJsonObject obj;
    obj[QStringLiteral("coordinatePrecisionVersion")] = feature.coordinatePrecisionVersion;
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
    if (feature.planarBoundaryVerified) obj[QStringLiteral("planarBoundaryVerified")] = true;
    obj[QStringLiteral("islandBoundaries")] = pointLoopsArray(feature.islandBoundaries);
    return obj;
}

// 函数说明：serializeContourFeature负责当前模块的核心处理。
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
    if (feature.planarBoundaryVerified) obj[QStringLiteral("planarBoundaryVerified")] = true;
    return obj;
}

// 函数说明：serializeOperation负责当前模块的核心处理。
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
    const SelectionChain &selection = operation->selectionEvidence;
    obj[QStringLiteral("selectionId")] = selection.id;
    obj[QStringLiteral("selectionSourceFingerprint")] = selection.sourceFingerprint;
    if (!selection.candidateGeometryId.isEmpty())
        obj[QStringLiteral("selectionCandidateGeometryId")] = selection.candidateGeometryId;
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
    obj[QStringLiteral("selectionEvidenceVersion")] = selection.evidenceVersion;
    obj[QStringLiteral("selectionReversed")] = selection.reversed;
    obj[QStringLiteral("selectionHasStartPoint")] = selection.hasStartPoint;
    if (selection.hasStartPoint) obj[QStringLiteral("selectionStartPoint")] = pointArray(selection.startPoint);
    QJsonArray refs;
    for (const auto &ref : selection.orderedTopologyRefs)
        refs.append(QJsonObject{{"id", ref.id}, {"kind", int(ref.kind)}, {"forward", ref.forward}});
    obj[QStringLiteral("selectionTopologyRefs")] = refs;
    auto anchorObject = [](const SelectionAnchor &anchor, bool present) {
        QJsonObject value{{"present", present}};
        if (present) {
            value["id"] = anchor.topologyId;
            value["point"] = pointArray(anchor.point);
            value["hasCurveParameter"] = anchor.hasCurveParameter;
            if (anchor.hasCurveParameter) value["curveParameter"] = anchor.curveParameter;
        }
        return value;
    };
    obj[QStringLiteral("selectionStartAnchor")] = anchorObject(selection.startAnchor, selection.hasStartAnchor);
    obj[QStringLiteral("selectionEndAnchor")] = anchorObject(selection.endAnchor, selection.hasEndAnchor);
    obj[QStringLiteral("selectionClosed")] = selection.closed;
    obj[QStringLiteral("selectionMachiningSide")] = int(selection.machiningSide);
    obj[QStringLiteral("selectionOuterLoopPointCount")] = selection.outerLoopPointCount;
    obj[QStringLiteral("selectionIslandCount")] = selection.islandCount;
    obj[QStringLiteral("selectionFreeEndCount")] = selection.freeEndCount;
    obj[QStringLiteral("selectionExplicit")] = selection.explicitUserSelection;
    QJsonArray openings;
    for (const auto &loop : selection.boundaryLoops) {
        QJsonObject proof;
        proof["machiningRole"] = int(loop.machiningRole);
        proof["faceId"] = loop.faceId;proof["sourceOuter"] = loop.outer;
        proof["edgeIds"] = QJsonArray::fromStringList(loop.orderedEdgeIds);
        QJsonArray directions;for (bool forward : loop.orderedForward) directions.append(forward);
        proof["directions"] = directions;
        proof["boundary"] = pointLoopArray(loop.machiningBoundary);
        proof["maximumDepth"] = loop.maximumDepth;
        proof["boundaryTolerance"] = loop.boundaryTolerance;
        proof["discretizationRule"]=loop.discretizationRule;proof["candidateRule"]=loop.candidateRule;
        proof["volumeProofRule"]=loop.volumeProofRule;proof["sourceBoundaryBound"]=loop.sourceBoundaryBound;
        proof["consumedBoundaryBound"]=loop.consumedBoundaryBound;proof["volumeProofBudget"]=loop.volumeProofBudget;
        proof["maximumEscapeBudget"]=loop.maximumEscapeBudget;
        QJsonArray curves;
        for(const auto &curve:loop.curves){QJsonObject c;c["circular"]=curve.circular;c["start"]=pointLoopArray({curve.start});c["end"]=pointLoopArray({curve.end});c["center"]=pointLoopArray({curve.center});c["sweep"]=curve.sweep;c["type"]=curve.type;c["firstParameter"]=curve.firstParameter;c["lastParameter"]=curve.lastParameter;c["approximationBound"]=curve.approximationBound;curves.append(c);}
        proof["curves"]=curves;
        proof["supportFaceIds"] = QJsonArray::fromStringList(loop.supportFaceIds);
        openings.append(proof);
    }
    if (!openings.isEmpty()) obj["selectionMachiningOpenings"] = openings;

    QJsonObject params;
    for (auto it = operation->params.values.cbegin(); it != operation->params.values.cend(); ++it) {
        params[it.key()] = QJsonValue::fromVariant(it.value());
    }
    obj[QStringLiteral("params")] = params;

    obj[QStringLiteral("coordinateRuleVersion")] = 1;
    if (operation->opType == OperationType::Hole) {
        obj[QStringLiteral("holeAxisRuleVersion")] = 1;
        obj[QStringLiteral("holeFeature")] = serializeFeature(operation->holeFeature);
    } else {
        obj[QStringLiteral("contourFeature")] = serializeContourFeature(operation->contourFeature);
    }
    return obj;
}

// 函数说明：findOperation负责当前模块的核心处理。
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

// 函数说明：calculate负责当前模块的核心处理。
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
    root[QStringLiteral("version")] = 2; // Model inputs now generate in resolved work coordinates.
    root[QStringLiteral("sources")] = sources;
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}
