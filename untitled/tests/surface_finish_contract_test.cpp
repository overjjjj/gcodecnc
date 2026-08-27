#include "../src/strategies/mill/SurfaceFinishStrategy.h"

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
    feature.subType = QStringLiteral("surface_area");
    feature.center = QVector3D();
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.length = 20.0;
    feature.width = 10.0;
    feature.depth = 0.2;
    feature.points = {
        QVector3D(-10.0f, -5.0f, 0.0f), QVector3D(10.0f, -5.0f, 0.0f),
        QVector3D(10.0f, 5.0f, 0.0f), QVector3D(-10.0f, 5.0f, 0.0f)};
    return feature;
}

ToolEntry tool()
{
    ToolEntry entry;
    entry.id = 5;
    entry.type = QStringLiteral("end_mill");
    entry.diameter = 6.0;
    return entry;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    SurfaceFinishStrategy strategy;
    StrategyParams params = strategy.defaultParams();
    params.set(QStringLiteral("safeHeight"), 10.0);
    params.set(QStringLiteral("plungeHeight"), 3.0);
    require(strategy.generate(rectangle(), tool(), params).ok,
            "verified flat rectangular boundary should allow surface finishing");

    ContourFeature missingBoundary = rectangle();
    missingBoundary.points.clear();
    require(!strategy.generate(missingBoundary, tool(), params).ok,
            "surface finish must not invent a fallback rectangle");

    ContourFeature spatial = rectangle();
    spatial.points[2].setZ(1.0f);
    require(!strategy.generate(spatial, tool(), params).ok,
            "fixed-Z surface finish must reject non-planar boundaries");

    ContourFeature sideFace = rectangle();
    sideFace.axis = QVector3D(1.0f, 0.0f, 0.0f);
    require(!strategy.generate(sideFace, tool(), params).ok,
            "fixed-Z surface finish must reject non-front-face geometry");

    StrategyParams unsafe = params;
    unsafe.set(QStringLiteral("safeHeight"), 2.0);
    require(!strategy.generate(rectangle(), tool(), unsafe).ok,
            "surface finish safe height must exceed its feed plane");
    return 0;
}
