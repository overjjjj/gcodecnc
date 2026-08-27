#include "InnerCornerCleanupStrategy.h"

#include <QObject>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kGeometryTolerance = 1.0e-4;

ProcessParameterDefinition numberDefinition(const QString &id,
                                            ProcessParameterUnit unit,
                                            double defaultValue)
{
    ProcessParameterDefinition definition;
    definition.id = id;
    definition.type = ProcessParameterType::Number;
    definition.unit = unit;
    definition.defaultValue = defaultValue;
    definition.visible = true;
    definition.required = true;
    return definition;
}

bool rectangularBounds(const ContourFeature &feature,
                       double *minimumX,
                       double *maximumX,
                       double *minimumY,
                       double *maximumY)
{
    if (feature.points.size() != 4 || !feature.islands.isEmpty() ||
        feature.axis.lengthSquared() <= 1.0e-8f ||
        std::abs(feature.axis.normalized().z()) < 0.999f ||
        feature.region == FaceRegion::Side || feature.region == FaceRegion::Back) {
        return false;
    }

    *minimumX = *maximumX = feature.points.first().x();
    *minimumY = *maximumY = feature.points.first().y();
    const double z = feature.points.first().z();
    for (const QVector3D &point : feature.points) {
        if (std::abs(double(point.z()) - z) > kGeometryTolerance) {
            return false;
        }
        *minimumX = std::min(*minimumX, double(point.x()));
        *maximumX = std::max(*maximumX, double(point.x()));
        *minimumY = std::min(*minimumY, double(point.y()));
        *maximumY = std::max(*maximumY, double(point.y()));
    }
    if (*maximumX - *minimumX <= kGeometryTolerance ||
        *maximumY - *minimumY <= kGeometryTolerance) {
        return false;
    }

    bool corners[2][2] = {{false, false}, {false, false}};
    for (const QVector3D &point : feature.points) {
        const int xIndex = std::abs(double(point.x()) - *minimumX) <= kGeometryTolerance
            ? 0 : std::abs(double(point.x()) - *maximumX) <= kGeometryTolerance ? 1 : -1;
        const int yIndex = std::abs(double(point.y()) - *minimumY) <= kGeometryTolerance
            ? 0 : std::abs(double(point.y()) - *maximumY) <= kGeometryTolerance ? 1 : -1;
        if (xIndex < 0 || yIndex < 0 || corners[xIndex][yIndex]) {
            return false;
        }
        corners[xIndex][yIndex] = true;
    }
    return corners[0][0] && corners[0][1] && corners[1][0] && corners[1][1];
}

QString pointMove(double x, double y, double feedRate)
{
    return QStringLiteral("G1 X%1 Y%2 F%3\n")
        .arg(x, 0, 'f', 3)
        .arg(y, 0, 'f', 3)
        .arg(feedRate, 0, 'f', 0);
}

} // namespace

ProcessParameterSchema InnerCornerCleanupStrategy::parameterSchema() const
{
    ProcessParameterSchema schema = ProcessParameterSchema::CommonOperation();
    schema.addDefinition(numberDefinition(
        QStringLiteral("previousToolDiameter"), ProcessParameterUnit::Millimeter, 10.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("cornerTolerance"), ProcessParameterUnit::Millimeter, 0.05));
    schema.addDefinition(numberDefinition(
        QStringLiteral("safetyDistance"), ProcessParameterUnit::Millimeter, 0.5));
    schema.addDefinition(numberDefinition(
        QStringLiteral("stockToLeave"), ProcessParameterUnit::Millimeter, 0.0));
    return schema;
}

StrategyParams InnerCornerCleanupStrategy::defaultParams() const
{
    StrategyParams params = parameterSchema().defaultParams();
    params.set(QStringLiteral("safeHeight"), 50.0);
    params.set(QStringLiteral("plungeHeight"), 3.0);
    params.set(QStringLiteral("referenceHeight"), 0.0);
    params.set(QStringLiteral("depth"), 1.0);
    params.set(QStringLiteral("stepOver"), 1.0);
    params.set(QStringLiteral("stepDown"), 1.0);
    params.set(QStringLiteral("feedRate"), 300.0);
    params.set(QStringLiteral("plungeRate"), 100.0);
    params.set(QStringLiteral("spindleSpeed"), 3000.0);
    return params;
}

ToolpathResult InnerCornerCleanupStrategy::generate(
    const HoleFeature &,
    const ToolEntry &,
    const StrategyParams &) const
{
    return {QString(), false, QObject::tr("清内角需要已验证的矩形型腔边界。"), 0.0,
            ParametricToolpathProgram()};
}

