#include "SlotFramePlanningService.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

void Apply(const StrategyParams &source, StrategyParams *destination)
{
    for (auto it = source.values.cbegin(); it != source.values.cend(); ++it) {
        destination->values.insert(it.key(), it.value());
    }
}

StrategyParams Resolve(const OperationParameterLayers &layers)
{
    StrategyParams values = ProcessParameterSchema::CommonOperation().defaultParams();
    Apply(layers.systemDefaults, &values);
    Apply(layers.materialTemplate, &values);
    Apply(layers.toolTypeTemplate, &values);
    Apply(layers.moduleRule, &values);
    Apply(layers.manualOverrides, &values);
    return values;
}

double DistancePointToSegment(const QVector3D &point,
                              const QVector3D &start,
                              const QVector3D &end)
{
    const QVector3D delta = end - start;
    const double length_squared = delta.lengthSquared();
    if (length_squared <= 1.0e-12) {
        return (point - start).length();
    }
    const double parameter = std::clamp(
        double(QVector3D::dotProduct(point - start, delta)) / length_squared,
        0.0, 1.0);
    return (point - (start + delta * float(parameter))).length();
}

double LoopDistance(const QVector<QVector3D> &first,
                    const QVector<QVector3D> &second)
{
    double distance = std::numeric_limits<double>::max();
    for (const QVector3D &point : first) {
        for (int index = 0; index < second.size(); ++index) {
            distance = std::min(distance, DistancePointToSegment(
                point, second.at(index), second.at((index + 1) % second.size())));
        }
    }
    for (const QVector3D &point : second) {
        for (int index = 0; index < first.size(); ++index) {
            distance = std::min(distance, DistancePointToSegment(
                point, first.at(index), first.at((index + 1) % first.size())));
        }
    }
    return distance;
}

bool PointInside(const QVector<QVector3D> &polygon, const QVector3D &point)
{
    bool inside = false;
    for (int first = 0, second = polygon.size() - 1;
         first < polygon.size(); second = first++) {
        const QVector3D &a = polygon.at(first);
        const QVector3D &b = polygon.at(second);
        const bool crosses = ((a.y() > point.y()) != (b.y() > point.y())) &&
            (point.x() < (b.x() - a.x()) * (point.y() - a.y()) /
                             (b.y() - a.y()) + a.x());
        if (crosses) {
            inside = !inside;
        }
    }
    return inside;
}

double MinimumSpan(const QVector<QVector3D> &boundary)
{
    if (boundary.isEmpty()) {
        return 0.0;
    }
    double min_x = boundary.first().x();
    double max_x = min_x;
    double min_y = boundary.first().y();
    double max_y = min_y;
    for (const QVector3D &point : boundary) {
        min_x = std::min(min_x, double(point.x()));
        max_x = std::max(max_x, double(point.x()));
        min_y = std::min(min_y, double(point.y()));
        max_y = std::max(max_y, double(point.y()));
    }
    return std::min(max_x - min_x, max_y - min_y);
}

QString ValidateProtection(const SlotFrameFeature &feature,
                           double required_clearance)
{
    if (feature.kind == SlotFrameKind::OpenSlot &&
        !feature.protectionRegions.isEmpty()) {
        return QStringLiteral(
            "Open-slot protection regions are not supported by the verified slot strategy.");
    }
    const QVector<QVector<QVector3D>> protected_loops =
        feature.islands + feature.protectionRegions;
    for (const QVector<QVector3D> &loop : protected_loops) {
        if (loop.size() < 3) {
            return QStringLiteral("A protection boundary is incomplete.");
        }
        for (const QVector3D &point : loop) {
            if (!PointInside(feature.outerBoundary, point)) {
                return QStringLiteral(
                    "A protection boundary touches or leaves the machining boundary.");
            }
        }
        if (LoopDistance(feature.outerBoundary, loop) + 1.0e-6 <
            required_clearance) {
            return QStringLiteral(
                "The tool cannot pass safely between the outer and protected boundaries.");
        }
    }
    return QString();
}

}  // namespace

