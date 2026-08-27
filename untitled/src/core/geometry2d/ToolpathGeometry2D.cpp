#include "ToolpathGeometry2D.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cnext::geometry2d {
namespace {

constexpr double kPi = 3.14159265358979323846;

Point Add(Point first, Point second)
{
    return {first.x + second.x, first.y + second.y};
}

Point Subtract(Point first, Point second)
{
    return {first.x - second.x, first.y - second.y};
}

Point Scale(Point point, double scale)
{
    return {point.x * scale, point.y * scale};
}

double Dot(Point first, Point second)
{
    return first.x * second.x + first.y * second.y;
}

double Cross(Point first, Point second)
{
    return first.x * second.y - first.y * second.x;
}

double Length(Point point)
{
    return std::hypot(point.x, point.y);
}

double Distance(Point first, Point second)
{
    return Length(Subtract(first, second));
}

Point UnitTangentAtStart(const Segment &segment,
                         const GeometryTolerance &tolerance)
{
    Point tangent;
    if (segment.kind == SegmentKind::Line) {
        tangent = Subtract(segment.end(), segment.start());
    } else {
        const double direction = segment.arc.sweepAngle >= 0.0 ? 1.0 : -1.0;
        tangent = {-std::sin(segment.arc.startAngle) * direction,
                   std::cos(segment.arc.startAngle) * direction};
    }
    const double length = Length(tangent);
    if (length <= tolerance.minimumSegmentLength) {
        return {};
    }
    return Scale(tangent, 1.0 / length);
}

bool NearlyEqual(Point first, Point second, double tolerance)
{
    return Distance(first, second) <= tolerance;
}

double NormalizeAngle(double angle)
{
    double normalized = std::fmod(angle, 2.0 * kPi);
    if (normalized < 0.0) {
        normalized += 2.0 * kPi;
    }
    return normalized;
}

bool IsFullCircle(const ArcSegment &arc, const GeometryTolerance &tolerance)
{
    return std::abs(std::abs(arc.sweepAngle) - 2.0 * kPi) <=
           tolerance.angular * 10.0;
}

bool AngleOnArc(double angle, const ArcSegment &arc,
                const GeometryTolerance &tolerance, bool include_end = true)
{
    if (IsFullCircle(arc, tolerance)) {
        return true;
    }
    const double direction = arc.sweepAngle >= 0.0 ? 1.0 : -1.0;
    const double traveled = direction > 0.0
        ? NormalizeAngle(angle - arc.startAngle)
        : NormalizeAngle(arc.startAngle - angle);
    const double span = std::abs(arc.sweepAngle);
    return include_end ? traveled <= span + tolerance.angular
                       : traveled < span - tolerance.angular;
}

bool PointOnLine(const LineSegment &line, Point point,
                 const GeometryTolerance &tolerance)
{
    const Point direction = Subtract(line.second, line.first);
    const double length = Length(direction);
    if (length <= tolerance.minimumSegmentLength) {
        return NearlyEqual(line.first, point, tolerance.linear);
    }
    if (std::abs(Cross(direction, Subtract(point, line.first))) >
        tolerance.linear * length) {
        return false;
    }
    const double projection = Dot(Subtract(point, line.first), direction);
    return projection >= -tolerance.linear &&
           projection <= Dot(direction, direction) + tolerance.linear;
}

bool PointOnArc(const ArcSegment &arc, Point point,
                const GeometryTolerance &tolerance)
{
    if (arc.radius <= tolerance.minimumSegmentLength ||
        std::abs(arc.sweepAngle) <= tolerance.angular) {
        return false;
    }
    const Point radial = Subtract(point, arc.center);
    if (std::abs(Length(radial) - arc.radius) > tolerance.linear) {
        return false;
    }
    return AngleOnArc(std::atan2(radial.y, radial.x), arc, tolerance);
}

bool PointOnSegment(const Segment &segment, Point point,
                    const GeometryTolerance &tolerance)
{
    return segment.kind == SegmentKind::Line
        ? PointOnLine(segment.line, point, tolerance)
        : PointOnArc(segment.arc, point, tolerance);
}

double DistanceToSegment(const Segment &segment, Point point,
                         const GeometryTolerance &tolerance)
{
    if (segment.kind == SegmentKind::Line) {
        const Point direction = Subtract(segment.line.second, segment.line.first);
        const double squared = Dot(direction, direction);
        if (squared <= tolerance.minimumSegmentLength *
                       tolerance.minimumSegmentLength) {
            return Distance(point, segment.line.first);
        }
        const double parameter = std::clamp(
            Dot(Subtract(point, segment.line.first), direction) / squared,
            0.0, 1.0);
        return Distance(point, Add(segment.line.first,
                                   Scale(direction, parameter)));
    }
    const Point radial = Subtract(point, segment.arc.center);
    const double angle = std::atan2(radial.y, radial.x);
    if (AngleOnArc(angle, segment.arc, tolerance)) {
        return std::abs(Length(radial) - segment.arc.radius);
    }
    return std::min(Distance(point, segment.start()),
                    Distance(point, segment.end()));
}

void AppendUnique(QVector<Point> &points, Point point, double tolerance)
{
    for (Point existing : points) {
        if (NearlyEqual(existing, point, tolerance)) {
            return;
        }
    }
    points.append(point);
}

IntersectionResult IntersectLines(const LineSegment &first,
                                  const LineSegment &second,
                                  const GeometryTolerance &tolerance)
{
    const Point first_direction = Subtract(first.second, first.first);
    const Point second_direction = Subtract(second.second, second.first);
    const double first_length = Length(first_direction);
    const double second_length = Length(second_direction);
    if (first_length <= tolerance.minimumSegmentLength ||
        second_length <= tolerance.minimumSegmentLength) {
        return {IntersectionKind::Degenerate, {}};
    }
    const double denominator = Cross(first_direction, second_direction);
    const Point delta = Subtract(second.first, first.first);
    if (std::abs(denominator) <= tolerance.linear *
                                     first_length * second_length) {
        if (std::abs(Cross(delta, first_direction)) >
            tolerance.linear * first_length) {
            return {};
        }
        const double squared = Dot(first_direction, first_direction);
        const double first_parameter = Dot(delta, first_direction) / squared;
        const double second_parameter =
            Dot(Subtract(second.second, first.first), first_direction) / squared;
        const double low = std::max(0.0, std::min(first_parameter, second_parameter));
        const double high = std::min(1.0, std::max(first_parameter, second_parameter));
        if (high < low - tolerance.linear) {
            return {};
        }
        if (std::abs(high - low) <= tolerance.linear) {
            return {IntersectionKind::Tangent,
                    {Add(first.first, Scale(first_direction, (low + high) * 0.5))}};
        }
        return {IntersectionKind::Overlap, {}};
    }
    const double first_parameter = Cross(delta, second_direction) / denominator;
    const double second_parameter = Cross(delta, first_direction) / denominator;
    if (first_parameter < -tolerance.linear ||
        first_parameter > 1.0 + tolerance.linear ||
        second_parameter < -tolerance.linear ||
        second_parameter > 1.0 + tolerance.linear) {
        return {};
    }
    return {IntersectionKind::Points,
            {Add(first.first, Scale(first_direction, first_parameter))}};
}

IntersectionResult IntersectLineArc(const LineSegment &line,
                                    const ArcSegment &arc,
                                    const GeometryTolerance &tolerance)
{
    const Point direction = Subtract(line.second, line.first);
    const double coefficient_a = Dot(direction, direction);
    if (coefficient_a <= tolerance.minimumSegmentLength *
                         tolerance.minimumSegmentLength ||
        arc.radius <= tolerance.minimumSegmentLength) {
        return {IntersectionKind::Degenerate, {}};
    }
    const Point relative = Subtract(line.first, arc.center);
    const double coefficient_b = 2.0 * Dot(relative, direction);
    const double coefficient_c = Dot(relative, relative) - arc.radius * arc.radius;
    const double discriminant = coefficient_b * coefficient_b -
                                4.0 * coefficient_a * coefficient_c;
    if (discriminant < -tolerance.linear) {
        return {};
    }
    IntersectionResult result;
    const double root = std::sqrt(std::max(0.0, discriminant));
    const double parameters[] = {
        (-coefficient_b - root) / (2.0 * coefficient_a),
        (-coefficient_b + root) / (2.0 * coefficient_a)};
    for (double parameter : parameters) {
        if (parameter < -tolerance.linear ||
            parameter > 1.0 + tolerance.linear) {
            continue;
        }
        const Point point = Add(line.first, Scale(direction, parameter));
        if (PointOnArc(arc, point, tolerance)) {
            AppendUnique(result.points, point, tolerance.linear);
        }
    }
    if (result.points.isEmpty()) {
        return {};
    }
    result.kind = std::abs(discriminant) <= tolerance.linear
        ? IntersectionKind::Tangent : IntersectionKind::Points;
    return result;
}

IntersectionResult IntersectArcs(const ArcSegment &first,
                                 const ArcSegment &second,
                                 const GeometryTolerance &tolerance)
{
    if (first.radius <= tolerance.minimumSegmentLength ||
        second.radius <= tolerance.minimumSegmentLength) {
        return {IntersectionKind::Degenerate, {}};
    }
    const double center_distance = Distance(first.center, second.center);
    if (center_distance <= tolerance.linear &&
        std::abs(first.radius - second.radius) <= tolerance.linear) {
        return {IntersectionKind::Overlap, {}};
    }
    if (center_distance > first.radius + second.radius + tolerance.linear ||
        center_distance < std::abs(first.radius - second.radius) - tolerance.linear ||
        center_distance <= tolerance.linear) {
        return {};
    }
    const double along = (first.radius * first.radius -
                          second.radius * second.radius +
                          center_distance * center_distance) /
                         (2.0 * center_distance);
    const double height_squared = first.radius * first.radius - along * along;
    if (height_squared < -tolerance.linear) {
        return {};
    }
    const Point axis = Scale(Subtract(second.center, first.center),
                             1.0 / center_distance);
    const Point base = Add(first.center, Scale(axis, along));
    const Point normal{-axis.y, axis.x};
    const double height = std::sqrt(std::max(0.0, height_squared));
    const Point candidates[] = {
        Add(base, Scale(normal, height)), Add(base, Scale(normal, -height))};
    IntersectionResult result;
    for (Point point : candidates) {
        if (PointOnArc(first, point, tolerance) &&
            PointOnArc(second, point, tolerance)) {
            AppendUnique(result.points, point, tolerance.linear);
        }
    }
    if (result.points.isEmpty()) {
        return {};
    }
    result.kind = height <= tolerance.linear
        ? IntersectionKind::Tangent : IntersectionKind::Points;
    return result;
}

bool IsAdjacent(int first, int second, int count)
{
    return second == first + 1 || (first == 0 && second == count - 1);
}

bool SegmentEquivalent(const Segment &first, const Segment &second,
                       const GeometryTolerance &tolerance)
{
    if (first.kind != second.kind) {
        return false;
    }
    if (first.kind == SegmentKind::Line) {
        return (NearlyEqual(first.start(), second.start(), tolerance.linear) &&
                NearlyEqual(first.end(), second.end(), tolerance.linear)) ||
               (NearlyEqual(first.start(), second.end(), tolerance.linear) &&
                NearlyEqual(first.end(), second.start(), tolerance.linear));
    }
    return NearlyEqual(first.arc.center, second.arc.center, tolerance.linear) &&
           std::abs(first.arc.radius - second.arc.radius) <= tolerance.linear &&
           ((NearlyEqual(first.start(), second.start(), tolerance.linear) &&
             NearlyEqual(first.end(), second.end(), tolerance.linear)) ||
            (NearlyEqual(first.start(), second.end(), tolerance.linear) &&
             NearlyEqual(first.end(), second.start(), tolerance.linear)));
}

bool WiresIntersect(const Wire &first, const Wire &second,
                    const GeometryTolerance &tolerance)
{
    for (const Segment &first_segment : first.segments) {
        for (const Segment &second_segment : second.segments) {
            if (Intersect(first_segment, second_segment, tolerance).kind !=
                IntersectionKind::None) {
                return true;
            }
        }
    }
    return false;
}

bool IsConvexLineWire(const Wire &wire, const GeometryTolerance &tolerance)
{
    double sign = 0.0;
    for (int index = 0; index < wire.segments.size(); ++index) {
        const Segment &current = wire.segments.at(index);
        const Segment &next = wire.segments.at(
            (index + 1) % wire.segments.size());
        if (current.kind != SegmentKind::Line || next.kind != SegmentKind::Line) {
            return false;
        }
        const double turn = Cross(Subtract(current.end(), current.start()),
                                  Subtract(next.end(), next.start()));
        if (std::abs(turn) <= tolerance.linear) {
            continue;
        }
        if (sign == 0.0) {
            sign = turn;
        } else if (sign * turn < 0.0) {
            return false;
        }
    }
    return sign != 0.0;
}

bool InfiniteLineIntersection(const LineSegment &first,
                              const LineSegment &second,
                              const GeometryTolerance &tolerance,
                              Point *intersection)
{
    const Point first_direction = Subtract(first.second, first.first);
    const Point second_direction = Subtract(second.second, second.first);
    const double denominator = Cross(first_direction, second_direction);
    if (std::abs(denominator) <= tolerance.linear *
                                     Length(first_direction) *
                                     Length(second_direction)) {
        return false;
    }
    const double parameter = Cross(Subtract(second.first, first.first),
                                   second_direction) / denominator;
    *intersection = Add(first.first, Scale(first_direction, parameter));
    return true;
}

}  // namespace

