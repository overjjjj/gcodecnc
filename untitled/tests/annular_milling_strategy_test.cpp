#include "../src/strategies/mill/AnnularMillingStrategy.h"

#include <QCoreApplication>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

QVector<QVector3D> circleLoop(double radius)
{
    QVector<QVector3D> points;
    constexpr int kSegments = 16;
    for (int index = 0; index < kSegments; ++index) {
        const double angle = 2.0 * std::acos(-1.0) * index / kSegments;
        points.append(QVector3D(float(radius * std::cos(angle)),
                               float(radius * std::sin(angle)), 0.0f));
    }
    return points;
}

ContourFeature annulus()
{
    ContourFeature feature;
    feature.subType = QStringLiteral("annular_pocket");
    feature.center = QVector3D(0.0f, 0.0f, 0.0f);
    feature.radius = 20.0;
    feature.depth = 4.0;
    feature.points = circleLoop(feature.radius);
    feature.islands = {circleLoop(8.0)};
    return feature;
}

ToolEntry tool()
{
    ToolEntry entry;
    entry.id = 7;
    entry.type = QStringLiteral("end_mill");
    entry.diameter = 6.0;
    entry.fluteLen = 20.0;
    entry.totalLen = 60.0;
    return entry;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    AnnularMillingStrategy strategy;
    StrategyParams params = strategy.defaultParams();
    params.set(QStringLiteral("safeHeight"), 12.0);
    params.set(QStringLiteral("plungeHeight"), 3.0);
    params.set(QStringLiteral("referenceHeight"), 0.0);
    params.set(QStringLiteral("depth"), 4.0);
    params.set(QStringLiteral("stepOver"), 3.0);
    params.set(QStringLiteral("stepDown"), 2.0);

    const ToolpathResult result = strategy.generate(annulus(), tool(), params);
    require(result.ok && !result.gcode.isEmpty(),
            "a verified concentric annulus should generate");
    require(result.gcode.count(QStringLiteral("ANNULAR LAYER")) == 2,
            "stepDown should create two explicit depth layers");
    require(result.gcode.count(QStringLiteral("G0 Z12.000")) >= 3,
            "every layer must begin and end through the safe plane");
    require(result.gcode.contains(QStringLiteral("G3")) &&
                result.gcode.contains(QStringLiteral("Z-2.000")) &&
                result.gcode.contains(QStringLiteral("Z-4.000")),
            "entry must descend helically to every requested depth");

    ContourFeature missingIsland = annulus();
    missingIsland.islands.clear();
    require(!strategy.generate(missingIsland, tool(), params).ok,
            "annular milling must reject a missing island boundary");

    ContourFeature eccentric = annulus();
    for (QVector3D &point : eccentric.islands[0]) {
        point.setX(point.x() + 2.0f);
    }
    require(!strategy.generate(eccentric, tool(), params).ok,
            "annular milling must reject an eccentric island");

    ContourFeature unverifiedOuter = annulus();
    unverifiedOuter.points = {
        QVector3D(-20.0f, -5.0f, 0.0f), QVector3D(20.0f, -5.0f, 0.0f),
        QVector3D(20.0f, 5.0f, 0.0f), QVector3D(-20.0f, 5.0f, 0.0f)};
    require(!strategy.generate(unverifiedOuter, tool(), params).ok,
            "annular milling must reject an unverified outer boundary");

    StrategyParams unsafe = params;
    unsafe.set(QStringLiteral("safeHeight"), 2.0);
    unsafe.set(QStringLiteral("plungeHeight"), 3.0);
    require(!strategy.generate(annulus(), tool(), unsafe).ok,
            "safe height below plunge height must block generation");

    ToolEntry oversized = tool();
    oversized.diameter = 13.0;
    require(!strategy.generate(annulus(), oversized, params).ok,
            "tool envelope that cannot fit the annulus must be blocked");

    return 0;
}