ToolpathResult InnerCornerCleanupStrategy::generate(
    const ContourFeature &feature,
    const ToolEntry &tool,
    const StrategyParams &params) const
{
    ToolpathResult result;
    double minimumX = 0.0;
    double maximumX = 0.0;
    double minimumY = 0.0;
    double maximumY = 0.0;
    if (!rectangularBounds(feature, &minimumX, &maximumX, &minimumY, &maximumY)) {
        result.errorMsg = QObject::tr("清内角当前仅支持正面、无孤岛、轴对齐的闭合矩形型腔。");
        return result;
    }

    const double safeHeight = params.get(QStringLiteral("safeHeight"), 50.0);
    const double plungeHeight = params.get(QStringLiteral("plungeHeight"), 3.0);
    const double referenceHeight = params.get(QStringLiteral("referenceHeight"), 0.0);
    const double depth = params.get(QStringLiteral("depth"), feature.depth);
    const double stepDown = params.get(QStringLiteral("stepDown"), 1.0);
    const double stepOver = params.get(QStringLiteral("stepOver"), 1.0);
    const double previousToolDiameter = params.get(
        QStringLiteral("previousToolDiameter"), 0.0);
    const double cornerTolerance = params.get(QStringLiteral("cornerTolerance"), 0.05);
    const double safetyDistance = params.get(QStringLiteral("safetyDistance"), 0.5);
    const double stockToLeave = params.get(QStringLiteral("stockToLeave"), 0.0);
    const double feedRate = params.get(QStringLiteral("feedRate"), 300.0);
    const double plungeRate = params.get(QStringLiteral("plungeRate"), 100.0);
    const double spindleSpeed = params.get(QStringLiteral("spindleSpeed"), 3000.0);

    if (tool.id <= 0 || tool.type != QStringLiteral("end_mill") ||
        tool.diameter <= 0.0 || tool.fluteLen <= 0.0 ||
        depth > tool.fluteLen + kGeometryTolerance ||
        previousToolDiameter <= tool.diameter ||
        depth <= 0.0 || stepDown <= 0.0 || stepOver <= 0.0 ||
        stepOver > tool.diameter + kGeometryTolerance ||
        cornerTolerance < 0.0 || safetyDistance < 0.0 || stockToLeave < 0.0 ||
        safeHeight < plungeHeight || plungeHeight < referenceHeight ||
        feedRate <= 0.0 || plungeRate <= 0.0 || spindleSpeed <= 0.0) {
        result.errorMsg = QObject::tr("清内角刀具、步距、层深、高度或余量参数不安全。");
        return result;
    }
    if (feature.depth > 0.0 && depth > feature.depth + kGeometryTolerance) {
        result.errorMsg = QObject::tr("清内角深度超过已识别型腔深度。");
        return result;
    }

    const double targetInset = tool.diameter * 0.5 + stockToLeave + cornerTolerance;
    const double startInset = previousToolDiameter * 0.5 + stockToLeave + safetyDistance;
    if (targetInset >= (maximumX - minimumX) * 0.5 ||
        targetInset >= (maximumY - minimumY) * 0.5 ||
        startInset >= (maximumX - minimumX) * 0.5 ||
        startInset >= (maximumY - minimumY) * 0.5 ||
        startInset <= targetInset + kGeometryTolerance) {
        result.errorMsg = QObject::tr("矩形型腔没有足够空间容纳前刀安全区和清角刀中心包络。");
        return result;
    }

    struct CornerPass {
        double startX;
        double startY;
        double targetX;
        double targetY;
    };
    const CornerPass corners[] = {
        {minimumX + startInset, minimumY + startInset,
         minimumX + targetInset, minimumY + targetInset},
        {maximumX - startInset, minimumY + startInset,
         maximumX - targetInset, minimumY + targetInset},
        {maximumX - startInset, maximumY - startInset,
         maximumX - targetInset, maximumY - targetInset},
        {minimumX + startInset, maximumY - startInset,
         minimumX + targetInset, maximumY - targetInset}
    };

    QString gcode;
    gcode += QStringLiteral("T%1 M6\n").arg(tool.id);
    gcode += QStringLiteral("S%1 M3\n").arg(spindleSpeed, 0, 'f', 0);
    int layer = 0;
    for (double cutDepth = std::min(stepDown, depth);
         cutDepth <= depth + kGeometryTolerance;
         cutDepth = std::min(depth, cutDepth + stepDown)) {
        ++layer;
        const double z = referenceHeight - cutDepth;
        gcode += QStringLiteral("; INNER CORNER LAYER %1 Z%2\n")
                     .arg(layer)
                     .arg(z, 0, 'f', 3);
        for (int corner = 0; corner < 4; ++corner) {
            const CornerPass &pass = corners[corner];
            gcode += QStringLiteral("; INNER CORNER C%1\n").arg(corner + 1);
            gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
            gcode += QStringLiteral("G0 X%1 Y%2\n")
                         .arg(pass.startX, 0, 'f', 3)
                         .arg(pass.startY, 0, 'f', 3);
            gcode += QStringLiteral("G0 Z%1\n").arg(plungeHeight, 0, 'f', 3);
            gcode += QStringLiteral("G1 Z%1 F%2\n")
                         .arg(z, 0, 'f', 3)
                         .arg(plungeRate, 0, 'f', 0);
            const double deltaX = pass.targetX - pass.startX;
            const double deltaY = pass.targetY - pass.startY;
            const double passLength = std::sqrt(deltaX * deltaX + deltaY * deltaY);
            const int stepCount = std::max(1, int(std::ceil(passLength / stepOver)));
            for (int step = 1; step <= stepCount; ++step) {
                const double factor = double(step) / stepCount;
                gcode += QStringLiteral("; INNER CORNER STEP %1/%2\n")
                             .arg(step)
                             .arg(stepCount);
                gcode += pointMove(pass.startX + deltaX * factor,
                                   pass.startY + deltaY * factor, feedRate);
                gcode += pointMove(pass.startX, pass.startY, feedRate);
            }
            gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
        }
        if (cutDepth >= depth - kGeometryTolerance) {
            break;
        }
    }
    result.gcode = gcode.trimmed();
    result.ok = true;
    result.estimatedTimeS = layer * 4.0;
    return result;
}
