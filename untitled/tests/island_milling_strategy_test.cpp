#include "../src/strategies/mill/IslandMillingStrategy.h"

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
    for (int index = 0; index < 16; ++index) {
        const double angle = 2.0 * std::acos(-1.0) * index / 16.0;
        points.append(QVector3D(float(radius * std::cos(angle)),
                               float(radius * std::sin(angle)), 0.0f));
    }
    return points;
}

ContourFeature circularIslandPocket()
{
    ContourFeature feature;
    feature.subType = QStringLiteral("island_pocket");
    feature.center = QVector3D();
    feature.radius = 20.0;
    feature.depth = 3.0;
    feature.points = circleLoop(20.0);
    feature.islands = {circleLoop(8.0)};
    return feature;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    IslandMillingStrategy strategy;
    ToolEntry tool;
    tool.id = 8;
    tool.type = QStringLiteral("end_mill");
    tool.diameter = 6.0;
    tool.fluteLen = 20.0;
    tool.totalLen = 60.0;
    StrategyParams params = strategy.defaultParams();
    params.set(QStringLiteral("safeHeight"), 10.0);
    params.set(QStringLiteral("plungeHeight"), 3.0);
    params.set(QStringLiteral("referenceHeight"), 0.0);
    params.set(QStringLiteral("depth"), 3.0);
    params.set(QStringLiteral("stepDown"), 1.5);
    params.set(QStringLiteral("stepOver"), 2.0);

    const ToolpathResult generated =
        strategy.generate(circularIslandPocket(), tool, params);
    require(generated.ok && generated.gcode.contains(QStringLiteral("ISLAND LAYER 2")),
            "verified circular island boundaries should generate an island operation");

    ContourFeature multipleIslands = circularIslandPocket();
    multipleIslands.islands.append(circleLoop(3.0));
    require(!strategy.generate(multipleIslands, tool, params).ok,
            "multiple islands must remain blocked until partitioning is verified");

    ContourFeature polygonIsland = circularIslandPocket();
    polygonIsland.islands = {{
        QVector3D(-5.0f, -5.0f, 0.0f), QVector3D(5.0f, -5.0f, 0.0f),
        QVector3D(5.0f, 5.0f, 0.0f), QVector3D(-5.0f, 5.0f, 0.0f)}};
    require(!strategy.generate(polygonIsland, tool, params).ok,
            "unverified polygon island offset must remain blocked");
    return 0;
}
