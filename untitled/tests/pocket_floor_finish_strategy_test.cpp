#include "../src/gcode/GCodeSafetyValidator.h"
#include "../src/strategies/mill/PocketFloorFinishStrategy.h"

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

    PocketFloorFinishStrategy strategy;
    ContourFeature rectangle;
    rectangle.subType = QStringLiteral("rectangular_pocket");
    rectangle.center = QVector3D(0.0f, 0.0f, 0.0f);
    rectangle.length = 20.0;
    rectangle.width = 12.0;
    rectangle.depth = 2.0;
    const ToolpathResult rectangularResult = strategy.generate(
        rectangle, tool(), strategy.defaultParams());
    if (expect(rectangularResult.ok, "rectangular pocket floor finish should generate") ||
        expect(rectangularResult.gcode.contains(QStringLiteral("; Pocket floor finish")),
               "rectangular floor finish should identify its operation") ||
        expect(passesSafetyGate(rectangularResult.gcode),
               "rectangular pocket floor finish should pass the G-code safety gate")) {
        return 1;
    }

    ContourFeature circle = rectangle;
    circle.subType = QStringLiteral("circular_pocket");
    circle.radius = 10.0;
    const ToolpathResult circularResult = strategy.generate(circle, tool(), strategy.defaultParams());
    if (expect(circularResult.ok, "circular pocket floor finish should generate") ||
        expect(passesSafetyGate(circularResult.gcode),
               "circular pocket floor finish should pass the G-code safety gate")) {
        return 1;
    }

    ContourFeature irregular = rectangle;
    irregular.subType = QStringLiteral("irregular_pocket");
    if (expect(!strategy.generate(irregular, tool(), strategy.defaultParams()).ok,
               "irregular pocket floor finish must remain unavailable until separately validated")) {
        return 1;
    }
    return 0;
}