double Pi()
{
    return kPi;
}

Segment Segment::Line(Point first, Point second)
{
    Segment segment;
    segment.kind = SegmentKind::Line;
    segment.line = {first, second};
    return segment;
}

Segment Segment::Arc(Point center, double radius,
                     double start_angle, double sweep_angle)
{
    Segment segment;
    segment.kind = SegmentKind::Arc;
    segment.arc = {center, radius, start_angle, sweep_angle};
    return segment;
}

Point Segment::start() const
{
    if (kind == SegmentKind::Line) {
        return line.first;
    }
    return Add(arc.center,
               {arc.radius * std::cos(arc.startAngle),
                arc.radius * std::sin(arc.startAngle)});
}

Point Segment::end() const
{
    if (kind == SegmentKind::Line) {
        return line.second;
    }
    const double angle = arc.startAngle + arc.sweepAngle;
    return Add(arc.center,
               {arc.radius * std::cos(angle), arc.radius * std::sin(angle)});
}

Wire WireFromPolyline(const QVector<Point> &points, bool closed)
{
    Wire wire;
    if (points.size() < 2) {
        return wire;
    }
    for (int index = 0; index + 1 < points.size(); ++index) {
        wire.segments.append(Segment::Line(points.at(index), points.at(index + 1)));
    }
    if (closed) {
        wire.segments.append(Segment::Line(points.last(), points.first()));
    }
    return wire;
}

