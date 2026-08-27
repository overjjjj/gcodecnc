#include "../src/strategies/hole/BoringG86Strategy.h"
#include "../src/strategies/hole/HighSpeedPeckDrillingStrategy.h"

#include <QCoreApplication>

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    HoleFeature hole;
    hole.center = QVector3D(10.0f, 20.0f, 0.0f);
    hole.radius = 5.0;
    hole.depth = 12.0;

    ToolEntry drill;
    drill.id = 6;
    drill.type = QStringLiteral("drill");
    drill.diameter = 6.0;

    HighSpeedPeckDrillingStrategy g73;
    const StrategyParams g73Defaults = g73.defaultParams();
    if (!expect(g73.parameterSchema().definition(QStringLiteral("peckDepth")) != nullptr,
                "G73 schema should declare peck depth") ||
        !expect(g73.parameterSchema().definition(QStringLiteral("retractDelta")) != nullptr,
                "G73 schema should declare cycle retract amount")) {
        return 1;
    }
    const ToolpathResult g73Result = g73.generate(hole, drill, g73Defaults);
    if (!expect(g73Result.ok &&
                    g73Result.gcode.contains(QStringLiteral("code=G73")) &&
                    g73Result.gcode.contains(QStringLiteral("retract=0.500")),
                "G73 should emit a traceable high-speed peck marker")) {
        return 1;
    }
    StrategyParams invalidG73 = g73Defaults;
    invalidG73.set(QStringLiteral("retractDelta"), 0.0);
    if (!expect(!g73.generate(hole, drill, invalidG73).ok,
                "G73 should reject a non-positive cycle retract amount")) {
        return 1;
    }

    ToolEntry boringBar;
    boringBar.id = 30;
    boringBar.type = QStringLiteral("boring_bar");
    boringBar.diameter = 8.0;
    BoringG86Strategy g86;
    if (!expect(g86.parameterSchema().definition(QStringLiteral("dwellTime")) != nullptr,
                "G86 schema should declare bottom dwell time")) {
        return 1;
    }
    const ToolpathResult g86Result = g86.generate(hole, boringBar, g86.defaultParams());
    if (!expect(g86Result.ok && g86Result.gcode.contains(QStringLiteral("code=G86")),
                "G86 should emit a boring cycle marker") ||
        !expect(!g86.generate(hole, drill, g86.defaultParams()).ok,
                "G86 should reject tools without explicit boring-bar geometry")) {
        return 1;
    }

    return 0;
}
