#include "../src/strategies/hole/HoleCircularMillingStrategy.h"
#include "../src/strategies/hole/HoleStrategyUtils.h"
#include "../src/strategies/hole/PeckDrillingStrategy.h"
#include "../src/strategies/hole/SpotDrillingStrategy.h"
#include "../src/strategies/hole/DeepHoleDrillingStrategy.h"
#include "../src/strategies/hole/ReamingStrategy.h"
#include "../src/strategies/hole/TappingStrategy.h"
#include "../src/strategies/hole/ChamferStrategy.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
bool markerDepthsEqual(const QString &gcode, double depth)
{
    auto matches=QRegularExpression(QStringLiteral("(?:^|\\s)z=([^\\s]+)")).globalMatch(gcode);
    bool found=false;
    while(matches.hasNext()){
        bool ok=false;const double value=matches.next().captured(1).toDouble(&ok);
        if(!ok || std::abs(value-depth)>1e-9)return false;
        found=true;
    }
    return found;
}

bool expect(bool condition, const char *message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    HoleFeature hole;
    hole.center = QVector3D(0.0f, 0.0f, -10.0f);
    hole.radius = 5.0;
    hole.depth = 20.0;
    hole.region = FaceRegion::Front;
    HoleFeature secondHole = hole;
    secondHole.center.setX(20.0f);

    ToolEntry endMill;
    endMill.id = 91;
    endMill.type = QStringLiteral("end_mill");
    endMill.diameter = 4.0;
    endMill.fluteLen = 8.0;
    endMill.totalLen = 50.0;

    HoleCircularMillingStrategy circularMill;
    StrategyParams params = circularMill.defaultParams();
    params.set(QStringLiteral("depth"), 5.0);
    params.set(QStringLiteral("stepDown"), 5.0);

    const ToolpathResult result = circularMill.generate(hole, endMill, params);
    if (!expect(result.ok, "valid circular-hole milling parameters must generate a path") ||
        !expect(result.gcode.contains(QStringLiteral("Z-5.000")),
                "explicit circular-hole depth 5 must reach entryZ - 5") ||
        !expect(!result.gcode.contains(QStringLiteral("Z-20.000")),
                "recognized model depth 20 must not replace explicit depth 5") ||
        !expect(holeCuttingDepth(circularMill.id(), hole, params) == 5.0,
                "tool-reach validation must consume the same explicit depth as the path")) {
        return 1;
    }

    ToolEntry drill = endMill;
    drill.id = 92;
    drill.type = QStringLiteral("drill");
    drill.diameter = 4.0;
    ToolEntry spotDrill = drill;
    spotDrill.id = 96;
    spotDrill.type = QStringLiteral("spot_drill");
    ToolEntry reamer = endMill;
    reamer.id = 93;
    reamer.type = QStringLiteral("reamer");
    reamer.diameter = 10.0;
    ToolEntry tap = reamer;
    tap.id = 94;
    tap.type = QStringLiteral("tap");
    tap.pitch = 1.0;
    ToolEntry chamferMill = endMill;
    chamferMill.id = 95;
    chamferMill.type = QStringLiteral("chamfer_mill");
    chamferMill.pointAngle = 90.0;
    chamferMill.extra.insert(QStringLiteral("angle"), 90.0);

    SpotDrillingStrategy spot;
    PeckDrillingStrategy peck;
    DeepHoleDrillingStrategy deepHole;
    ReamingStrategy reaming;
    TappingStrategy tapping;
    ChamferStrategy chamfer;
    struct CycleCase {
        const StrategyBase *strategy;
        const ToolEntry *tool;
        QString marker;
    };
    const QVector<CycleCase> fixedCycles{
        {&spot, &spotDrill, QStringLiteral("code=G81")},
        {&peck, &drill, QStringLiteral("code=G83")},
        {&deepHole, &drill, QStringLiteral("code=G83")},
        {&reaming, &reamer, QStringLiteral("code=G85")},
        {&tapping, &tap, QStringLiteral("code=G84")},
    };
    for (const CycleCase &cycle : fixedCycles) {
        StrategyParams cycleParams = cycle.strategy->defaultParams();
        cycleParams.set(QStringLiteral("depth"), 5.0);
        cycleParams.set(QStringLiteral("peckDepth"), 2.0);
        cycleParams.set(QStringLiteral("safeHeight"), 25.0);
        cycleParams.set(QStringLiteral("feedHeight"), 3.0);
        cycleParams.set(QStringLiteral("spindleSpeed"), 600.0);
        cycleParams.set(QStringLiteral("feedRate"), 40.0);
        cycleParams.set(QStringLiteral("coolantMode"), 1.0);
        const ToolpathResult cycleResult = cycle.strategy->generate(
            QVector<HoleFeature>{hole, secondHole}, *cycle.tool, cycleParams);
        if (!expect(cycleResult.ok, "valid two-hole fixed-cycle parameters must generate a path") ||
            !expect(cycleResult.gcode.count(cycle.marker) == 2,
                    "two-hole fixed cycle must preserve both hole calls") ||
            !expect(cycleResult.gcode.count(QStringLiteral("T%1 M6").arg(cycle.tool->id)) == 1,
                    "two-hole fixed cycle must use one batched tool change") ||
            !expect(markerDepthsEqual(cycleResult.gcode,-5),
                    "explicit fixed-cycle depth 5 must override model depth 20") ||
            !expect(cycleResult.gcode.contains(QStringLiteral("M8")),
                    "enabled flood coolant must be emitted before fixed-cycle cutting") ||
            !expect(cycleResult.gcode.endsWith(QStringLiteral("G0 Z25.000000\n")),
                    "fixed-cycle strategy must end with an explicit rapid safe-height move")) {
            return 1;
        }
    }

    hole.subType = secondHole.subType = "through_hole";
    chamferMill.extra.insert(QStringLiteral("tipDiameter"), 0.0);
    StrategyParams chamferParams = chamfer.defaultParams();
    chamferParams.set(QStringLiteral("safeHeight"), 25.0);
    chamferParams.set(QStringLiteral("feedHeight"), 3.0);
    chamferParams.set(QStringLiteral("spindleSpeed"), 600.0);
    chamferParams.set(QStringLiteral("feedRate"), 40.0);
    chamferParams.set(QStringLiteral("coolantMode"), 1.0);
    const ToolpathResult chamferResult = static_cast<const StrategyBase &>(chamfer).generate(
        QVector<HoleFeature>{hole, secondHole}, chamferMill, chamferParams);
    if (!expect(chamferResult.ok && chamferResult.gcode.count(QStringLiteral("; Chamfer hole")) == 2,
                "two-hole chamfer must generate both circumferential paths") ||
        !expect(chamferResult.gcode.count(QStringLiteral("T95 M6")) == 1,
                "two-hole chamfer must use one batched tool change") ||
        !expect(chamferResult.gcode.contains(QStringLiteral("Z-2.000")),
                "explicit D4 sharp 90-degree cone tip must reach two millimetres below the hole mouth") ||
        !expect(chamferResult.gcode.contains(QStringLiteral("M8")),
                "enabled flood coolant must be emitted before chamfer cutting")) {
        return 1;
    }

    StrategyParams axialChamferParams = chamferParams;
    axialChamferParams.set(QStringLiteral("chamferMode"), 1.0);
    axialChamferParams.set(QStringLiteral("axialDepth"), 2.0);
    const ToolpathResult axialChamferResult = static_cast<const StrategyBase &>(chamfer).generate(
        QVector<HoleFeature>{hole, secondHole}, chamferMill, axialChamferParams);
    if (!expect(axialChamferResult.ok,
                "valid axial countersink parameters must generate a path") ||
        !expect(axialChamferResult.gcode.count(QStringLiteral("code=G81")) == 2,
                "two-hole axial countersink must preserve both G81 calls") ||
        !expect(!axialChamferResult.gcode.contains(QStringLiteral("G2 ")),
                "axial countersink mode must not emit the circumferential G2 path") ||
        !expect(markerDepthsEqual(axialChamferResult.gcode,-2),
                "operator-confirmed axial depth 2 must target entryZ minus 2") ||
        !expect(axialChamferResult.gcode.count(QStringLiteral("T95 M6")) == 1,
                "two-hole axial countersink must use one batched tool change") ||
        !expect(axialChamferResult.gcode.contains(QStringLiteral("M8")) &&
                    axialChamferResult.gcode.endsWith(QStringLiteral("G0 Z25.000000\n")),
                "axial countersink must retain coolant and explicit safe retract") ||
        !expect(holeCuttingDepth(chamfer.id(), hole, axialChamferParams) == 2.0,
                "axial countersink reach validation must consume the confirmed axial depth")) {
        return 1;
    }

    for (const auto &invalidValue : QList<QPair<QString, double>>{
             {QStringLiteral("chamferMode"), 2.0},
             {QStringLiteral("chamferMode"), 0.5},
             {QStringLiteral("axialDepth"), 0.0},
             {QStringLiteral("axialDepth"), std::numeric_limits<double>::quiet_NaN()}}) {
        StrategyParams invalid = axialChamferParams;
        invalid.set(invalidValue.first, invalidValue.second);
        const ToolpathResult rejected = chamfer.generate(hole, chamferMill, invalid);
        if (!expect(!rejected.ok && rejected.gcode.isEmpty(),
                    "invalid axial countersink mode/depth must be rejected atomically")) {
            return 1;
        }
    }
    ToolEntry missingAngle = chamferMill;
    missingAngle.extra.remove(QStringLiteral("angle"));
    missingAngle.pointAngle = std::numeric_limits<double>::quiet_NaN();
    if (!expect(!chamfer.generate(hole, missingAngle, axialChamferParams).ok,
                "axial countersink requires explicit finite tool included-angle metadata")) {
        return 1;
    }

    StrategyParams circularBatchParams = params;
    circularBatchParams.set(QStringLiteral("safeHeight"), 25.0);
    circularBatchParams.set(QStringLiteral("coolantMode"), 1.0);
    const ToolpathResult circularBatch = circularMill.generate(
        QVector<HoleFeature>{hole, secondHole}, endMill, circularBatchParams);
    if (!expect(circularBatch.ok &&
                    circularBatch.gcode.count(QStringLiteral("; Center-start spiral mill")) == 2,
                "two-hole circular milling must generate both hole paths") ||
        !expect(circularBatch.gcode.count(QStringLiteral("T91 M6")) == 1,
                "two-hole circular milling must use one batched tool change") ||
        !expect(circularBatch.gcode.contains(QStringLiteral("M8")),
                "enabled flood coolant must be emitted before circular-hole cutting")) {
        return 1;
    }

    std::cout << "Hole completion strategy depth, batching and coolant contracts passed\n";
    return 0;
}
