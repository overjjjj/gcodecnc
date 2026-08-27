#include "../src/strategies/mill/PlanarSlopeMillingStrategy.h"

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

ContourFeature slopeFeature()
{
    ContourFeature feature;
    feature.subType = QStringLiteral("planar_slope_2d");
    feature.center = QVector3D(0.0f, 0.0f, 0.0f);
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.length = 32.0;
    feature.width = 16.0;
    feature.depth = 24.0 * std::tan(10.0 * std::acos(-1.0) / 180.0);
    feature.points = {
        QVector3D(-16.0f, -8.0f, 0.0f), QVector3D(16.0f, -8.0f, 0.0f),
        QVector3D(16.0f, 8.0f, 0.0f), QVector3D(-16.0f, 8.0f, 0.0f)};
    return feature;
}

ToolEntry ballTool()
{
    ToolEntry tool;
    tool.id = 21;
    tool.type = QStringLiteral("ball_end_mill");
    tool.diameter = 6.0;
    tool.fluteLen = 16.0;
    tool.totalLen = 60.0;
    return tool;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    PlanarSlopeMillingStrategy strategy;
    StrategyParams params = strategy.defaultParams();
    params.set(QStringLiteral("safeHeight"), 15.0);
    params.set(QStringLiteral("plungeHeight"), 3.0);
    params.set(QStringLiteral("slopeAngle"), 10.0);
    params.set(QStringLiteral("slopeLength"), 24.0);
    params.set(QStringLiteral("xyWidth"), 8.0);
    params.set(QStringLiteral("slopeDirection"), 0.0);
    params.set(QStringLiteral("stockToLeave"), 0.0);
    params.set(QStringLiteral("edgeMargin"), 4.0);
    params.set(QStringLiteral("keepEdges"), 1.0);
    params.set(QStringLiteral("compensation"), 0.0);
    params.set(QStringLiteral("layerAllowance"), 2.0);
    params.set(QStringLiteral("stepDown"), 1.0);
    params.set(QStringLiteral("stepOver"), 2.0);

    const ToolpathResult generated =
        strategy.generate(slopeFeature(), ballTool(), params);
    require(generated.ok, "verified rectangular 2D slope should generate");
    require(generated.gcode.count(QStringLiteral("PLANAR SLOPE LAYER")) == 3,
            "layer allowance must descend to the final slope in stepDown increments");
    require(generated.gcode.contains(QStringLiteral("X12.521")) &&
                generated.gcode.contains(QStringLiteral("Z-4.277")),
            "final path must include ball-center compensation at the low slope edge");
    require(generated.gcode.count(QStringLiteral("G0 Z15.000")) >= 10,
            "every slope scan line must return through the safe plane");

    ToolEntry flatTool = ballTool();
    flatTool.type = QStringLiteral("end_mill");
    require(!strategy.generate(slopeFeature(), flatTool, params).ok,
            "flat end mills must not generate the verified ball-envelope slope path");

    ContourFeature wrongDepth = slopeFeature();
    wrongDepth.depth += 1.0;
    require(!strategy.generate(wrongDepth, ballTool(), params).ok,
            "slope angle, usable length, and target depth must agree");

    ContourFeature nonRectangular = slopeFeature();
    nonRectangular.points[2].setX(10.0f);
    require(!strategy.generate(nonRectangular, ballTool(), params).ok,
            "non-rectangular projected slope boundaries must be blocked");

    ContourFeature spatialBoundary = slopeFeature();
    spatialBoundary.points[2].setZ(1.0f);
    require(!strategy.generate(spatialBoundary, ballTool(), params).ok,
            "3D boundary points must not enter the 2D slope strategy");

    StrategyParams unsafe = params;
    unsafe.set(QStringLiteral("safeHeight"), 2.0);
    require(!strategy.generate(slopeFeature(), ballTool(), unsafe).ok,
            "safe height below the feed plane must block slope generation");

    StrategyParams machineComp = params;
    machineComp.set(QStringLiteral("compensation"), 1.0);
    require(!strategy.generate(slopeFeature(), ballTool(), machineComp).ok,
            "unverified machine radius compensation must block slope generation");

    StrategyParams noEdgeProtection = params;
    noEdgeProtection.set(QStringLiteral("keepEdges"), 0.0);
    require(!strategy.generate(slopeFeature(), ballTool(), noEdgeProtection).ok,
            "the restricted slope subset must require edge protection");

    StrategyParams narrowMargin = params;
    narrowMargin.set(QStringLiteral("edgeMargin"), 3.0);
    narrowMargin.set(QStringLiteral("slopeLength"), 26.0);
    narrowMargin.set(QStringLiteral("xyWidth"), 10.0);
    require(!strategy.generate(slopeFeature(), ballTool(), narrowMargin).ok,
            "edge margin must contain the compensated ball envelope");
    return 0;
}
