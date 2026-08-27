#include "PlanarSlopeMillingStrategy.h"

#include <QObject>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kGeometryTolerance = 0.02;

ProcessParameterDefinition numberDefinition(const QString &id,
                                            ProcessParameterUnit unit,
                                            double defaultValue,
                                            double minimum = 0.0,
                                            bool hasMinimum = false,
                                            double maximum = 0.0,
                                            bool hasMaximum = false)
{
    ProcessParameterDefinition definition;
    definition.id = id;
    definition.type = ProcessParameterType::Number;
    definition.unit = unit;
    definition.defaultValue = defaultValue;
    definition.minimum = minimum;
    definition.hasMinimum = hasMinimum;
    definition.maximum = maximum;
    definition.hasMaximum = hasMaximum;
    definition.visible = true;
    return definition;
}

struct RectangleBounds {
    double minimumX = 0.0;
    double maximumX = 0.0;
    double minimumY = 0.0;
    double maximumY = 0.0;
    bool valid = false;
};

bool nearValue(double first, double second)
{
    return std::abs(first - second) <= kGeometryTolerance;
}

RectangleBounds rectangleBounds(const ContourFeature &feature)
{
    RectangleBounds bounds;
    if (feature.points.size() != 4 || !feature.islands.isEmpty() ||
        std::abs(feature.axis.normalized().z()) < 0.999) {
        return bounds;
    }
    bounds.minimumX = bounds.maximumX = feature.points.first().x();
    bounds.minimumY = bounds.maximumY = feature.points.first().y();
    for (const QVector3D &point : feature.points) {
        if (!nearValue(point.z(), feature.center.z())) {
            return RectangleBounds();
        }
        bounds.minimumX = std::min(bounds.minimumX, double(point.x()));
        bounds.maximumX = std::max(bounds.maximumX, double(point.x()));
        bounds.minimumY = std::min(bounds.minimumY, double(point.y()));
        bounds.maximumY = std::max(bounds.maximumY, double(point.y()));
    }
    if (bounds.maximumX - bounds.minimumX <= kGeometryTolerance ||
        bounds.maximumY - bounds.minimumY <= kGeometryTolerance) {
        return RectangleBounds();
    }

    bool corners[2][2] = {{false, false}, {false, false}};
    for (const QVector3D &point : feature.points) {
        const int xIndex = nearValue(point.x(), bounds.minimumX) ? 0
            : (nearValue(point.x(), bounds.maximumX) ? 1 : -1);
        const int yIndex = nearValue(point.y(), bounds.minimumY) ? 0
            : (nearValue(point.y(), bounds.maximumY) ? 1 : -1);
        if (xIndex < 0 || yIndex < 0 || corners[xIndex][yIndex]) {
            return RectangleBounds();
        }
        corners[xIndex][yIndex] = true;
    }
    bounds.valid = corners[0][0] && corners[0][1] &&
                   corners[1][0] && corners[1][1];
    return bounds;
}

QVector<double> laneOffsets(double usableWidth, double stepOver)
{
    QVector<double> offsets;
    const double halfWidth = usableWidth * 0.5;
    offsets.append(-halfWidth);
    for (double offset = -halfWidth + stepOver;
         offset < halfWidth - 1.0e-6; offset += stepOver) {
        offsets.append(offset);
    }
    if (usableWidth > 1.0e-6) {
        offsets.append(halfWidth);
    }
    return offsets;
}

QString cutXYZ(const QVector3D &point, double feedRate)
{
    return QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
        .arg(point.x(), 0, 'f', 3)
        .arg(point.y(), 0, 'f', 3)
        .arg(point.z(), 0, 'f', 3)
        .arg(feedRate, 0, 'f', 0);
}

} // namespace

ProcessParameterSchema PlanarSlopeMillingStrategy::parameterSchema() const
{
    ProcessParameterSchema schema = ProcessParameterSchema::CommonOperation();
    schema.addDefinition(numberDefinition(
        QStringLiteral("slopeAngle"), ProcessParameterUnit::None,
        10.0, 0.0, true, 60.0, true));
    schema.addDefinition(numberDefinition(
        QStringLiteral("slopeLength"), ProcessParameterUnit::Millimeter,
        20.0, 0.0, true));
    schema.addDefinition(numberDefinition(
        QStringLiteral("xyWidth"), ProcessParameterUnit::Millimeter,
        10.0, 0.0, true));
    schema.addDefinition(numberDefinition(
        QStringLiteral("slopeDirection"), ProcessParameterUnit::None, 0.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("stockToLeave"), ProcessParameterUnit::Millimeter, 0.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("edgeMargin"), ProcessParameterUnit::Millimeter,
        4.0, 0.0, true));
    schema.addDefinition(numberDefinition(
        QStringLiteral("keepEdges"), ProcessParameterUnit::None, 1.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("compensation"), ProcessParameterUnit::None, 0.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("layerAllowance"), ProcessParameterUnit::Millimeter, 0.0));
    return schema;
}

