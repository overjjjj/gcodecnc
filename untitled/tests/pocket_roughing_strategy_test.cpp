#include "src/strategies/mill/PocketRoughingStrategy.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

struct CuttingSegment {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
};

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

QVector<CuttingSegment> horizontalCuttingSegments(const QString &gcode)
{
    const QRegularExpression xyExpression(
        QStringLiteral("^[^;]*(G[01])\\s+X(-?\\d+(?:\\.\\d+)?)\\s+Y(-?\\d+(?:\\.\\d+)?)"));
    QVector<CuttingSegment> segments;
    double currentX = 0.0;
    double currentY = 0.0;
    const QStringList lines = gcode.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch match = xyExpression.match(line.trimmed());
        if (!match.hasMatch()) {
            continue;
        }
        const double nextX = match.captured(2).toDouble();
        const double nextY = match.captured(3).toDouble();
        if (match.captured(1) == QStringLiteral("G1") &&
            std::abs(currentY - nextY) <= 0.001) {
            segments.push_back({currentX, currentY, nextX, nextY});
        }
        currentX = nextX;
        currentY = nextY;
    }
    return segments;
}

QString expandParametricProgram(const ParametricToolpathProgram &program)
{
    QStringList lines = program.prefixLines;
    for (const ParametricToolpathCall &call : program.calls) {
        for (QString line : program.bodyTemplateLines) {
            for (auto it = call.arguments.cbegin(); it != call.arguments.cend(); ++it) {
                line.replace(QStringLiteral("${%1}").arg(it.key()), it.value());
            }
            lines.append(line);
        }
    }
    lines.append(program.suffixLines);
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
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
    require(!result.parametricProgram.isEmpty() &&
                result.parametricProgram.routineName == QStringLiteral("POCKET_LAYER") &&
                result.parametricProgram.calls.size() == 2 &&
                expandParametricProgram(result.parametricProgram) == result.gcode,
            "vertical regular pocket layers should expose an exactly expandable macro routine");
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

    ContourFeature irregular;
    irregular.subType = QStringLiteral("irregular_pocket");
    irregular.center = QVector3D(20, 15, 0);
    irregular.depth = 4.0;
    irregular.points = {
        QVector3D(0, 0, 0), QVector3D(40, 0, 0),
        QVector3D(40, 30, 0), QVector3D(0, 30, 0)
    };
    irregular.islands = {{
        QVector3D(15, 10, 0), QVector3D(25, 10, 0),
        QVector3D(25, 20, 0), QVector3D(15, 20, 0)
    }};

    ToolEntry irregularTool = tool;
    irregularTool.diameter = 4.0;
    StrategyParams irregularParams = params;
    irregularParams.set(QStringLiteral("stepover"), 4.0);
    const ToolpathResult irregularResult = strategy.generate(
        irregular, irregularTool, irregularParams);
    require(irregularResult.ok,
            "irregular pocket with one island should generate segmented clearing");
    require(irregularResult.gcode.contains(QStringLiteral("; POCKET REGION: IRREGULAR")),
            "irregular pocket output should identify the geometry mode");
    require(!irregularResult.parametricProgram.isEmpty(),
            "irregular multi-layer clearing should expose a reusable layer routine");
    require(irregularResult.parametricProgram.routineName ==
                QStringLiteral("IRREGULAR_POCKET_LAYER") &&
                irregularResult.parametricProgram.parameterNames ==
                    QStringList{QStringLiteral("DEPTH_Z")} &&
                irregularResult.parametricProgram.calls.size() == 2,
            "layer routine should declare one depth parameter and one call per Z layer");
    require(irregularResult.parametricProgram.calls.at(0).arguments.value(
                QStringLiteral("DEPTH_Z")) == QStringLiteral("-2.000") &&
                irregularResult.parametricProgram.calls.at(1).arguments.value(
                    QStringLiteral("DEPTH_Z")) == QStringLiteral("-4.000"),
            "layer routine calls should retain each absolute cutting depth");
    require(expandParametricProgram(irregularResult.parametricProgram) ==
                irregularResult.gcode,
            "expanding controller-neutral routine metadata must reproduce final G-code exactly");

    const QVector<CuttingSegment> segments = horizontalCuttingSegments(irregularResult.gcode);
    require(!segments.isEmpty(), "irregular pocket should contain cutting segments");
    bool foundSplitRow = false;
    double minimumCutY = segments.first().y0;
    double maximumCutY = segments.first().y0;
    for (const CuttingSegment &segment : segments) {
        minimumCutY = std::min(minimumCutY, segment.y0);
        maximumCutY = std::max(maximumCutY, segment.y0);
        require(std::abs(segment.y0 - segment.y1) <= 0.001,
                "irregular clearing cuts must be horizontal row segments");
        require(std::min(segment.x0, segment.x1) >= 2.999 &&
                    std::max(segment.x0, segment.x1) <= 37.001 &&
                    segment.y0 >= 2.999 && segment.y0 <= 27.001,
                "tool center must keep tool-radius plus stock clearance from outer boundary");
        if (segment.y0 > 7.001 && segment.y0 < 22.999) {
            double forbiddenMin = 12.0;
            double forbiddenMax = 28.0;
            if (segment.y0 < 10.0) {
                const double dy = 10.0 - segment.y0;
                const double cornerSpan = std::sqrt(std::max(0.0, 9.0 - dy * dy));
                forbiddenMin = 15.0 - cornerSpan;
                forbiddenMax = 25.0 + cornerSpan;
            } else if (segment.y0 > 20.0) {
                const double dy = segment.y0 - 20.0;
                const double cornerSpan = std::sqrt(std::max(0.0, 9.0 - dy * dy));
                forbiddenMin = 15.0 - cornerSpan;
                forbiddenMax = 25.0 + cornerSpan;
            }
            const bool leftOfIsland =
                std::max(segment.x0, segment.x1) <= forbiddenMin + 0.001;
            const bool rightOfIsland =
                std::min(segment.x0, segment.x1) >= forbiddenMax - 0.001;
            require(leftOfIsland || rightOfIsland,
                    "cutting segment must not cross the tool-expanded island");
            foundSplitRow = true;
        }
    }
    require(foundSplitRow, "at least one scan row should be split around the island");
    require(minimumCutY <= 3.001 && maximumCutY >= 26.999,
            "irregular clearing should reach both effective outer-boundary sides");
    require(irregularResult.gcode.contains(
                QStringLiteral("; POCKET LINK: SAFE SAME-REGION")),
            "equal adjacent safe regions should be linked without a repeated plunge");
    require(irregularResult.gcode.count(QStringLiteral("G0 X")) < segments.size(),
            "safe row linking should use fewer rapid entries than horizontal cutting rows");
    require(irregularResult.gcode.count(QStringLiteral("G0 Z50.000")) ==
                irregularResult.gcode.count(QStringLiteral("G0 X")) + 1,
            "every irregular-pocket rapid XY transition must occur after a safe retract");

    StrategyParams irregularHelicalParams = irregularParams;
    irregularHelicalParams.set(QStringLiteral("entryMode"), 1.0);
    irregularHelicalParams.set(QStringLiteral("helixRadius"), 2.0);
    irregularHelicalParams.set(QStringLiteral("helixPitch"), 0.5);
    require(!strategy.generate(irregular, irregularTool, irregularHelicalParams).ok,
            "irregular pockets must reject unvalidated helical entry");

    ContourFeature selfIntersecting = irregular;
    selfIntersecting.islands.clear();
    selfIntersecting.points = {
        QVector3D(0, 0, 0), QVector3D(40, 30, 0),
        QVector3D(0, 30, 0), QVector3D(40, 0, 0)
    };
    require(!strategy.generate(selfIntersecting, irregularTool, irregularParams).ok,
            "self-intersecting pocket boundaries must be rejected");

    ContourFeature outsideIsland = irregular;
    outsideIsland.islands = {{
        QVector3D(45, 10, 0), QVector3D(50, 10, 0),
        QVector3D(50, 15, 0), QVector3D(45, 15, 0)
    }};
    require(!strategy.generate(outsideIsland, irregularTool, irregularParams).ok,
            "islands outside the pocket boundary must be rejected");

    ContourFeature concaveVertexBetweenRows = irregular;
    concaveVertexBetweenRows.islands.clear();
    concaveVertexBetweenRows.points = {
        QVector3D(0, 0, 0), QVector3D(40, 0, 0), QVector3D(40, 30, 0),
        QVector3D(0, 30, 0), QVector3D(0, 22, 0), QVector3D(8, 15, 0),
        QVector3D(0, 8, 0)
    };
    StrategyParams wideRows = irregularParams;
    wideRows.set(QStringLiteral("stepover"), 8.0);
    const ToolpathResult concaveResult = strategy.generate(
        concaveVertexBetweenRows, irregularTool, wideRows);
    require(concaveResult.ok, "concave pocket should retain a valid segmented path");
    require(!concaveResult.gcode.contains(
                QStringLiteral("; POCKET LINK: SAFE SAME-REGION")),
            "a topology vertex between adjacent rows must force a safe retract");

    QTextStream(stdout) << "PASS pocket_roughing_strategy_test" << Qt::endl;
    return 0;
}
