#include "ContourMachiningChoice.h"

#include <algorithm>

namespace {

int boundedStartIndex(int index, int pointCount)
{
    return pointCount > 0 ? std::max(0, std::min(index, pointCount - 1)) : 0;
}

QString pointGeometryId(const QVector3D &point)
{
    return QStringLiteral("point:%1:%2:%3")
        .arg(point.x(), 0, 'f', 6)
        .arg(point.y(), 0, 'f', 6)
        .arg(point.z(), 0, 'f', 6);
}

} // namespace

bool isManualContourChoiceStrategy(const QString &strategyId)
{
    return strategyId == QStringLiteral("mill_closed_contour")
        || strategyId == QStringLiteral("mill_open_contour")
        || strategyId == QStringLiteral("mill_outer_chamfer")
        || strategyId == QStringLiteral("mill_slope_plane_2d")
        || strategyId == QStringLiteral("mill_annular")
        || strategyId == QStringLiteral("mill_island");
}

bool canReverseContourDirection(const ContourFeature &feature)
{
    return feature.points.size() >= 2;
}

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
        feature.center + QVector3D(float(-halfLength), float(-halfWidth), 0.0f),
        feature.center + QVector3D(float( halfLength), float(-halfWidth), 0.0f),
        feature.center + QVector3D(float( halfLength), float( halfWidth), 0.0f),
        feature.center + QVector3D(float(-halfLength), float( halfWidth), 0.0f)
    };
}

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
}

SelectionChain selectionChainForContourChoice(
    const ContourFeature &feature,
    const ContourMachiningChoice &choice,
    const QString &coordinateSystemId)
{
    SelectionChain initial;
    initial.geometrySource = choice.geometrySource;
    initial.selectionMode = choice.selectionMode;
    initial.machiningSide = choice.machiningSide;
    initial.coordinateSystemId = coordinateSystemId;
    initial.reversed = choice.direction == ContourTraversalDirection::Reverse;
    initial.closed = choice.closedContour;

    SelectionChainController controller(initial);
    controller.begin();
    for (const QVector3D &point : feature.points) {
        if (!controller.addGeometry(pointGeometryId(point), point)) {
            return SelectionChain();
        }
    }
    if (!feature.points.isEmpty()) {
        controller.setStartPoint(feature.points.first());
    }
    controller.sort(choice.sortStrategy);
    if (!controller.finishChain()) {
        return SelectionChain();
    }

    QStringList branches{QStringLiteral("outer")};
    for (int index = 0; index < feature.islands.size(); ++index) {
        branches.append(QStringLiteral("island:%1").arg(index));
    }
    int branchIndex = branches.indexOf(choice.selectedBranchGeometryId);
    controller.setBranchChoices(branches, branchIndex >= 0 ? branchIndex : 0);
    return controller.commit();
}
