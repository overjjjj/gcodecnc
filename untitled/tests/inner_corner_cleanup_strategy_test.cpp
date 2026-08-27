#include "../src/strategies/mill/InnerCornerCleanupStrategy.h"

#include <QCoreApplication>

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

ContourFeature rectangularPocket()
{
    ContourFeature feature;
    feature.subType = QStringLiteral("rectangular_pocket");
    feature.depth = 6.0;
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.region = FaceRegion::Front;
    feature.points = {
        QVector3D(0.0f, 0.0f, 0.0f), QVector3D(30.0f, 0.0f, 0.0f),
        QVector3D(30.0f, 20.0f, 0.0f), QVector3D(0.0f, 20.0f, 0.0f)};
    return feature;
}

ToolEntry cleanupTool()
{
    ToolEntry tool;
    tool.id = 4;
    tool.type = QStringLiteral("end_mill");
    tool.diameter = 4.0;
    tool.fluteLen = 20.0;
    tool.totalLen = 60.0;
    return tool;
}

StrategyParams safeParams(const InnerCornerCleanupStrategy &strategy)
{
    StrategyParams params = strategy.defaultParams();
    params.set(QStringLiteral("safeHeight"), 12.0);
    params.set(QStringLiteral("plungeHeight"), 3.0);
    params.set(QStringLiteral("referenceHeight"), 0.0);
    params.set(QStringLiteral("depth"), 6.0);
    params.set(QStringLiteral("stepDown"), 2.0);
    params.set(QStringLiteral("stepOver"), 1.0);
    params.set(QStringLiteral("previousToolDiameter"), 10.0);
    params.set(QStringLiteral("cornerTolerance"), 0.05);
    params.set(QStringLiteral("safetyDistance"), 0.5);
    params.set(QStringLiteral("stockToLeave"), 0.0);
    return params;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    InnerCornerCleanupStrategy strategy;
    const ToolpathResult generated = strategy.generate(
        rectangularPocket(), cleanupTool(), safeParams(strategy));
    require(generated.ok, "a bound rectangular-pocket cleanup should generate");
    require(generated.gcode.count(QStringLiteral("INNER CORNER LAYER")) == 3,
            "six millimeters at two millimeters per layer must create three layers");
    require(generated.gcode.count(QStringLiteral("G0 Z12.000")) >= 12,
            "every corner pass must retract to the safe plane");
    require(generated.gcode.count(QStringLiteral("G0 Z3.000")) == 12,
            "every corner pass must approach through the configured plunge plane");
    require(generated.gcode.count(QStringLiteral("INNER CORNER STEP")) == 60,
            "stepOver must divide each diagonal residual pass into bounded advances");
    require(generated.gcode.contains(QStringLiteral("X2.050 Y2.050")) &&
                generated.gcode.contains(QStringLiteral("X27.950 Y17.950")),
            "tool centers must stop at tool radius plus tolerance inside the boundary");
    require(!generated.gcode.contains(QStringLiteral("X0.000")) &&
                !generated.gcode.contains(QStringLiteral("Y0.000")),
            "the cleanup tool center must never enter the pocket boundary");

    StrategyParams oversizedStep = safeParams(strategy);
    oversizedStep.set(QStringLiteral("stepOver"), 4.1);
    require(!strategy.generate(rectangularPocket(), cleanupTool(), oversizedStep).ok,
            "stepOver larger than the cleanup-tool diameter must be rejected");

    StrategyParams widerStep = safeParams(strategy);
    widerStep.set(QStringLiteral("stepOver"), 2.0);
    const ToolpathResult widerStepPath = strategy.generate(
        rectangularPocket(), cleanupTool(), widerStep);
    require(widerStepPath.ok &&
                widerStepPath.gcode.count(QStringLiteral("INNER CORNER STEP")) == 36,
            "changing stepOver must change the actual cleanup path");

    StrategyParams unsafeHeight = safeParams(strategy);
    unsafeHeight.set(QStringLiteral("safeHeight"), 2.0);
    require(!strategy.generate(rectangularPocket(), cleanupTool(), unsafeHeight).ok,
            "safe height below plunge height must block generation");

    StrategyParams largerCleanupTool = safeParams(strategy);
    largerCleanupTool.set(QStringLiteral("previousToolDiameter"), 3.0);
    require(!strategy.generate(rectangularPocket(), cleanupTool(), largerCleanupTool).ok,
            "the cleanup tool must be smaller than the previous tool");

    ToolEntry shortTool = cleanupTool();
    shortTool.fluteLen = 5.0;
    require(!strategy.generate(rectangularPocket(), shortTool, safeParams(strategy)).ok,
            "cleanup depth beyond flute length must block generation");

    ContourFeature openBoundary = rectangularPocket();
    openBoundary.points.removeLast();
    require(!strategy.generate(openBoundary, cleanupTool(), safeParams(strategy)).ok,
            "an incomplete rectangular boundary must block generation");

    ContourFeature islandPocket = rectangularPocket();
    islandPocket.islands.append({
        QVector3D(10.0f, 8.0f, 0.0f), QVector3D(12.0f, 8.0f, 0.0f),
        QVector3D(12.0f, 10.0f, 0.0f), QVector3D(10.0f, 10.0f, 0.0f)});
    require(!strategy.generate(islandPocket, cleanupTool(), safeParams(strategy)).ok,
            "the initial cleanup subset must reject islands");
    return 0;
}
