#include "../src/strategies/mill/OuterContourChamferStrategy.h"

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

ContourFeature rectangle()
{
    ContourFeature feature;
    feature.subType = QStringLiteral("closed_contour");
    feature.center = QVector3D(0.0f, 0.0f, 0.0f);
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.points = {
        QVector3D(0.0f, 0.0f, 0.0f), QVector3D(20.0f, 0.0f, 0.0f),
        QVector3D(20.0f, 10.0f, 0.0f), QVector3D(0.0f, 10.0f, 0.0f)};
    return feature;
}

ToolEntry verifiedChamferTool()
{
    ToolEntry tool;
    tool.id = 12;
    tool.type = QStringLiteral("chamfer_mill");
    tool.diameter = 10.0;
    tool.fluteLen = 12.0;
    tool.totalLen = 50.0;
    tool.extra.insert(QStringLiteral("includedAngle"), 90.0);
    tool.extra.insert(QStringLiteral("tipRadius"), 0.2);
    return tool;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    OuterContourChamferStrategy strategy;
    StrategyParams params = strategy.defaultParams();
    params.set(QStringLiteral("safeHeight"), 12.0);
    params.set(QStringLiteral("feedHeight"), 3.0);
    params.set(QStringLiteral("chamferWidth"), 1.0);
    params.set(QStringLiteral("chamferAngle"), 45.0);
    params.set(QStringLiteral("toolTipRadius"), 0.2);
    params.set(QStringLiteral("tipCompensation"), 0.083);
    params.set(QStringLiteral("sideClearance"), 0.5);
    params.set(QStringLiteral("stepDown"), 0.5);
    params.set(QStringLiteral("overcut"), 2.0);
    params.set(QStringLiteral("leadLength"), 3.0);

    const ToolpathResult generated =
        strategy.generate(rectangle(), verifiedChamferTool(), params);
    require(generated.ok, "verified convex outer chamfer should generate");
    require(generated.gcode.count(QStringLiteral("OUTER CHAMFER LAYER")) == 3,
            "reverse-calculated chamfer depth must be split by stepDown");
    require(generated.gcode.contains(QStringLiteral("G1 Z-1.083")),
            "final depth must include width/angle depth and tip compensation");
    require(generated.gcode.contains(QStringLiteral("G1 X2.000 Y0.000")),
            "closed overlap must continue along the first edge");
    require(generated.gcode.count(QStringLiteral("G0 Z12.000")) >= 4,
            "every chamfer layer must retract through the safe plane");

    ToolEntry missingGeometry = verifiedChamferTool();
    missingGeometry.extra.clear();
    require(!strategy.generate(rectangle(), missingGeometry, params).ok,
            "missing chamfer tool envelope metadata must block generation");

    ToolEntry undersized = verifiedChamferTool();
    undersized.diameter = 2.8;
    require(!strategy.generate(rectangle(), undersized, params).ok,
            "tool diameter must contain chamfer width and side clearance");

    ContourFeature concave = rectangle();
    concave.points = {
        QVector3D(0.0f, 0.0f, 0.0f), QVector3D(20.0f, 0.0f, 0.0f),
        QVector3D(20.0f, 10.0f, 0.0f), QVector3D(10.0f, 5.0f, 0.0f),
        QVector3D(0.0f, 10.0f, 0.0f)};
    require(!strategy.generate(concave, verifiedChamferTool(), params).ok,
            "unverified concave outer chamfer geometry must be blocked");

    ContourFeature selfIntersecting = rectangle();
    selfIntersecting.points = {
        QVector3D(10.0f, 0.0f, 0.0f), QVector3D(15.9f, 18.1f, 0.0f),
        QVector3D(0.5f, 6.9f, 0.0f), QVector3D(19.5f, 6.9f, 0.0f),
        QVector3D(4.1f, 18.1f, 0.0f)};
    require(!strategy.generate(selfIntersecting, verifiedChamferTool(), params).ok,
            "self-intersecting outer chamfer boundary must be blocked");

    ContourFeature spatial = rectangle();
    spatial.points[2].setZ(1.0f);
    require(!strategy.generate(spatial, verifiedChamferTool(), params).ok,
            "non-planar 3D chamfer geometry must be blocked");

    StrategyParams unsafeHeight = params;
    unsafeHeight.set(QStringLiteral("safeHeight"), 2.0);
    require(!strategy.generate(rectangle(), verifiedChamferTool(), unsafeHeight).ok,
            "safe height below feed plane must block generation");

    return 0;
}
