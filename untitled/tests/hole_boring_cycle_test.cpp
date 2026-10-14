#include "../src/strategies/hole/BoringCycleStrategy.h"
#include "../src/postprocessor/FanucPostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"

#include <QCoreApplication>
#include <iostream>

static bool expect(bool value, const char *message)
{
    if (!value) std::cerr << message << '\n';
    return value;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    HoleFeature hole;
    hole.center = QVector3D(12.0f, 24.0f, -10.0f);
    hole.radius = 5.0;
    hole.depth = 20.0;
    HoleFeature secondHole = hole;
    secondHole.center.setX(32.0f);

    ToolEntry bar;
    bar.id = 17;
    bar.type = QStringLiteral("boring_bar");
    bar.diameter = 8.0;
    bar.fluteLen = 30.0;
    bar.totalLen = 70.0;

    BoringCycleStrategy g76(QStringLiteral("hole_g76"), QStringLiteral("G76"), false);
    BoringCycleStrategy g86(QStringLiteral("hole_g86"), QStringLiteral("G86"), true);
    auto p76 = g76.defaultParams();
    p76.set(QStringLiteral("depth"), 5.0);
    p76.set(QStringLiteral("boringOffset"), 0.1);
    auto p86 = g86.defaultParams();
    p86.set(QStringLiteral("depth"), 5.0);

    const ToolpathResult g76Path = g76.generate(hole, bar, p76);
    const ToolpathResult g86Path = g86.generate(QVector<HoleFeature>{hole, secondHole}, bar, p86);
    ToolEntry missingReachBar = bar;
    missingReachBar.fluteLen = 0.0;
    missingReachBar.totalLen = 0.0;
    ToolEntry shortFluteBar = bar;
    shortFluteBar.fluteLen = 4.0;
    ToolEntry shortOverallBar = bar;
    shortOverallBar.totalLen = 5.0;
    const ToolpathResult missingReachPath = g76.generate(hole, missingReachBar, p76);
    const ToolpathResult shortFlutePath = g76.generate(hole, shortFluteBar, p76);
    const ToolpathResult shortOverallPath = g86.generate(hole, shortOverallBar, p86);
    if (!expect(g76Path.ok && g76Path.gcode.contains(QStringLiteral("code=G76")) &&
                    g76Path.gcode.contains(QStringLiteral("q=0.100")),
                "G76 must emit an explicit positive radial offset Q") ||
        !expect(g76Path.gcode.contains(QStringLiteral("M8")) &&
                    g86Path.gcode.contains(QStringLiteral("M8")),
                "enabled flood coolant must be emitted before boring cycles") ||
        !expect(g86Path.ok && g86Path.gcode.count(QStringLiteral("code=G86")) == 2 &&
                    !g86Path.gcode.contains(QStringLiteral("M5")),
                "G86 cycle semantics must not insert a raw M5 between holes") ||
        !expect(g76Path.gcode.contains(QStringLiteral("z=-5.000")) &&
                    g86Path.gcode.contains(QStringLiteral("z=-5.000")),
                "boring cycles must consume confirmed depth") ||
        !expect(!missingReachPath.ok && !shortFlutePath.ok && !shortOverallPath.ok,
                "boring cycles must reject missing or insufficient flute/overall reach")) {
        return 1;
    }

    FanucPostProcessor fanuc;
    PostProcessorOptions options;
    options.safeStartBlocks = QStringList{QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
                                          QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    const QString fanuc76 = fanuc.wrapGCode(g76Path.gcode.split('\n'), options);
    const QString fanuc86 = fanuc.wrapGCode(g86Path.gcode.split('\n'), options);
    if (!expect(fanuc76.contains(QStringLiteral("G98 G76 Z-5.000 R3.000 Q0.100 F40.000")),
                "Fanuc G76 must preserve Q offset and depth") ||
        !expect(fanuc86.contains(QStringLiteral("G98 G86 Z-5.000 R3.000 F40.000 X12.000 Y24.000")) &&
                    fanuc86.contains(QStringLiteral("X32.000 Y24.000")) &&
                    fanuc86.indexOf(QStringLiteral("M5")) > fanuc86.indexOf(QStringLiteral("X32.000 Y24.000")) &&
                    !fanuc86.mid(fanuc86.indexOf(QStringLiteral("G98 G86")),
                                 fanuc86.indexOf(QStringLiteral("X32.000 Y24.000")) - fanuc86.indexOf(QStringLiteral("G98 G86")))
                         .contains(QStringLiteral("M5")),
                "Fanuc two-hole G86 must keep the cycle active through the second hole and stop only at shutdown")) {
        return 1;
    }

    SiemensPostProcessor siemens;
    const QString siemens76 = siemens.wrapGCode(g76Path.gcode.split('\n'), options);
    const QString siemens86 = siemens.wrapGCode(g86Path.gcode.split('\n'), options);
    if (!expect(!siemens76.contains(QStringLiteral("G76")) && !siemens86.contains(QStringLiteral("G86")),
                "Siemens must not receive unverified Fanuc G76/G86 syntax")) {
        return 1;
    }
    std::cout << "G76/G86 boring cycle semantics passed\n";
    return 0;
}
