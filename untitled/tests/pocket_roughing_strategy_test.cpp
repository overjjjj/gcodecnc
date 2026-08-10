#include "src/strategies/mill/PocketRoughingStrategy.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << Qt::endl;
        std::exit(1);
    }
}

double maximumAbsoluteAxis(const QString &gcode, QChar axis)
{
    const QRegularExpression expression(
        QStringLiteral("(?:^|\\s)%1(-?\\d+(?:\\.\\d+)?)").arg(axis),
        QRegularExpression::MultilineOption);
    double maximum = 0.0;
    QRegularExpressionMatchIterator matches = expression.globalMatch(gcode);
    while (matches.hasNext()) {
        maximum = std::max(maximum, std::abs(matches.next().captured(1).toDouble()));
    }
    return maximum;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    PocketRoughingStrategy strategy;

    ContourFeature pocket;
    pocket.subType = QStringLiteral("rectangular_pocket");
    pocket.center = QVector3D(0, 0, 0);
    pocket.length = 40.0;
    pocket.width = 20.0;
    pocket.radius = 30.0; // Deliberately larger: true L/W must take precedence.
    pocket.depth = 4.0;

    ToolEntry tool;
    tool.id = 3;
    tool.type = QStringLiteral("end_mill");
    tool.diameter = 6.0;

    StrategyParams params = strategy.defaultParams();
    params.set(QStringLiteral("stockToLeave"), 1.0);
    params.set(QStringLiteral("stepover"), 4.0);
    params.set(QStringLiteral("entryMode"), 0.0);
    params.set(QStringLiteral("helixRadius"), 0.0);

    const ToolpathResult result = strategy.generate(pocket, tool, params);
    require(result.ok, "rectangular pocket should generate a toolpath");

    const double maxX = maximumAbsoluteAxis(result.gcode, QLatin1Char('X'));
    const double maxY = maximumAbsoluteAxis(result.gcode, QLatin1Char('Y'));
    require(maxX <= 16.001,
            "tool center must remain inside length/2 - toolRadius - stock");
    require(maxY <= 6.001,
            "tool center must remain inside width/2 - toolRadius - stock");
    require(maxX >= 15.999 && maxY >= 5.999,
            "rectangular clearing should reach both effective L/W boundaries");
    require(result.gcode.contains(QStringLiteral("; POCKET ENTRY: VERTICAL")),
            "vertical entry choice should be explicit in generated G-code");
    require(result.gcode.contains(QStringLiteral("G0 X0.000 Y0.000")),
            "vertical entry should plunge at the pocket center");
    require(!result.gcode.contains(QStringLiteral("G2 ")),
            "vertical entry must not emit helical interpolation");

    StrategyParams helicalParams = params;
    helicalParams.set(QStringLiteral("entryMode"), 1.0);
    helicalParams.set(QStringLiteral("helixRadius"), 2.0);
    helicalParams.set(QStringLiteral("helixPitch"), 0.5);
    const ToolpathResult helicalResult = strategy.generate(pocket, tool, helicalParams);
    require(helicalResult.ok, "valid helical entry should generate a toolpath");
    require(helicalResult.gcode.contains(QStringLiteral("; POCKET ENTRY: HELICAL")),
            "helical entry choice should be explicit in generated G-code");
    require(helicalResult.gcode.contains(QStringLiteral("G2 ")),
            "helical entry should emit circular interpolation with Z descent");

    StrategyParams missingEntry = params;
    missingEntry.values.remove(QStringLiteral("entryMode"));
    require(!strategy.generate(pocket, tool, missingEntry).ok,
            "pocket roughing must reject a missing operator entry choice");

    StrategyParams oversizedHelix = helicalParams;
    oversizedHelix.set(QStringLiteral("helixRadius"), 7.0);
    require(!strategy.generate(pocket, tool, oversizedHelix).ok,
            "helical entry outside the effective pocket boundary must be rejected");

    StrategyParams missingPitch = helicalParams;
    missingPitch.set(QStringLiteral("helixPitch"), 0.0);
    require(!strategy.generate(pocket, tool, missingPitch).ok,
            "helical entry must not invent a pitch that the operator did not confirm");

    ContourFeature rotated = pocket;
    rotated.angle = 90.0;
    const ToolpathResult rotatedResult = strategy.generate(rotated, tool, params);
    require(rotatedResult.ok, "rotated rectangular pocket should generate");
    require(maximumAbsoluteAxis(rotatedResult.gcode, QLatin1Char('X')) <= 6.001,
            "90-degree pocket should rotate the short effective boundary onto X");
    require(maximumAbsoluteAxis(rotatedResult.gcode, QLatin1Char('Y')) <= 16.001,
            "90-degree pocket should rotate the long effective boundary onto Y");

    ContourFeature tooNarrow = pocket;
    tooNarrow.width = 7.0;
    require(!strategy.generate(tooNarrow, tool, params).ok,
            "pocket narrower than tool diameter plus side stock must be rejected");

    QTextStream(stdout) << "PASS pocket_roughing_strategy_test" << Qt::endl;
    return 0;
}
