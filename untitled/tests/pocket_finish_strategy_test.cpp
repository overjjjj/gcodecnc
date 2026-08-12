#include "../src/gcode/GCodeSafetyValidator.h"
#include "../src/strategies/mill/PocketFinishStrategy.h"

#include <QCoreApplication>
#include <iostream>

static int expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        return 1;
    }
    return 0;
}

static ToolEntry tool()
{
    ToolEntry value;
    value.id = 1;
    value.type = QStringLiteral("end_mill");
    value.diameter = 4.0;
    return value;
}

static bool passesSafetyGate(const QString &toolpath)
{
    return GCodeSafetyValidator::validate(
               QStringLiteral("G17 G40 G49 G80\nG21\nG90\nG54\nG94\n") + toolpath +
               QStringLiteral("M5\nM9\nM30\n"))
        .ok;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    PocketFinishStrategy strategy;
    if (expect(strategy.defaultParams().values.contains(QStringLiteral("sideStockToLeave")),
               "pocket-wall finish should expose a separate side-stock parameter") ||
        expect(strategy.defaultParams().values.contains(QStringLiteral("floorStockToLeave")),
               "pocket-wall finish should expose a separate floor-stock parameter")) {
        return 1;
    }
    ContourFeature rectangle;
    rectangle.subType = QStringLiteral("rectangular_pocket");
    rectangle.center = QVector3D(0.0f, 0.0f, 0.0f);
    rectangle.length = 20.0;
    rectangle.width = 12.0;
    rectangle.depth = 2.0;
    const ToolpathResult rectangularResult = strategy.generate(
        rectangle, tool(), strategy.defaultParams());
    if (expect(rectangularResult.ok, "rectangular pocket finish should generate") ||
        expect(rectangularResult.gcode.contains(
                   QStringLiteral("G0 X0.000 Y0.000\nG0 Z3.000\nG1 Z-1.000 F200\nG40\nG1 X-8.000 Y-4.000 F800")),
               "rectangular pocket-wall finish must plunge at the cleared pocket center before feeding to the wall") ||
        expect(rectangularResult.gcode.contains(QStringLiteral("G40")) &&
                   !rectangularResult.gcode.contains(QStringLiteral("G41")) &&
                   !rectangularResult.gcode.contains(QStringLiteral("G42")),
               "rectangular pocket finish should use CAM-side offset") ||
        expect(passesSafetyGate(rectangularResult.gcode),
               "rectangular pocket finish should pass the G-code safety gate")) {
        return 1;
    }

    ContourFeature rotatedRectangle = rectangle;
    rotatedRectangle.angle = 90.0;
    const ToolpathResult rotatedResult = strategy.generate(
        rotatedRectangle, tool(), strategy.defaultParams());
    if (expect(rotatedResult.ok, "rotated rectangular pocket finish should generate") ||
        expect(rotatedResult.gcode.contains(
                   QStringLiteral("G1 X4.000 Y-8.000 F800\nG1 X4.000 Y8.000 F800\nG1 X-4.000 Y8.000")),
               "pocket-wall finish should rotate its tool-center contour by the feature angle") ||
        expect(passesSafetyGate(rotatedResult.gcode),
               "rotated rectangular pocket finish should pass the G-code safety gate")) {
        return 1;
    }

    StrategyParams splitStockParams = strategy.defaultParams();
    splitStockParams.set(QStringLiteral("sideStockToLeave"), 1.0);
    splitStockParams.set(QStringLiteral("floorStockToLeave"), 0.5);
    const ToolpathResult splitStockResult = strategy.generate(
        rectangle, tool(), splitStockParams);
    if (expect(splitStockResult.ok, "pocket-wall finish should accept separate side and floor stock") ||
        expect(splitStockResult.gcode.contains(
                   QStringLiteral("; Pocket wall finish Z=-1.500")),
               "pocket-wall finish should stop above the floor by the confirmed floor stock") ||
        expect(splitStockResult.gcode.contains(
                   QStringLiteral("G1 X-7.000 Y-3.000 F800")),
               "pocket-wall finish should apply side stock only to the XY tool-center boundary")) {
        return 1;
    }

    StrategyParams excessiveFloorStock = strategy.defaultParams();
    excessiveFloorStock.set(QStringLiteral("floorStockToLeave"), rectangle.depth);
    if (expect(!strategy.generate(rectangle, tool(), excessiveFloorStock).ok,
               "pocket-wall finish must reject floor stock that consumes the full depth")) {
        return 1;
    }

    StrategyParams legacyStockParams = strategy.defaultParams();
    legacyStockParams.values.remove(QStringLiteral("sideStockToLeave"));
    legacyStockParams.values.remove(QStringLiteral("floorStockToLeave"));
    legacyStockParams.set(QStringLiteral("stockToLeave"), 0.5);
    const ToolpathResult legacyStockResult = strategy.generate(
        rectangle, tool(), legacyStockParams);
    if (expect(legacyStockResult.ok, "legacy pocket-wall stock should remain loadable") ||
        expect(legacyStockResult.gcode.contains(QStringLiteral("Z=-1.500")) &&
                   legacyStockResult.gcode.contains(QStringLiteral("X-7.500 Y-3.500")),
               "legacy stock should fall back to both side and floor stock")) {
        return 1;
    }

    ContourFeature circle = rectangle;
    circle.subType = QStringLiteral("circular_pocket");
    circle.radius = 10.0;
    const ToolpathResult circularResult = strategy.generate(circle, tool(), strategy.defaultParams());
    if (expect(circularResult.ok, "circular pocket finish should generate") ||
        expect(circularResult.gcode.contains(
                   QStringLiteral("G0 X0.000 Y0.000\nG0 Z3.000\nG1 Z-1.000 F200\nG40\nG1 X8.000 Y0.000 F800")),
               "circular pocket-wall finish must plunge at the cleared pocket center before feeding to the wall") ||
        expect(circularResult.gcode.contains(QStringLiteral("G2 ")),
               "circular pocket finish should contain a circular wall pass") ||
        expect(passesSafetyGate(circularResult.gcode),
               "circular pocket finish should pass the G-code safety gate")) {
        return 1;
    }

    ContourFeature irregular = rectangle;
    irregular.subType = QStringLiteral("irregular_pocket");
    if (expect(!strategy.generate(irregular, tool(), strategy.defaultParams()).ok,
               "irregular pocket finish must remain unavailable until separately validated")) {
        return 1;
    }
    return 0;
}
