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

geometry::Wire Rectangle(double min_x, double min_y,
                         double max_x, double max_y)
{
    return geometry::WireFromPolyline(
        {{min_x, min_y}, {max_x, min_y}, {max_x, max_y}, {min_x, max_y}}, true);
}

geometry::Wire RegularPolygon(int count, double radius)
{
    QVector<geometry::Point> points;
    for (int index = 0; index < count; ++index) {
        const double angle = 2.0 * geometry::Pi() * index / count;
        points.append({radius * std::cos(angle), radius * std::sin(angle)});
    }
    return geometry::WireFromPolyline(points, true);
}

}  // namespace

int main()
{
    const geometry::GeometryTolerance tolerance;
    const geometry::Wire outer = Rectangle(0.0, 0.0, 40.0, 30.0);
    const geometry::Wire island = Rectangle(15.0, 10.0, 25.0, 20.0);
    geometry::Region region{outer, {island}};
    if (!Require(geometry::ValidateRegion(region, tolerance).ok,
                 "strictly contained island should form a valid region") ||
        !Require(geometry::IsPointInMachiningRegion(
                     region, {5.0, 5.0}, tolerance),
                 "outer material should remain machinable") ||
        !Require(!geometry::IsPointInMachiningRegion(
                     region, {20.0, 15.0}, tolerance),
                 "island interior should remain protected")) {
        return 1;
    }

    geometry::Region touching{outer, {Rectangle(0.0, 10.0, 5.0, 20.0)}};
    geometry::Region overlapping{
        outer,
        {Rectangle(10.0, 10.0, 22.0, 20.0),
         Rectangle(20.0, 10.0, 30.0, 20.0)}};
    if (!Require(!geometry::ValidateRegion(touching, tolerance).ok,
                 "island touching outer boundary must be rejected") ||
        !Require(!geometry::ValidateRegion(overlapping, tolerance).ok,
                 "overlapping islands must be rejected")) {
        return 1;
    }

    const geometry::OffsetResult inset = geometry::OffsetWire(
        outer, geometry::OffsetSide::Inside, 3.0, tolerance);
    if (!Require(inset.ok,
                 "convex rectangle should support a safe inside offset") ||
        !Require(geometry::ClassifyPoint(
                     inset.wire, {3.0, 3.0}, tolerance) ==
                     geometry::PointRelation::OnBoundary,
                 "inside offset should preserve the requested clearance")) {
        return 1;
    }

    const geometry::Wire concave = geometry::WireFromPolyline(
        {{0.0, 0.0}, {20.0, 0.0}, {20.0, 8.0},
         {8.0, 8.0}, {8.0, 20.0}, {0.0, 20.0}}, true);
    if (!Require(!geometry::OffsetWire(
                      concave, geometry::OffsetSide::Inside,
                      2.0, tolerance).ok,
                 "ambiguous concave offset must remain explicitly blocked")) {
        return 1;
    }

    const geometry::Wire circle{{geometry::Segment::Arc(
        {0.0, 0.0}, 10.0, 0.0, 2.0 * geometry::Pi())}};
    const geometry::OffsetResult circle_inset = geometry::OffsetWire(
        circle, geometry::OffsetSide::Inside, 2.0, tolerance);
    if (!Require(circle_inset.ok &&
                     std::abs(circle_inset.wire.segments.first().arc.radius - 8.0) <
                         tolerance.linear,
                 "full circle should remain an arc after offset")) {
        return 1;
    }

    geometry::Wire capsule;
    capsule.segments = {
        geometry::Segment::Line({0.0, 0.0}, {20.0, 0.0}),
        geometry::Segment::Arc({20.0, 5.0}, 5.0, -geometry::Pi() * 0.5,
                               geometry::Pi()),
        geometry::Segment::Line({20.0, 10.0}, {0.0, 10.0}),
        geometry::Segment::Arc({0.0, 5.0}, 5.0, geometry::Pi() * 0.5,
                               geometry::Pi())};
    const geometry::OffsetResult capsule_inset = geometry::OffsetWire(
        capsule, geometry::OffsetSide::Inside, 1.0, tolerance);
    if (!Require(capsule_inset.ok &&
                     capsule_inset.wire.segments.at(1).kind ==
                         geometry::SegmentKind::Arc &&
                     std::abs(capsule_inset.wire.segments.at(1).arc.radius - 4.0) <
                         tolerance.linear,
                 "tangent line-arc wire should offset without flattening arcs")) {
        return 1;
    }

    for (int side_count = 3; side_count <= 12; ++side_count) {
        const geometry::Wire polygon = RegularPolygon(side_count, 30.0);
        const geometry::OffsetResult offset = geometry::OffsetWire(
            polygon, geometry::OffsetSide::Inside, 2.0, tolerance);
        if (!Require(offset.ok,
                     "deterministic convex polygon offset property should hold")) {
            return 1;
        }
        for (const geometry::Segment &segment : offset.wire.segments) {
            if (!Require(geometry::HasBoundaryClearance(
                             polygon, segment.start(), 2.0, tolerance),
                         "every offset tool-center vertex must retain clearance")) {
                return 1;
            }
        }
    }

    if (!Require(!geometry::OffsetWire(
                      Rectangle(0.0, 0.0, 4.0, 20.0),
                      geometry::OffsetSide::Inside, 2.1, tolerance).ok,
                 "narrow channel must fail minimum reachable width")) {
        return 1;
    }

    const geometry::Wire reversed = geometry::ReverseWire(outer);
    const geometry::Wire stable = geometry::SelectStableStart(reversed, tolerance);
    const QVector<geometry::LoopConnection> connections =
        geometry::PlanLoopConnections({stable, island});
    const QVector<geometry::LeadCandidate> leads =
        geometry::PlanLeadCandidates(stable, tolerance);
    if (!Require(geometry::SignedArea(reversed) < 0.0,
                 "wire reversal should reverse orientation") ||
        !Require(stable.segments.first().start().x == 0.0 &&
                     stable.segments.first().start().y == 0.0,
                 "stable start should select the lexicographically smallest point") ||
        !Require(connections.size() == 1 &&
                     connections.first().motion ==
                         geometry::ConnectionMotion::LiftToSafe,
                 "cross-loop connection must require a safe-height lift") ||
        !Require(leads.size() == 2 &&
                     leads.first().kind == geometry::LeadKind::Entry &&
                     leads.last().kind == geometry::LeadKind::Exit &&
                     leads.first().requiresSafeHeight,
                 "wire should expose controller-neutral entry and exit candidates")) {
        return 1;
    }

    return 0;
}