double SignedArea(const Wire &wire)
{
    double doubled_area = 0.0;
    for (const Segment &segment : wire.segments) {
        if (segment.kind == SegmentKind::Line) {
            doubled_area += Cross(segment.start(), segment.end());
            continue;
        }
        const Point start = segment.start();
        const Point end = segment.end();
        doubled_area += segment.arc.center.x * (end.y - start.y) -
                        segment.arc.center.y * (end.x - start.x) +
                        segment.arc.radius * segment.arc.radius *
                            segment.arc.sweepAngle;
    }
    return doubled_area * 0.5;
}

PointRelation ClassifyPoint(const Wire &wire, Point point,
                            const GeometryTolerance &tolerance)
{
    for (const Segment &segment : wire.segments) {
        if (PointOnSegment(segment, point, tolerance)) {
            return PointRelation::OnBoundary;
        }
    }
    int crossings = 0;
    for (const Segment &segment : wire.segments) {
        if (segment.kind == SegmentKind::Line) {
            const Point first = segment.start();
            const Point second = segment.end();
            if ((first.y > point.y) == (second.y > point.y)) {
                continue;
            }
            const double x = first.x + (point.y - first.y) *
                             (second.x - first.x) / (second.y - first.y);
            if (x > point.x + tolerance.linear) {
                ++crossings;
            }
            continue;
        }
        const double normalized_y =
            (point.y - segment.arc.center.y) / segment.arc.radius;
        if (normalized_y < -1.0 || normalized_y > 1.0) {
            continue;
        }
        const double first_angle = std::asin(std::clamp(normalized_y, -1.0, 1.0));
        const double angles[] = {first_angle, kPi - first_angle};
        for (double angle : angles) {
            if (!AngleOnArc(angle, segment.arc, tolerance, false) ||
                std::abs(std::cos(angle)) <= tolerance.angular) {
                continue;
            }
            const double x = segment.arc.center.x +
                             segment.arc.radius * std::cos(angle);
            if (x > point.x + tolerance.linear) {
                ++crossings;
            }
        }
    }
    return crossings % 2 == 1 ? PointRelation::Inside : PointRelation::Outside;
}