StrategyParams PlanarSlopeMillingStrategy::defaultParams() const
{
    StrategyParams params = parameterSchema().defaultParams();
    params.set(QStringLiteral("safeHeight"), 50.0);
    params.set(QStringLiteral("plungeHeight"), 3.0);
    params.set(QStringLiteral("referenceHeight"), 0.0);
    params.set(QStringLiteral("depth"), 1.0);
    params.set(QStringLiteral("stepOver"), 2.0);
    params.set(QStringLiteral("stepDown"), 1.0);
    params.set(QStringLiteral("feedRate"), 500.0);
    params.set(QStringLiteral("plungeRate"), 120.0);
    params.set(QStringLiteral("spindleSpeed"), 4000.0);
    return params;
}

ToolpathResult PlanarSlopeMillingStrategy::generate(
    const HoleFeature &,
    const ToolEntry &,
    const StrategyParams &) const
{
    return {QString(), false, QObject::tr("二维斜面铣需要显式矩形投影边界。"),
            0.0, ParametricToolpathProgram()};
}

ToolpathResult PlanarSlopeMillingStrategy::generate(
    const ContourFeature &feature,
    const ToolEntry &tool,
    const StrategyParams &params) const
{
    ToolpathResult result;
    const QStringList parameterErrors = parameterSchema().validate(params);
    if (!parameterErrors.isEmpty()) {
        result.errorMsg = parameterErrors.join(QLatin1Char('\n'));
        return result;
    }
    if (tool.id <= 0 || tool.diameter <= 0.0 ||
        tool.type != QStringLiteral("ball_end_mill")) {
        result.errorMsg = QObject::tr("二维斜面安全子集必须使用有效球头铣刀。");
        return result;
    }

    const RectangleBounds bounds = rectangleBounds(feature);
    if (!bounds.valid) {
        result.errorMsg = QObject::tr(
            "二维斜面仅支持前视 Setup 下无孤岛、无自交的显式矩形投影边界。");
        return result;
    }
    const double spanX = bounds.maximumX - bounds.minimumX;
    const double spanY = bounds.maximumY - bounds.minimumY;
    if ((feature.length > 0.0 && !nearValue(feature.length, spanX)) ||
        (feature.width > 0.0 && !nearValue(feature.width, spanY))) {
        result.errorMsg = QObject::tr("斜面特征长宽与矩形投影边界不一致。");
        return result;
    }

    const double angleDegrees = params.get(QStringLiteral("slopeAngle"));
    const double slopeLength = params.get(QStringLiteral("slopeLength"));
    const double xyWidth = params.get(QStringLiteral("xyWidth"));
    const double directionValue = params.get(QStringLiteral("slopeDirection"));
    const int directionIndex = int(std::round(directionValue));
    const double stock = params.get(QStringLiteral("stockToLeave"));
    const double edgeMargin = params.get(QStringLiteral("edgeMargin"));
    const double keepEdges = params.get(QStringLiteral("keepEdges"));
    const double compensation = params.get(QStringLiteral("compensation"));
    const double layerAllowance = params.get(QStringLiteral("layerAllowance"));
    const double stepDown = params.get(QStringLiteral("stepDown"));
    const double stepOver = params.get(QStringLiteral("stepOver"));
    const double safeHeight = params.get(QStringLiteral("safeHeight"));
    const double feedHeight = params.get(QStringLiteral("plungeHeight"));
    const double feedRate = params.get(QStringLiteral("feedRate"));
    const double plungeRate = params.get(QStringLiteral("plungeRate"));
    const double spindleSpeed = params.get(QStringLiteral("spindleSpeed"));
    if (angleDegrees <= 0.0 || angleDegrees > 60.0 || slopeLength <= 0.0 ||
        xyWidth <= 0.0 || directionIndex < 0 || directionIndex > 3 ||
        !nearValue(directionValue, directionIndex) || stock < 0.0 ||
        edgeMargin <= 0.0 || std::abs(keepEdges - 1.0) > 1.0e-6 ||
        std::abs(compensation) > 1.0e-6 || layerAllowance < 0.0 ||
        stepDown <= 0.0 || stepOver <= 0.0 || stepOver > xyWidth ||
        feedHeight < 0.0 || safeHeight <= feature.center.z() + feedHeight ||
        feedRate <= 0.0 || plungeRate <= 0.0 || spindleSpeed <= 0.0) {
        result.errorMsg = QObject::tr(
            "二维斜面坡度、保边、步距、分层、刀补或进刀高度参数无效。");
        return result;
    }

    const bool slopeAlongX = directionIndex < 2;
    const double projectedLength = slopeAlongX ? spanX : spanY;
    const double projectedWidth = slopeAlongX ? spanY : spanX;
    if (!nearValue(slopeLength, projectedLength - 2.0 * edgeMargin) ||
        !nearValue(xyWidth, projectedWidth - 2.0 * edgeMargin)) {
        result.errorMsg = QObject::tr(
            "斜面可加工长度/宽度必须与投影边界扣除保边量后的尺寸一致。");
        return result;
    }

    const double angleRadians = angleDegrees * kPi / 180.0;
    const double targetDepth = slopeLength * std::tan(angleRadians);
    if (feature.depth <= 0.0 ||
        std::abs(feature.depth - targetDepth) > std::max(0.02, targetDepth * 0.002)) {
        result.errorMsg = QObject::tr("坡度、可加工长度与目标深度不一致。");
        return result;
    }
    if ((tool.fluteLen > 0.0 && targetDepth > tool.fluteLen + 0.01) ||
        (tool.totalLen > 0.0 && targetDepth >= tool.totalLen - 0.01)) {
        result.errorMsg = QObject::tr("斜面目标深度超过球头刀可验证切削长度。");
        return result;
    }

    const double toolRadius = tool.diameter * 0.5;
    const double horizontalBallOffset = (toolRadius + stock) * std::sin(angleRadians);
    const double requiredEdgeMargin = toolRadius + horizontalBallOffset;
    if (edgeMargin + kGeometryTolerance < requiredEdgeMargin) {
        result.errorMsg = QObject::tr("保边量不足以容纳球头刀中心补偿后的完整包络。");
        return result;
    }

    QVector3D slopeDirection;
    if (directionIndex == 0) slopeDirection = QVector3D(1.0f, 0.0f, 0.0f);
    if (directionIndex == 1) slopeDirection = QVector3D(-1.0f, 0.0f, 0.0f);
    if (directionIndex == 2) slopeDirection = QVector3D(0.0f, 1.0f, 0.0f);
    if (directionIndex == 3) slopeDirection = QVector3D(0.0f, -1.0f, 0.0f);
    const QVector3D crossDirection(-slopeDirection.y(), slopeDirection.x(), 0.0f);
    const QVector3D normal(float(std::sin(angleRadians) * slopeDirection.x()),
                           float(std::sin(angleRadians) * slopeDirection.y()),
                           float(std::cos(angleRadians)));
    const QVector3D highContact = feature.center - slopeDirection * float(slopeLength * 0.5);
    const QVector3D lowContact = feature.center + slopeDirection * float(slopeLength * 0.5) +
                                 QVector3D(0.0f, 0.0f, float(-targetDepth));
    const QVector3D ballOffset = normal * float(toolRadius + stock) -
                                 QVector3D(0.0f, 0.0f, float(toolRadius));
    const QVector<double> offsets = laneOffsets(xyWidth, stepOver);
    const int layerCount = int(std::ceil(layerAllowance / stepDown)) + 1;

    QString gcode;
    gcode += QStringLiteral("T%1 M6\n").arg(tool.id);
    gcode += QStringLiteral("S%1 M3\n").arg(spindleSpeed, 0, 'f', 0);
    gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
    double totalLength = 0.0;
    for (int layer = 0; layer < layerCount; ++layer) {
        const double allowance = std::max(
            0.0, layerAllowance - std::min(layer * stepDown, layerAllowance));
        gcode += QStringLiteral("(PLANAR SLOPE LAYER %1 ALLOWANCE %2)\n")
                     .arg(layer + 1)
                     .arg(allowance, 0, 'f', 3);
        bool forward = true;
        for (double offset : offsets) {
            QVector3D first = highContact + crossDirection * float(offset) + ballOffset;
            QVector3D second = lowContact + crossDirection * float(offset) + ballOffset;
            first.setZ(first.z() + float(allowance));
            second.setZ(second.z() + float(allowance));
            if (!forward) {
                std::swap(first, second);
            }
            gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
            gcode += QStringLiteral("G0 X%1 Y%2\n")
                         .arg(first.x(), 0, 'f', 3)
                         .arg(first.y(), 0, 'f', 3);
            gcode += QStringLiteral("G0 Z%1\n")
                         .arg(first.z() + feedHeight, 0, 'f', 3);
            gcode += QStringLiteral("G1 Z%1 F%2\n")
                         .arg(first.z(), 0, 'f', 3)
                         .arg(plungeRate, 0, 'f', 0);
            gcode += cutXYZ(second, feedRate);
            gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
            totalLength += slopeLength / std::cos(angleRadians);
            forward = !forward;
        }
    }

    result.gcode = gcode;
    result.ok = true;
    result.estimatedTimeS = totalLength / feedRate * 60.0 +
                            targetDepth / plungeRate * 60.0;
    return result;
}
