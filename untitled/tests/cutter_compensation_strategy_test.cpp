#include "../src/strategies/mill/BlindSlotMillingStrategy.h"
#include "../src/strategies/mill/CircleMillingStrategy.h"
#include "../src/strategies/mill/ClosedContourMillingStrategy.h"
#include "../src/strategies/mill/ContourFinishStrategy.h"
#include "../src/strategies/mill/OpenContourMillingStrategy.h"
#include "../src/strategies/mill/SlotMillingStrategy.h"
#include "../src/gcode/GCodeSafetyValidator.h"

#include <QCoreApplication>
#include <QStringList>
#include <iostream>

static int expect(bool condition, const QString &message)
{
    if (!condition) {
        std::cerr << message.toStdString() << "\n";
        return 1;
    }
    return 0;
}

static bool hasLinearXY(const QString &line)
{
    return line.contains(QStringLiteral("G1 ")) &&
           line.contains(QLatin1Char('X')) &&
           line.contains(QLatin1Char('Y'));
}

static int expectSafeCompensationTransitions(const QString &name, const QString &gcode)
{
    bool compensationActive = false;
    const QStringList lines = gcode.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.contains(QStringLiteral("G41")) || line.contains(QStringLiteral("G42"))) {
            if (expect(hasLinearXY(line),
                       QStringLiteral("%1 line %2 must activate compensation on G1 X/Y: %3")
                           .arg(name)
                           .arg(i + 1)
                           .arg(line))) {
                return 1;
            }
            compensationActive = true;
        }
        if (line.contains(QStringLiteral("G40")) && compensationActive) {
            if (expect(hasLinearXY(line),
                       QStringLiteral("%1 line %2 must cancel compensation on G1 X/Y: %3")
                           .arg(name)
                           .arg(i + 1)
                           .arg(line))) {
                return 1;
            }
            compensationActive = false;
        }
    }
    return expect(!compensationActive,
                  QStringLiteral("%1 must cancel cutter compensation").arg(name));
}

static int expectPassesSafetyGate(const QString &name, const QString &strategyGCode)
{
    const QString program =
        QStringLiteral("G17 G40 G49 G80\n"
                       "G21\n"
                       "G90\n"
                       "G54\n"
                       "G94\n") +
        strategyGCode +
        QStringLiteral("M5\nM9\nM30\n");
    const GCodeSafetyReport report = GCodeSafetyValidator::validate(program);
    return expect(report.ok,
                  QStringLiteral("%1 should pass the G-code safety gate: %2")
                      .arg(name, report.messages.join(QStringLiteral(" | "))));
}