IntersectionResult Intersect(const Segment &first, const Segment &second,
                             const GeometryTolerance &tolerance)
{
    if (first.kind == SegmentKind::Line && second.kind == SegmentKind::Line) {
        return IntersectLines(first.line, second.line, tolerance);
    }
    if (first.kind == SegmentKind::Line && second.kind == SegmentKind::Arc) {
        return IntersectLineArc(first.line, second.arc, tolerance);
    }
    if (first.kind == SegmentKind::Arc && second.kind == SegmentKind::Line) {
        return IntersectLineArc(second.line, first.arc, tolerance);
    }
    return IntersectArcs(first.arc, second.arc, tolerance);
}

WireValidation ValidateWire(const Wire &wire,
                            const GeometryTolerance &tolerance)
{
    WireValidation result;
    if (wire.segments.isEmpty()) {
        result.errors.append(QStringLiteral("Wire has no segments."));
        return result;
    }
    for (int index = 0; index < wire.segments.size(); ++index) {
        const Segment &segment = wire.segments.at(index);
        const double segment_length = segment.kind == SegmentKind::Line
            ? Distance(segment.start(), segment.end())
            : std::abs(segment.arc.sweepAngle) * segment.arc.radius;
        if (segment_length <= tolerance.minimumSegmentLength) {
            result.errors.append(QStringLiteral("Wire contains a zero or minimal segment."));
        }
        const Segment &next = wire.segments.at((index + 1) % wire.segments.size());
        if (!NearlyEqual(segment.end(), next.start(), tolerance.linear)) {
            result.errors.append(QStringLiteral(
                "Wire is not closed or has a non-manifold connection."));
        }
    }
    for (int first = 0; first < wire.segments.size(); ++first) {
        for (int second = first + 1; second < wire.segments.size(); ++second) {
            if (SegmentEquivalent(wire.segments.at(first),
                                  wire.segments.at(second), tolerance)) {
                result.errors.append(QStringLiteral("Wire contains a repeated edge."));
                continue;
            }
            const IntersectionResult intersection = Intersect(
                wire.segments.at(first), wire.segments.at(second), tolerance);
            if (IsAdjacent(first, second, wire.segments.size())) {
                const Point shared_point = first == 0 &&
                        second == wire.segments.size() - 1
                    ? wire.segments.at(first).start()
                    : wire.segments.at(first).end();
                if (intersection.kind == IntersectionKind::Overlap ||
                    intersection.kind == IntersectionKind::Degenerate ||
                    intersection.points.size() != 1 ||
                    !NearlyEqual(intersection.points.first(),
                                 shared_point,
                                 tolerance.linear)) {
                    result.errors.append(QStringLiteral(
                        "Adjacent wire edges overlap or degenerate."));
                }
                continue;
            }
            if (intersection.kind != IntersectionKind::None) {
                result.errors.append(QStringLiteral("Wire self-intersects or self-touches."));
            }
        }
    }
    if (std::abs(SignedArea(wire)) <= tolerance.linear * tolerance.linear) {
        result.errors.append(QStringLiteral("Wire has zero enclosed area."));
    }
    result.ok = result.errors.isEmpty();
    return result;
}