SlotFrameConfirmationResult SlotFramePlanningService::Confirm(
    const SlotFrameFeature &feature,
    const QMap<int, SlotFrameToolInfo> &available_tools,
    const ProcessContext &context,
    const OperationParameterLayers &layers)
{
    SlotFrameConfirmationResult result;
    if (!feature.rejectionReasons.isEmpty() || feature.geometryRef.isEmpty()) {
        result.errors.append(QStringLiteral(
            "The slot-frame candidate is not valid."));
        return result;
    }
    result.errors = context.validate();
    if (!result.errors.isEmpty()) {
        return result;
    }
    if (!available_tools.contains(context.toolId)) {
        result.errors.append(QStringLiteral(
            "The selected slot-frame tool is unavailable."));
        return result;
    }
    const SlotFrameToolInfo tool = available_tools.value(context.toolId);
    if (tool.type != QStringLiteral("end_mill") || tool.diameter <= 0.0 ||
        tool.fluteLength + 1.0e-6 < feature.depth ||
        tool.totalLength <= feature.depth + 1.0e-6) {
        result.errors.append(QStringLiteral(
            "The selected end mill has an unsafe type or reach."));
        return result;
    }

    const StrategyParams resolved = Resolve(layers);
    const double safe_height = resolved.get(QStringLiteral("safeHeight"));
    const double feed_height = resolved.get(QStringLiteral("plungeHeight"));
    const double reference_height = resolved.get(QStringLiteral("referenceHeight"));
    const double step_over = resolved.get(QStringLiteral("stepOver"));
    const double step_down = resolved.get(QStringLiteral("stepDown"));
    const double stock = resolved.get(QStringLiteral("stockToLeave"), 0.0);
    if (safe_height < feed_height || feed_height < reference_height ||
        step_over <= 0.0 || step_over > tool.diameter + 1.0e-6 ||
        step_down <= 0.0 || step_down > feature.depth + 1.0e-6) {
        result.errors.append(QStringLiteral(
            "Slot-frame safe height, stepOver, or stepDown is invalid."));
        return result;
    }

    const double minimum_span = feature.kind == SlotFrameKind::OpenSlot
        ? feature.machiningGeometry.width : MinimumSpan(feature.outerBoundary);
    const double required_clearance = tool.diameter + 2.0 * std::max(0.0, stock);
    if (minimum_span <= required_clearance + 1.0e-6) {
        result.errors.append(QStringLiteral(
            "The slot-frame channel is not reachable by the selected tool."));
        return result;
    }
    const QString protection_error = ValidateProtection(feature, required_clearance);
    if (!protection_error.isEmpty()) {
        result.errors.append(protection_error);
        return result;
    }

    OperationProposal proposal;
    proposal.kind = OperationProposalKind::Contour;
    proposal.strategyId = feature.kind == SlotFrameKind::OpenSlot
        ? QStringLiteral("mill_slot") : QStringLiteral("mill_pocket_rough");
    proposal.toolId = context.toolId;
    proposal.contourFeature = feature.machiningGeometry;
    proposal.contourFeature.points = feature.outerBoundary;
    proposal.contourFeature.islands = feature.islands + feature.protectionRegions;
    proposal.selectionChain.id = feature.geometryRef;
    proposal.selectionChain.geometrySource = ChainGeometrySource::Entity;
    proposal.selectionChain.selectionMode = ChainSelectionMode::Face;
    proposal.selectionChain.machiningSide = ChainMachiningSide::Inside;
    proposal.selectionChain.coordinateSystemId = context.setup.workOffset;
    proposal.selectionChain.orderedGeometryIds = QStringList{feature.geometryRef};
    proposal.selectionChain.closed = feature.closed;
    proposal.params.set(QStringLiteral("feedHeight"), feed_height);
    proposal.params.set(QStringLiteral("stepover"), step_over);

    const OperationFactoryResult created = OperationFactory::CreateConfirmed(
        proposal, layers, context);
    if (!created.ok) {
        result.errors = created.errors;
        return result;
    }
    result.ok = true;
    result.operations = {created.operation};
    return result;
}
