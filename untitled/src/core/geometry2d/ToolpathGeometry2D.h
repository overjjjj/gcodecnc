#pragma once

#include <QStringList>
#include <QVector>

namespace cnext::geometry2d {

double Pi();

struct GeometryTolerance {
    double linear = 1.0e-6;
    double angular = 1.0e-9;
    double minimumSegmentLength = 1.0e-5;
};

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct LineSegment {
    Point first;
    Point second;
};

struct ArcSegment {
    Point center;
    double radius = 0.0;
    double startAngle = 0.0;
    double sweepAngle = 0.0;
};

enum class SegmentKind {
    Line,
    Arc
};

struct Segment {
    SegmentKind kind = SegmentKind::Line;
    LineSegment line;
    ArcSegment arc;

    static Segment Line(Point first, Point second);
    static Segment Arc(Point center, double radius,
                       double start_angle, double sweep_angle);
    Point start() const;
    Point end() const;
};

struct Wire {
    QVector<Segment> segments;
};

struct Region {
    Wire outer;
    QVector<Wire> islands;
};

enum class PointRelation {
    Outside,
    Inside,
    OnBoundary
};

enum class IntersectionKind {
    None,
    Points,
    Tangent,
    Overlap,
    Degenerate
};

struct IntersectionResult {
    IntersectionKind kind = IntersectionKind::None;
    QVector<Point> points;
};

struct WireValidation {
    bool ok = false;
    QStringList errors;
};

enum class OffsetSide {
    Inside,
    Outside
};

struct OffsetResult {
    bool ok = false;
    Wire wire;
    QString error;
};

struct RegionValidation {
    bool ok = false;
    QStringList errors;
};

enum class ConnectionMotion {
    ContinueAtCuttingHeight,
    LiftToSafe
};

struct LoopConnection {
    int fromLoop = -1;
    int toLoop = -1;
    ConnectionMotion motion = ConnectionMotion::LiftToSafe;
};

enum class LeadKind {
    Entry,
    Exit
};

struct LeadCandidate {
    LeadKind kind = LeadKind::Entry;
    Point anchor;
    Point tangent;
    bool requiresSafeHeight = true;
};

Wire WireFromPolyline(const QVector<Point> &points, bool closed);
double SignedArea(const Wire &wire);
PointRelation ClassifyPoint(const Wire &wire, Point point,
                            const GeometryTolerance &tolerance);
IntersectionResult Intersect(const Segment &first, const Segment &second,
                             const GeometryTolerance &tolerance);
WireValidation ValidateWire(const Wire &wire,
                            const GeometryTolerance &tolerance);
RegionValidation ValidateRegion(const Region &region,
                                const GeometryTolerance &tolerance);
bool IsPointInMachiningRegion(const Region &region, Point point,
                              const GeometryTolerance &tolerance);
bool HasBoundaryClearance(const Wire &wire, Point point, double clearance,
                          const GeometryTolerance &tolerance);
OffsetResult OffsetWire(const Wire &wire, OffsetSide side, double distance,
                        const GeometryTolerance &tolerance);
Wire ReverseWire(const Wire &wire);
Wire SelectStableStart(const Wire &wire,
                       const GeometryTolerance &tolerance);
QVector<LoopConnection> PlanLoopConnections(const QVector<Wire> &loops);
QVector<LeadCandidate> PlanLeadCandidates(
    const Wire &wire, const GeometryTolerance &tolerance);

}  // namespace cnext::geometry2d