RegionValidation ValidateRegion(const Region &region,
                                const GeometryTolerance &tolerance)
{
    RegionValidation result;
    const WireValidation outer_validation = ValidateWire(region.outer, tolerance);
    result.errors += outer_validation.errors;
    for (int island_index = 0; island_index < region.islands.size(); ++island_index) {
        const Wire &island = region.islands.at(island_index);
        const WireValidation island_validation = ValidateWire(island, tolerance);
        result.errors += island_validation.errors;
        if (island.segments.isEmpty() ||
            ClassifyPoint(region.outer, island.segments.first().start(), tolerance) !=
                PointRelation::Inside ||
            WiresIntersect(region.outer, island, tolerance)) {
                result.errors.append(QStringLiteral(
                    "Island must lie strictly inside the outer wire."));
        }
        for (int other_index = 0; other_index < island_index; ++other_index) {
            const Wire &other = region.islands.at(other_index);
            if (WiresIntersect(island, other, tolerance) ||
                ClassifyPoint(other, island.segments.first().start(), tolerance) !=
                    PointRelation::Outside ||
                ClassifyPoint(island, other.segments.first().start(), tolerance) !=
                    PointRelation::Outside) {
                result.errors.append(QStringLiteral(
                    "Islands overlap, touch, or contain each other."));
            }
        }
    }
    result.ok = result.errors.isEmpty();
    return result;
}

