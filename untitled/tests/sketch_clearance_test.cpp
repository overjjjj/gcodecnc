#include "../src/workspaces/program_verification/simulation/SketchClearance.h"
#include <iostream>
#include <limits>

int main()
{
    using namespace SketchClearance;
    int failures = 0;
    auto check = [&](bool ok, const char *name) {
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
        if (!ok) ++failures;
    };
    const QVector<QPointF> square{{0,0},{10,0},{10,10},{0,10}};
    check(convexOrientation(square) == 1, "original double convex orientation");
    check(segmentClearsPolygon({-2,-2},{12,-2},square,1) == Verdict::Clear,
          "entire outside segment has positive cutter clearance");
    check(segmentClearsPolygon({-2,5},{12,5},square,1) != Verdict::Clear,
          "outside endpoints do not conceal an interior crossing");
    check(segmentClearsPolygon({2,5},{8,5},square,1) != Verdict::Clear,
          "inside segment cannot pass just by being far from polygon edges");
    check(segmentClearsPolygon({-2,-1},{12,-1},square,1) != Verdict::Clear,
          "exact cutter contact is not certified");
    check(segmentClearsPolygon({-2,-1.001},{12,-1.001},square,1) == Verdict::Clear,
          "positive output margin passes without a relaxed clearance tolerance");
    const auto uncertain = cross({0,0},{1e9,1e9},{2e9,std::nextafter(2e9,std::numeric_limits<double>::infinity())});
    check(uncertain.lo <= 0 && uncertain.hi >= 0,
          "cancellation interval encloses zero rather than trusting nominal determinant");
    const auto translated = add(Bound::parsed(.001), Bound::exact(9007199254740992.0));
    check(translated.lo <= 9007199254740992.0 && translated.hi > 9007199254740992.0,
          "large-origin addition retains the unresolved displacement enclosure");
    check(pointClearsRectangle({-2.001,5},0,0,10,10,2) == Verdict::Clear,
          "whole cutter disk outside declared stock");
    check(pointClearsRectangle({-1,5},0,0,10,10,2) != Verdict::Clear,
          "center outside stock is insufficient for cutter disk");
    check(pointClearsRectangle({-2,5},0,0,10,10,2) != Verdict::Clear,
          "stock tangent contact remains unproven");
    check(!multiply(Bound::exact(1e308),Bound::exact(1e308)).valid(),
          "overflow cannot manufacture a clearance proof");
    const double o = 1e12;
    const QPointF a(o,o), b(o+.5,o+.5), p(o+.25,o+.26);
    const QVector<QPointF> nearShortSegment{p,{o-9.75,o+10.26},{o-19.75,o+.26}};
    check(pointSegmentDistanceSquared(p,a,b).hi < .01,
          "large-coordinate short segment is not a fuzzy-equal point");
    check(segmentClearsPolygon(a,b,nearShortSegment,.1) != Verdict::Clear,
          "large-coordinate short segment cannot falsely clear nearby polygon");
    const auto roundedInteger = Bound::parsed(QString("9007199254740993").toDouble());
    check(roundedInteger.lo < 9007199254740992.0 && roundedInteger.hi > 9007199254740992.0,
          "integer-looking parsed double still encloses decimal rounding");
    std::cout << "FAILURES " << failures << '\n';
    return failures ? 1 : 0;
}
