#include "ContourMachiningChoice.h"
#include "../../../strategies/mill/ContourSideGeometry.h"

#include "../../../import/StepImporter.h"

#include <algorithm>

namespace {

// 函数说明：boundedStartIndex负责当前模块的核心处理。
int boundedStartIndex(int index, int pointCount)
{
    return pointCount > 0 ? std::max(0, std::min(index, pointCount - 1)) : 0;
}

} // namespace

// 函数说明：isManualContourChoiceStrategy负责当前模块的核心处理。
bool isManualContourChoiceStrategy(const QString &strategyId)
{
    return strategyId == QStringLiteral("mill_closed_contour")
        || strategyId == QStringLiteral("mill_open_contour");
}

// 函数说明：canReverseContourDirection负责当前模块的核心处理。
bool canReverseContourDirection(const ContourFeature &feature)
{
    return feature.points.size() >= 2;
}

// 函数说明：contourPreviewPoints负责当前模块的核心处理。
QVector<QVector3D> contourPreviewPoints(const ContourFeature &feature)
{
    if (feature.points.size() >= 2) {
        return feature.points;
    }

    const double halfLength = feature.length > 0.0 ? feature.length * 0.5 : feature.radius;
    const double halfWidth = feature.width > 0.0 ? feature.width * 0.5 : feature.radius;
    if (halfLength <= 0.0 || halfWidth <= 0.0) {
        return {};
    }
    return {
        feature.center.toVector3D() + QVector3D(float(-halfLength), float(-halfWidth), 0.0f),
        feature.center.toVector3D() + QVector3D(float( halfLength), float(-halfWidth), 0.0f),
        feature.center.toVector3D() + QVector3D(float( halfLength), float( halfWidth), 0.0f),
        feature.center.toVector3D() + QVector3D(float(-halfLength), float( halfWidth), 0.0f)
    };
}

// 函数说明：applyContourMachiningChoice负责当前模块的核心处理。
void applyContourMachiningChoice(ContourFeature &feature,
                                 StrategyParams &params,
                                 const ContourMachiningChoice &choice)
{
    if (canReverseContourDirection(feature)) {
        const QVector<QVector3D> source = feature.points;
        const int pointCount = source.size();
        const int start = boundedStartIndex(choice.startPointIndex, pointCount);
        QVector<QVector3D> ordered;
        ordered.reserve(pointCount);
        if (choice.closedContour) {
            const int step = choice.direction == ContourTraversalDirection::Reverse ? -1 : 1;
            for (int offset = 0; offset < pointCount; ++offset) {
                const int index = (start + step * offset + pointCount * 2) % pointCount;
                ordered.append(source.at(index));
            }
        } else if (choice.direction == ContourTraversalDirection::Reverse) {
            ordered = source;
            std::reverse(ordered.begin(), ordered.end());
        } else {
            ordered = source;
        }
        feature.points = ordered;
    }

    double compensation = 0.0;
    if (choice.compensation == ContourCompensationChoice::LeftG41) {
        compensation = 1.0;
    } else if (choice.compensation == ContourCompensationChoice::RightG42) {
        compensation = -1.0;
    }
    params.set(QStringLiteral("compensation"), compensation);
    const int side = choice.machiningSide == ChainMachiningSide::Mixed
        ? 1 : contourCutterSide(feature, choice.machiningSide);
    params.set(QStringLiteral("camOffsetSide"), side);
    if (choice.machiningSide != ChainMachiningSide::Mixed && compensation != 0)
        params.set(QStringLiteral("compensation"), side);
}

bool bindContourMachiningChoice(SelectionChain &selection,
                                const QVector<QVector3D> &sourcePoints,
                                const ContourMachiningChoice &choice,
                                const MeshData &mesh)
{
    if (sourcePoints.size() < 2 || choice.closedContour != selection.closed) return false;
    const bool reverse = choice.direction == ContourTraversalDirection::Reverse;
    const int index = choice.closedContour ? choice.startPointIndex
                                          : (reverse ? sourcePoints.size() - 1 : 0);
    if (index < 0 || index >= sourcePoints.size()) return false;
    const QVector3D start = sourcePoints.at(index);
    if (sourcePoints.count(start) != 1) return false;
    SelectionChain bound = selection;
    bound.startPoint = start;
    bound.hasStartPoint = true;
    bound.reversed = selection.reversed != reverse;
    if (selection.selectionMode == ChainSelectionMode::Face) {
        if (selection.orderedTopologyRefs.size() != 1 ||
            selection.orderedTopologyRefs.first().kind != SelectionTopologyKind::Face) return false;
        int outerCount = 0;
        for (const auto &loop : selection.boundaryLoops) outerCount += loop.outer;
        if (outerCount != 1) return false;
        selection = bound;
        return true;
    }
    if (selection.evidenceVersion < 2 || selection.orderedTopologyRefs.isEmpty()) return false;
    auto edgeFor = [&mesh](const QString &id) -> const MeshPickEdge * {
        for (const auto &edge : mesh.pickEdges) if (edge.id == id) return &edge;
        return nullptr;
    };
    auto vertexFor = [&mesh](const QString &id) -> const MeshPickVertex * {
        for (const auto &vertex : mesh.pickVertices) if (vertex.id == id) return &vertex;
        return nullptr;
    };
    if (reverse) {
        std::reverse(bound.orderedTopologyRefs.begin(), bound.orderedTopologyRefs.end());
        for (auto &ref : bound.orderedTopologyRefs) ref.forward = !ref.forward;
    }
    int startIndex = -1;
    QString startId;
    for (int i = 0; i < bound.orderedTopologyRefs.size(); ++i) {
        const auto &ref = bound.orderedTopologyRefs.at(i);
        const auto *edge = edgeFor(ref.id);
        if (!edge || ref.kind != SelectionTopologyKind::Edge) return false;
        const QString id = ref.forward ? edge->firstVertexId : edge->lastVertexId;
        const auto *vertex = vertexFor(id);
        if (vertex && vertex->point == start) {
            if (startIndex >= 0) return false;
            startIndex = i;
            startId = id;
        }
    }
    if (startIndex < 0 || (!selection.closed && startIndex != 0)) return false;
    std::rotate(bound.orderedTopologyRefs.begin(),
                bound.orderedTopologyRefs.begin() + startIndex, bound.orderedTopologyRefs.end());
    QString previousEnd;
    for (const auto &ref : bound.orderedTopologyRefs) {
        const auto *edge = edgeFor(ref.id);
        const QString first = ref.forward ? edge->firstVertexId : edge->lastVertexId;
        if (!previousEnd.isEmpty() && previousEnd != first) return false;
        previousEnd = ref.forward ? edge->lastVertexId : edge->firstVertexId;
    }
    if (selection.closed && previousEnd != startId) return false;
    bound.startAnchor = {startId, start, false, 0.0};
    bound.hasStartAnchor = true;
    bound.endAnchor = {};
    bound.hasEndAnchor = false;
    if (!selection.closed) {
        const auto *end = vertexFor(previousEnd);
        if (!end || end->point != sourcePoints.at(reverse ? 0 : sourcePoints.size() - 1)) return false;
        bound.endAnchor = {end->id, end->point, false, 0.0};
        bound.hasEndAnchor = true;
    }
    bound.orderedGeometryIds.clear();
    for (const auto &ref : bound.orderedTopologyRefs) bound.orderedGeometryIds.append(ref.id);
    selection = bound;
    return true;
}