bool IsPointInMachiningRegion(const Region &region, Point point,
                              const GeometryTolerance &tolerance)
{
    if (ClassifyPoint(region.outer, point, tolerance) == PointRelation::Outside) {
        return false;
    }
    for (const Wire &island : region.islands) {
        if (ClassifyPoint(island, point, tolerance) != PointRelation::Outside) {
            return false;
        }
    }
    return true;
}

bool HasBoundaryClearance(const Wire &wire, Point point, double clearance,
                          const GeometryTolerance &tolerance)
{
    if (ClassifyPoint(wire, point, tolerance) == PointRelation::Outside) {
        return false;
    }
    double minimum_distance = std::numeric_limits<double>::max();
    for (const Segment &segment : wire.segments) {
        minimum_distance = std::min(
            minimum_distance, DistanceToSegment(segment, point, tolerance));
    }
    return minimum_distance + tolerance.linear >= clearance;
}

OffsetResult OffsetWire(const Wire &wire, OffsetSide side, double distance,
                        const GeometryTolerance &tolerance)
{
    OffsetResult result;
    const WireValidation validation = ValidateWire(wire, tolerance);
    if (!validation.ok) {
        result.error = QStringLiteral("Invalid wire cannot be offset.");
        return result;
    }
    if (distance <= tolerance.linear) {
        result.error = QStringLiteral("Offset distance must be positive.");
        return result;
    }
    if (wire.segments.size() == 1 &&
        wire.segments.first().kind == SegmentKind::Arc &&
        IsFullCircle(wire.segments.first().arc, tolerance)) {
        ArcSegment arc = wire.segments.first().arc;
        const double orientation = arc.sweepAngle >= 0.0 ? 1.0 : -1.0;
        const double signed_offset = side == OffsetSide::Inside
            ? distance : -distance;
        arc.radius -= signed_offset * orientation;
        if (arc.radius <= tolerance.minimumSegmentLength) {
            result.error = QStringLiteral("Offset collapses the circular wire.");
            return result;
        }
        result.wire.segments.append(Segment::Arc(
            arc.center, arc.radius, arc.startAngle, arc.sweepAngle));
        result.ok = true;
        return result;
    }
    const bool contains_arc = std::any_of(
        wire.segments.cbegin(), wire.segments.cend(),
        [](const Segment &segment) {
            return segment.kind == SegmentKind::Arc;
        });
    if (contains_arc) {
        const double orientation = SignedArea(wire) > 0.0 ? 1.0 : -1.0;
        const double left_offset =
            (side == OffsetSide::Inside ? 1.0 : -1.0) * orientation * distance;
        for (const Segment &segment : wire.segments) {
            if (segment.kind == SegmentKind::Line) {
                const Point direction = Subtract(segment.end(), segment.start());
                const double length = Length(direction);
                const Point normal{-direction.y / length, direction.x / length};
                result.wire.segments.append(Segment::Line(
                    Add(segment.start(), Scale(normal, left_offset)),
                    Add(segment.end(), Scale(normal, left_offset))));
                continue;
            }
            const double arc_direction =
                segment.arc.sweepAngle >= 0.0 ? 1.0 : -1.0;
            const double radius = segment.arc.radius -
                                  left_offset * arc_direction;
            if (radius <= tolerance.minimumSegmentLength) {
                result.wire = {};
                result.error = QStringLiteral("Offset collapses an arc segment.");
                return result;
            }
            result.wire.segments.append(Segment::Arc(
                segment.arc.center, radius, segment.arc.startAngle,
                segment.arc.sweepAngle));
        }
        for (int index = 0; index < result.wire.segments.size(); ++index) {
            const Segment &segment = result.wire.segments.at(index);
            const Segment &next = result.wire.segments.at(
                (index + 1) % result.wire.segments.size());
            if (!NearlyEqual(segment.end(), next.start(), tolerance.linear)) {
                result.wire = {};
                result.error = QStringLiteral(
                    "Line-arc offset requires tangent continuous joins.");
                return result;
            }
        }
        if (!ValidateWire(result.wire, tolerance).ok) {
            result.wire = {};
            result.error = QStringLiteral("Line-arc offset self-intersects.");
            return result;
        }
        for (const Segment &segment : result.wire.segments) {
            if (side == OffsetSide::Inside &&
                !HasBoundaryClearance(wire, segment.start(),
                                      distance, tolerance)) {
                result.wire = {};
                result.error = QStringLiteral(
                    "Line-arc offset does not preserve requested clearance.");
                return result;
            }
        }
        result.ok = true;
        return result;
    }
    if (!IsConvexLineWire(wire, tolerance)) {
        result.error = QStringLiteral(
            "Only convex line wires and full circles are supported safely.");
        return result;
    }

    const double orientation = SignedArea(wire) > 0.0 ? 1.0 : -1.0;
    const double left_offset =
        (side == OffsetSide::Inside ? 1.0 : -1.0) * orientation * distance;
    QVector<LineSegment> shifted;
    for (const Segment &segment : wire.segments) {
        const Point direction = Subtract(segment.end(), segment.start());
        const double length = Length(direction);
        const Point normal{-direction.y / length, direction.x / length};
        shifted.append({Add(segment.start(), Scale(normal, left_offset)),
                        Add(segment.end(), Scale(normal, left_offset))});
    }
    QVector<Point> vertices;
    for (int index = 0; index < shifted.size(); ++index) {
        Point vertex;
        const LineSegment &previous = shifted.at(
            (index + shifted.size() - 1) % shifted.size());
        if (!InfiniteLineIntersection(previous, shifted.at(index),
                                      tolerance, &vertex)) {
            result.error = QStringLiteral("Offset contains parallel adjacent edges.");
            return result;
        }
        vertices.append(vertex);
    }
    result.wire = WireFromPolyline(vertices, true);
    if (!ValidateWire(result.wire, tolerance).ok ||
        std::abs(SignedArea(result.wire)) <= tolerance.linear ||
        (side == OffsetSide::Inside &&
         std::abs(SignedArea(result.wire)) >= std::abs(SignedArea(wire)))) {
        result.wire = {};
        result.error = QStringLiteral("Offset collapses or self-intersects.");
        return result;
    }
    for (const Segment &segment : result.wire.segments) {
        if (side == OffsetSide::Inside &&
            !HasBoundaryClearance(wire, segment.start(), distance, tolerance)) {
            result.wire = {};
            result.error = QStringLiteral("Offset does not preserve requested clearance.");
            return result;
        }
    }
    result.ok = true;
    return result;
}

