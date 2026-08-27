#include "../src/core/geometry2d/ToolpathGeometry2D.h"

#include <cmath>
#include <iostream>

namespace geometry = cnext::geometry2d;

namespace {

bool Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

geometry::Wire Polygon(std::initializer_list<geometry::Point> points)
{
    QVector<geometry::Point> vertices;
    for (const geometry::Point &point : points) {
        vertices.append(point);
    }
    return geometry::WireFromPolyline(vertices, true);
}

}  // namespace

int main()
{
    const geometry::GeometryTolerance tolerance;
    const geometry::Wire rectangle = Polygon(
        {{0.0, 0.0}, {20.0, 0.0}, {20.0, 10.0}, {0.0, 10.0}});
    if (!Require(geometry::ClassifyPoint(rectangle, {10.0, 5.0}, tolerance) ==
                     geometry::PointRelation::Inside,
                 "rectangle interior should classify inside") ||
        !Require(geometry::ClassifyPoint(rectangle, {20.0, 5.0}, tolerance) ==
                     geometry::PointRelation::OnBoundary,
                 "edge point should classify on boundary") ||
        !Require(geometry::ClassifyPoint(rectangle, {20.0, 10.0}, tolerance) ==
                     geometry::PointRelation::OnBoundary,
                 "vertex ray case should classify on boundary") ||
        !Require(geometry::ClassifyPoint(rectangle, {21.0, 5.0}, tolerance) ==
                     geometry::PointRelation::Outside,
                 "exterior point should classify outside") ||
        !Require(geometry::SignedArea(rectangle) > 0.0,
                 "counter-clockwise rectangle should have positive area")) {
        return 1;
    }

    const geometry::Segment crossing_a = geometry::Segment::Line(
        {0.0, 0.0}, {10.0, 10.0});
    const geometry::Segment crossing_b = geometry::Segment::Line(
        {0.0, 10.0}, {10.0, 0.0});
    const geometry::IntersectionResult crossing =
        geometry::Intersect(crossing_a, crossing_b, tolerance);
    if (!Require(crossing.kind == geometry::IntersectionKind::Points &&
                     crossing.points.size() == 1,
                 "crossing lines should return one normal intersection") ||
        !Require(std::abs(crossing.points.first().x - 5.0) < tolerance.linear,
                 "line intersection should be located accurately")) {
        return 1;
    }

    const geometry::IntersectionResult overlap = geometry::Intersect(
        geometry::Segment::Line({0.0, 0.0}, {10.0, 0.0}),
        geometry::Segment::Line({5.0, 0.0}, {15.0, 0.0}), tolerance);
    if (!Require(overlap.kind == geometry::IntersectionKind::Overlap &&
                     overlap.points.isEmpty(),
                 "collinear overlap must not manufacture an intersection point")) {
        return 1;
    }

    const geometry::Segment upper_semicircle = geometry::Segment::Arc(
        {0.0, 0.0}, 5.0, 0.0, geometry::Pi());
    const geometry::IntersectionResult tangent = geometry::Intersect(
        geometry::Segment::Line({-10.0, 5.0}, {10.0, 5.0}),
        upper_semicircle, tolerance);
    if (!Require(tangent.kind == geometry::IntersectionKind::Tangent &&
                     tangent.points.size() == 1,
                 "line-arc tangency should be explicit")) {
        return 1;
    }

    const geometry::IntersectionResult arc_crossing = geometry::Intersect(
        geometry::Segment::Arc({0.0, 0.0}, 5.0, 0.0, 2.0 * geometry::Pi()),
        geometry::Segment::Arc({6.0, 0.0}, 5.0, 0.0, 2.0 * geometry::Pi()),
        tolerance);
    if (!Require(arc_crossing.kind == geometry::IntersectionKind::Points &&
                     arc_crossing.points.size() == 2,
                 "two circles should retain two arc-arc intersections")) {
        return 1;
    }

    const geometry::Wire bow_tie = Polygon(
        {{0.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}, {10.0, 0.0}});
    const geometry::WireValidation bow_tie_validation =
        geometry::ValidateWire(bow_tie, tolerance);
    const geometry::Wire duplicate_edge = Polygon(
        {{0.0, 0.0}, {10.0, 0.0}, {0.0, 0.0}, {0.0, 10.0}});
    const geometry::Wire minimal_edge = Polygon(
        {{0.0, 0.0}, {1.0e-8, 0.0}, {10.0, 0.0}, {0.0, 10.0}});
    if (!Require(!bow_tie_validation.ok,
                 "self-intersecting bow-tie wire must be rejected") ||
        !Require(!geometry::ValidateWire(duplicate_edge, tolerance).ok,
                 "repeated or reversed duplicate edge must be rejected") ||
        !Require(!geometry::ValidateWire(minimal_edge, tolerance).ok,
                 "minimal edge must be rejected before offset")) {
        return 1;
    }

    return 0;
}