static ToolEntry testTool()
{
    ToolEntry tool;
    tool.id = 1;
    tool.type = QStringLiteral("end_mill");
    tool.diameter = 2.0;
    return tool;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const ToolEntry tool = testTool();

    ContourFeature circle;
    circle.subType = QStringLiteral("circle");
    circle.center = QVector3D(0.0f, 0.0f, 0.0f);
    circle.radius = 10.0;
    circle.depth = 1.0;

    CircleMillingStrategy circleStrategy;
    const ToolpathResult circleResult =
        circleStrategy.generate(circle, tool, circleStrategy.defaultParams());
    if (expect(circleResult.ok, QStringLiteral("circle milling generation should succeed")) ||
        expectSafeCompensationTransitions(QStringLiteral("circle milling"), circleResult.gcode) ||
        expectPassesSafetyGate(QStringLiteral("circle milling"), circleResult.gcode)) {
        return 1;
    }

    ContourFeature openContour;
    openContour.subType = QStringLiteral("open_contour");
    openContour.center = QVector3D(0.0f, 0.0f, 0.0f);
    openContour.depth = 1.0;
    openContour.points = {QVector3D(0.0f, 0.0f, 0.0f),
                          QVector3D(10.0f, 0.0f, 0.0f),
                          QVector3D(10.0f, 5.0f, 0.0f)};

    ContourFinishStrategy contourFinish;
    const ToolpathResult contourFinishResult =
        contourFinish.generate(openContour, tool, contourFinish.defaultParams());
    if (expect(contourFinishResult.ok, QStringLiteral("contour finish generation should succeed")) ||
        expectSafeCompensationTransitions(QStringLiteral("contour finish"), contourFinishResult.gcode) ||
        expectPassesSafetyGate(QStringLiteral("contour finish"), contourFinishResult.gcode)) {
        return 1;
    }

    OpenContourMillingStrategy openStrategy;
    const ToolpathResult openResult =
        openStrategy.generate(openContour, tool, openStrategy.defaultParams());
    if (expect(openResult.ok, QStringLiteral("open contour generation should succeed")) ||
        expectSafeCompensationTransitions(QStringLiteral("open contour"), openResult.gcode) ||
        expectPassesSafetyGate(QStringLiteral("open contour"), openResult.gcode)) {
        return 1;
    }

    ContourFeature closedContour = openContour;
    closedContour.subType = QStringLiteral("closed_contour");
    closedContour.points = {QVector3D(0.0f, 0.0f, 0.0f),
                            QVector3D(10.0f, 0.0f, 0.0f),
                            QVector3D(10.0f, 5.0f, 0.0f),
                            QVector3D(0.0f, 5.0f, 0.0f)};

    ClosedContourMillingStrategy closedStrategy;
    const ToolpathResult closedResult =
        closedStrategy.generate(closedContour, tool, closedStrategy.defaultParams());
    if (expect(closedResult.ok, QStringLiteral("closed contour generation should succeed")) ||
        expectSafeCompensationTransitions(QStringLiteral("closed contour"), closedResult.gcode) ||
        expectPassesSafetyGate(QStringLiteral("closed contour"), closedResult.gcode)) {
        return 1;
    }

    ContourFeature openSlot;
    openSlot.subType = QStringLiteral("open_slot");
    openSlot.center = QVector3D(0.0f, 0.0f, 0.0f);
    openSlot.length = 20.0;
    openSlot.width = 8.0;
    openSlot.depth = 1.0;
    openSlot.openSide = 1.0;

    SlotMillingStrategy slotStrategy;
    StrategyParams slotParams = slotStrategy.defaultParams();
    slotParams.set(QStringLiteral("compensation"), 1.0);
    slotParams.set(QStringLiteral("depth"), openSlot.depth);
    slotParams.set(QStringLiteral("slotLength"), openSlot.length);
    slotParams.set(QStringLiteral("slotWidth"), openSlot.width);
    const ToolpathResult slotResult = slotStrategy.generate(openSlot, tool, slotParams);
    if (expect(slotResult.ok,
               QStringLiteral("open slot generation should succeed: %1").arg(slotResult.errorMsg)) ||
        expectSafeCompensationTransitions(QStringLiteral("open slot"), slotResult.gcode) ||
        expectPassesSafetyGate(QStringLiteral("open slot"), slotResult.gcode)) {
        return 1;
    }

    ContourFeature blindSlot = openSlot;
    blindSlot.subType = QStringLiteral("blind_slot");

    BlindSlotMillingStrategy blindSlotStrategy;
    StrategyParams blindSlotParams = blindSlotStrategy.defaultParams();
    blindSlotParams.set(QStringLiteral("compensation"), 1.0);
    blindSlotParams.set(QStringLiteral("depth"), blindSlot.depth);
    blindSlotParams.set(QStringLiteral("slotLength"), blindSlot.length);
    blindSlotParams.set(QStringLiteral("slotWidth"), blindSlot.width);
    const ToolpathResult blindSlotResult =
        blindSlotStrategy.generate(blindSlot, tool, blindSlotParams);
    if (expect(blindSlotResult.ok,
               QStringLiteral("blind slot generation should succeed: %1").arg(blindSlotResult.errorMsg)) ||
        expectSafeCompensationTransitions(QStringLiteral("blind slot"), blindSlotResult.gcode) ||
        expectPassesSafetyGate(QStringLiteral("blind slot"), blindSlotResult.gcode)) {
        return 1;
    }

    return 0;
}