Wire ReverseWire(const Wire &wire)
{
    Wire reversed;
    for (int index = wire.segments.size() - 1; index >= 0; --index) {
        const Segment &segment = wire.segments.at(index);
        if (segment.kind == SegmentKind::Line) {
            reversed.segments.append(Segment::Line(segment.end(), segment.start()));
        } else {
            reversed.segments.append(Segment::Arc(
                segment.arc.center, segment.arc.radius,
                segment.arc.startAngle + segment.arc.sweepAngle,
                -segment.arc.sweepAngle));
        }
    }
    return reversed;
}

Wire SelectStableStart(const Wire &wire,
                       const GeometryTolerance &tolerance)
{
    Q_UNUSED(tolerance);
    if (wire.segments.isEmpty()) {
        return wire;
    }
    int best_index = 0;
    for (int index = 1; index < wire.segments.size(); ++index) {
        const Point candidate = wire.segments.at(index).start();
        const Point best = wire.segments.at(best_index).start();
        if (candidate.x < best.x ||
            (candidate.x == best.x && candidate.y < best.y)) {
            best_index = index;
        }
    }
    Wire stable;
    for (int offset = 0; offset < wire.segments.size(); ++offset) {
        stable.segments.append(wire.segments.at(
            (best_index + offset) % wire.segments.size()));
    }
    return stable;
}

QVector<LoopConnection> PlanLoopConnections(const QVector<Wire> &loops)
{
    QVector<LoopConnection> connections;
    for (int index = 0; index + 1 < loops.size(); ++index) {
        connections.append({index, index + 1, ConnectionMotion::LiftToSafe});
    }
    return connections;
}

QVector<LeadCandidate> PlanLeadCandidates(
    const Wire &wire, const GeometryTolerance &tolerance)
{
    QVector<LeadCandidate> candidates;
    if (!ValidateWire(wire, tolerance).ok) {
        return candidates;
    }
    const Wire stable = SelectStableStart(wire, tolerance);
    const Point tangent = UnitTangentAtStart(stable.segments.first(), tolerance);
    if (Length(tangent) <= tolerance.minimumSegmentLength) {
        return candidates;
    }
    const Point anchor = stable.segments.first().start();
    candidates.append({LeadKind::Entry, anchor, tangent, true});
    candidates.append({LeadKind::Exit, anchor, Scale(tangent, -1.0), true});
    return candidates;
}

}  // namespace cnext::geometry2d
