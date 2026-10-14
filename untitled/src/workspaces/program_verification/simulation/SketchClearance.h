#pragma once

#include <QPointF>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <limits>

// Bounded predicates for the sketch checker only. Never use a nominal sign
// when the outward enclosure contains zero.
namespace SketchClearance {

struct Bound {
    double lo = std::numeric_limits<double>::quiet_NaN();
    double hi = std::numeric_limits<double>::quiet_NaN();
    static Bound exact(double value) { return {value, value}; }
    static Bound parsed(double value) {
        return {std::nextafter(value, -std::numeric_limits<double>::infinity()),
                std::nextafter(value, std::numeric_limits<double>::infinity())};
    }
    bool valid() const { return std::isfinite(lo) && std::isfinite(hi) && lo <= hi; }
};

inline Bound outward(double lo, double hi)
{
    return {std::nextafter(lo, -std::numeric_limits<double>::infinity()),
            std::nextafter(hi, std::numeric_limits<double>::infinity())};
}
inline Bound add(Bound a, Bound b)
{
    if (!a.valid() || !b.valid()) return {};
    if (a.lo == 0 && a.hi == 0) return b;
    if (b.lo == 0 && b.hi == 0) return a;
    return outward(a.lo + b.lo, a.hi + b.hi);
}
inline Bound subtract(Bound a, Bound b) { return add(a, {-b.hi, -b.lo}); }
inline Bound multiply(Bound a, Bound b)
{
    if (!a.valid() || !b.valid()) return {};
    if ((a.lo == 0 && a.hi == 0) || (b.lo == 0 && b.hi == 0)) return Bound::exact(0);
    const double values[] = {a.lo*b.lo, a.lo*b.hi, a.hi*b.lo, a.hi*b.hi};
    return outward(*std::min_element(values, values+4), *std::max_element(values, values+4));
}
inline Bound divide(Bound a, Bound b)
{
    if (!a.valid() || !b.valid() || (b.lo <= 0 && b.hi >= 0)) return {};
    const double values[] = {a.lo/b.lo, a.lo/b.hi, a.hi/b.lo, a.hi/b.hi};
    return outward(*std::min_element(values, values+4), *std::max_element(values, values+4));
}
inline Bound square(Bound a)
{
    if (!a.valid()) return {};
    if (a.lo == 0 && a.hi == 0) return Bound::exact(0);
    const double low = a.lo <= 0 && a.hi >= 0 ? 0 : std::min(a.lo*a.lo, a.hi*a.hi);
    return outward(low, std::max(a.lo*a.lo, a.hi*a.hi));
}
inline int sign(Bound value)
{
    if (!value.valid()) return 0;
    return value.lo > 0 ? 1 : value.hi < 0 ? -1 : 0;
}
inline Bound delta(double a, double b) { return subtract(Bound::exact(a), Bound::exact(b)); }
inline Bound cross(const QPointF &a, const QPointF &b, const QPointF &c)
{
    return subtract(multiply(delta(b.x(), a.x()), delta(c.y(), a.y())),
                    multiply(delta(b.y(), a.y()), delta(c.x(), a.x())));
}
inline bool axisCollinear(const QPointF &a, const QPointF &b, const QPointF &c)
{
    return (a.x() == b.x() && b.x() == c.x()) || (a.y() == b.y() && b.y() == c.y());
}
inline bool exactlyEqual(const QPointF &a, const QPointF &b)
{
    return a.x() == b.x() && a.y() == b.y();
}
inline int convexOrientation(const QVector<QPointF> &polygon)
{
    if (polygon.size() < 3) return 0;
    int orientation = 0;
    for (int i = 0; i < polygon.size(); ++i) {
        const auto &a = polygon[i], &b = polygon[(i+1)%polygon.size()];
        if (exactlyEqual(a,b) || !std::isfinite(a.x()) || !std::isfinite(a.y())) return 0;
        for (const auto &point : polygon) {
            if (exactlyEqual(point,a) || exactlyEqual(point,b) || axisCollinear(a,b,point)) continue;
            const int side = sign(cross(a,b,point));
            if (!side || (orientation && side != orientation)) return 0;
            orientation = side;
        }
    }
    return orientation;
}
inline Bound pointSegmentDistanceSquared(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const auto wx = delta(p.x(),a.x()), wy = delta(p.y(),a.y());
    if (exactlyEqual(a,b)) return add(square(wx),square(wy));
    const auto vx = delta(b.x(),a.x()), vy = delta(b.y(),a.y());
    const auto length = add(square(vx),square(vy));
    if (!length.valid() || length.lo <= 0) return {};
    auto t = divide(add(multiply(wx,vx),multiply(wy,vy)),length);
    if (!t.valid()) return {};
    t.lo = std::max(0.0,std::min(1.0,t.lo));
    t.hi = std::max(0.0,std::min(1.0,t.hi));
    return add(square(subtract(wx,multiply(t,vx))),square(subtract(wy,multiply(t,vy))));
}
inline bool segmentsProvenDisjoint(const QPointF &a, const QPointF &b,
                                    const QPointF &c, const QPointF &d)
{
    if (std::max(a.x(),b.x()) < std::min(c.x(),d.x()) ||
        std::max(c.x(),d.x()) < std::min(a.x(),b.x()) ||
        std::max(a.y(),b.y()) < std::min(c.y(),d.y()) ||
        std::max(c.y(),d.y()) < std::min(a.y(),b.y())) return true;
    const int abC = sign(cross(a,b,c)), abD = sign(cross(a,b,d));
    const int cdA = sign(cross(c,d,a)), cdB = sign(cross(c,d,b));
    return (abC && abC == abD) || (cdA && cdA == cdB);
}

enum class Verdict { Clear, Interference, Uncertain };

inline Verdict compareClearance(Bound distanceSquared, double clearance)
{
    const auto required = square(Bound::exact(clearance));
    if (!distanceSquared.valid() || !required.valid() || clearance <= 0) return Verdict::Uncertain;
    if (distanceSquared.lo > required.hi) return Verdict::Clear;
    if (distanceSquared.hi < required.lo) return Verdict::Interference;
    return Verdict::Uncertain;
}

inline Verdict segmentClearsPolygon(const QPointF &a, const QPointF &b,
                                    const QVector<QPointF> &polygon, double clearance)
{
    const int orientation = convexOrientation(polygon);
    if (!orientation) return Verdict::Uncertain;
    bool outside = false;
    for (int i = 0; i < polygon.size(); ++i)
        outside |= sign(cross(polygon[i],polygon[(i+1)%polygon.size()],a)) == -orientation;
    if (!outside) return Verdict::Uncertain;
    for (int i = 0; i < polygon.size(); ++i) {
        const auto &c = polygon[i], &d = polygon[(i+1)%polygon.size()];
        if (!segmentsProvenDisjoint(a,b,c,d)) return Verdict::Uncertain;
        const Bound distances[] = {pointSegmentDistanceSquared(a,c,d),pointSegmentDistanceSquared(b,c,d),
                                    pointSegmentDistanceSquared(c,a,b),pointSegmentDistanceSquared(d,a,b)};
        Bound minimum = distances[0];
        for (const auto value : distances) {
            if (!value.valid()) return Verdict::Uncertain;
            minimum.lo = std::min(minimum.lo,value.lo);
            minimum.hi = std::min(minimum.hi,value.hi);
        }
        const auto verdict = compareClearance(minimum,clearance);
        if (verdict != Verdict::Clear) return verdict;
    }
    return Verdict::Clear;
}

inline Verdict pointClearsRectangle(const QPointF &point, double minX, double minY,
                                    double maxX, double maxY, double clearance)
{
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
        !std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) || !std::isfinite(maxY) ||
        minX >= maxX || minY >= maxY) return Verdict::Uncertain;
    const auto dx = point.x() < minX ? delta(minX,point.x()) : point.x() > maxX ? delta(point.x(),maxX) : Bound::exact(0);
    const auto dy = point.y() < minY ? delta(minY,point.y()) : point.y() > maxY ? delta(point.y(),maxY) : Bound::exact(0);
    return compareClearance(add(square(dx),square(dy)),clearance);
}

} // namespace SketchClearance
