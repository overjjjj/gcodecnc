#include "AnnularMillingStrategy.h"

#include "../../core/geometry2d/ToolpathGeometry2D.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace {

namespace geometry = cnext::geometry2d;

struct FittedCircle {
    QVector3D center;
    double radius = 0.0;
    bool valid = false;
};

FittedCircle fitCircle(const QVector<QVector3D> &points)
{
    FittedCircle circle;
    if (points.size() < 6) {
        return circle;
    }
    for (const QVector3D &point : points) {
        circle.center += point;
    }
    circle.center /= float(points.size());

    double minimumRadius = std::numeric_limits<double>::max();
    double maximumRadius = 0.0;
    double radiusSum = 0.0;
    for (const QVector3D &point : points) {
        const double radius = double((point - circle.center).length());
        minimumRadius = std::min(minimumRadius, radius);
        maximumRadius = std::max(maximumRadius, radius);
        radiusSum += radius;
    }
    circle.radius = radiusSum / points.size();
    const double tolerance = std::max(0.05, circle.radius * 0.002);
    circle.valid = circle.radius > 0.0 && maximumRadius - minimumRadius <= tolerance;
    return circle;
}

ProcessParameterDefinition stockDefinition(const QString &id)
{
    ProcessParameterDefinition definition;
    definition.id = id;
    definition.type = ProcessParameterType::Number;
    definition.unit = ProcessParameterUnit::Millimeter;
    definition.defaultValue = 0.0;
    definition.visible = true;
    return definition;
}

void appendQuarterCircle(QString *gcode,
                         double centerX,
                         double centerY,
                         double radius,
                         double startAngle,
                         double endZ,
                         double feedRate)
{
    const double startX = centerX + radius * std::cos(startAngle);
    const double startY = centerY + radius * std::sin(startAngle);
    const double endAngle = startAngle + std::acos(-1.0) * 0.5;
    const double endX = centerX + radius * std::cos(endAngle);
    const double endY = centerY + radius * std::sin(endAngle);
    *gcode += QStringLiteral("G3 X%1 Y%2 Z%3 I%4 J%5 F%6\n")
                  .arg(endX, 0, 'f', 3)
                  .arg(endY, 0, 'f', 3)
                  .arg(endZ, 0, 'f', 3)
                  .arg(centerX - startX, 0, 'f', 3)
                  .arg(centerY - startY, 0, 'f', 3)
                  .arg(feedRate, 0, 'f', 0);
}

void appendCircle(QString *gcode,
                  double centerX,
                  double centerY,
                  double radius,
                  double z,
                  double feedRate)
{
    for (int quarter = 0; quarter < 4; ++quarter) {
        appendQuarterCircle(gcode, centerX, centerY, radius,
                            quarter * std::acos(-1.0) * 0.5, z, feedRate);
    }
}

} // namespace

ProcessParameterSchema AnnularMillingStrategy::parameterSchema() const
{
    ProcessParameterSchema schema = ProcessParameterSchema::CommonOperation();
    schema.addDefinition(stockDefinition(QStringLiteral("outerStockToLeave")));
    schema.addDefinition(stockDefinition(QStringLiteral("islandStockToLeave")));
    return schema;
}

StrategyParams AnnularMillingStrategy::defaultParams() const
{
    StrategyParams params = parameterSchema().defaultParams();
    params.set(QStringLiteral("safeHeight"), 50.0);
    params.set(QStringLiteral("plungeHeight"), 3.0);
    params.set(QStringLiteral("referenceHeight"), 0.0);
    params.set(QStringLiteral("depth"), 1.0);
    params.set(QStringLiteral("stepOver"), 3.0);
    params.set(QStringLiteral("stepDown"), 2.0);
    params.set(QStringLiteral("feedRate"), 800.0);
    params.set(QStringLiteral("plungeRate"), 180.0);
    params.set(QStringLiteral("spindleSpeed"), 3000.0);
    return params;
}

ToolpathResult AnnularMillingStrategy::generate(
    const HoleFeature &,
    const ToolEntry &,
    const StrategyParams &) const
{
    return {QString(), false, QObject::tr("环形铣需要闭合的外边界和孤岛边界。"),
            0.0, ParametricToolpathProgram()};
}

ToolpathResult AnnularMillingStrategy::generate(
    const ContourFeature &feature,
    const ToolEntry &tool,
    const StrategyParams &params) const
{
    ToolpathResult result;
    const QStringList parameterErrors = validate(params);
    if (!parameterErrors.isEmpty()) {
        result.errorMsg = parameterErrors.join(QLatin1Char('\n'));
        return result;
    }
    if (feature.radius <= 0.0 || feature.islands.size() != 1) {
        result.errorMsg = QObject::tr("环形铣仅支持一个圆形外边界和一个圆形孤岛边界。");
        return result;
    }
    if (tool.id <= 0 || tool.diameter <= 0.0) {
        result.errorMsg = QObject::tr("环形铣刀具无效。");
        return result;
    }

    const FittedCircle outer = fitCircle(feature.points);
    if (!outer.valid || double((outer.center - feature.center).length()) > 0.05 ||
        std::abs(outer.radius - feature.radius) > 0.05) {
        result.errorMsg = QObject::tr("外边界不是与特征半径一致的可验证圆形。");
        return result;
    }
    const FittedCircle island = fitCircle(feature.islands.first());
    if (!island.valid) {
        result.errorMsg = QObject::tr("孤岛边界不是可验证的圆形。");
        return result;
    }
    if (double((island.center - feature.center).length()) > 0.05) {
        result.errorMsg = QObject::tr("当前环形铣仅支持同心的内外边界。");
        return result;
    }

    geometry::Region region;
    region.outer.segments.append(geometry::Segment::Arc(
        {outer.center.x(), outer.center.y()}, outer.radius,
        0.0, 2.0 * geometry::Pi()));
    geometry::Wire islandWire;
    islandWire.segments.append(geometry::Segment::Arc(
        {island.center.x(), island.center.y()}, island.radius,
        0.0, 2.0 * geometry::Pi()));
    region.islands.append(islandWire);
    const geometry::GeometryTolerance geometryTolerance;
    if (!geometry::ValidateRegion(region, geometryTolerance).ok) {
        result.errorMsg = QObject::tr("环形铣内外边界不构成有效加工区域。");
        return result;
    }

    const double outerStock = params.get(QStringLiteral("outerStockToLeave"), 0.0);
    const double islandStock = params.get(QStringLiteral("islandStockToLeave"), 0.0);
    if (outerStock < 0.0 || islandStock < 0.0) {
        result.errorMsg = QObject::tr("边界余量不得为负值。");
        return result;
    }
    const double toolRadius = tool.diameter * 0.5;
    const geometry::OffsetResult outerEnvelope = geometry::OffsetWire(
        region.outer, geometry::OffsetSide::Inside,
        toolRadius + outerStock, geometryTolerance);
    const geometry::OffsetResult islandEnvelope = geometry::OffsetWire(
        islandWire, geometry::OffsetSide::Outside,
        toolRadius + islandStock, geometryTolerance);
    if (!outerEnvelope.ok || !islandEnvelope.ok) {
        result.errorMsg = QObject::tr("刀具中心包络偏置失败。");
        return result;
    }
    const double maximumRadius = outerEnvelope.wire.segments.first().arc.radius;
    const double minimumRadius = islandEnvelope.wire.segments.first().arc.radius;
    if (maximumRadius <= minimumRadius + 1.0e-6) {
        result.errorMsg = QObject::tr("刀具中心包络无法进入当前环形区域。");
        return result;
    }

    const double safeHeight = params.get(QStringLiteral("safeHeight"));
    const double plungeHeight = params.get(QStringLiteral("plungeHeight"));
    const double referenceHeight = params.get(QStringLiteral("referenceHeight"));
    const double depth = params.get(QStringLiteral("depth"));
    const double stepOver = params.get(QStringLiteral("stepOver"));
    const double stepDown = params.get(QStringLiteral("stepDown"));
    const double feedRate = params.get(QStringLiteral("feedRate"));
    const double plungeRate = params.get(QStringLiteral("plungeRate"));
    const double spindleSpeed = params.get(QStringLiteral("spindleSpeed"));
    if (safeHeight <= plungeHeight || plungeHeight <= referenceHeight ||
        stepOver <= 0.0 || stepDown <= 0.0 || depth <= 0.0 ||
        feedRate <= 0.0 || plungeRate <= 0.0 || spindleSpeed <= 0.0) {
        result.errorMsg = QObject::tr("环形铣高度、深度、步距或切削参数不安全。");
        return result;
    }

    QVector<double> radii{maximumRadius};
    for (double radius = maximumRadius - stepOver;
         radius > minimumRadius + 1.0e-6; radius -= stepOver) {
        radii.append(radius);
    }
    radii.append(minimumRadius);

    const int layerCount = int(std::ceil(depth / stepDown));
    const double centerX = feature.center.x();
    const double centerY = feature.center.y();
    QString gcode;
    gcode += QStringLiteral("T%1 M6\n").arg(tool.id);
    gcode += QStringLiteral("S%1 M3\n").arg(spindleSpeed, 0, 'f', 0);
    gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
    const QString layerName = id() == QStringLiteral("mill_island")
        ? QStringLiteral("ISLAND") : QStringLiteral("ANNULAR");
    for (int layer = 1; layer <= layerCount; ++layer) {
        const double layerZ = referenceHeight - std::min(layer * stepDown, depth);
        gcode += QStringLiteral("(%1 LAYER %2)\n").arg(layerName).arg(layer);
        gcode += QStringLiteral("G0 X%1 Y%2\n")
                     .arg(centerX + maximumRadius, 0, 'f', 3)
                     .arg(centerY, 0, 'f', 3);
        gcode += QStringLiteral("G1 Z%1 F%2\n")
                     .arg(plungeHeight, 0, 'f', 3)
                     .arg(plungeRate, 0, 'f', 0);
        for (int quarter = 0; quarter < 4; ++quarter) {
            const double quarterZ = plungeHeight +
                (layerZ - plungeHeight) * (quarter + 1) / 4.0;
            appendQuarterCircle(&gcode, centerX, centerY, maximumRadius,
                                quarter * std::acos(-1.0) * 0.5,
                                quarterZ, plungeRate);
        }
        for (int radiusIndex = 1; radiusIndex < radii.size(); ++radiusIndex) {
            const double radius = radii.at(radiusIndex);
            gcode += QStringLiteral("G1 X%1 Y%2 F%3\n")
                         .arg(centerX + radius, 0, 'f', 3)
                         .arg(centerY, 0, 'f', 3)
                         .arg(feedRate, 0, 'f', 0);
            appendCircle(&gcode, centerX, centerY, radius, layerZ, feedRate);
        }
        gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
    }

    result.gcode = gcode;
    result.ok = true;
    const double pathLengthPerLayer =
        2.0 * std::acos(-1.0) * std::accumulate(radii.cbegin(), radii.cend(), 0.0);
    result.estimatedTimeS = pathLengthPerLayer * layerCount / feedRate * 60.0;
    return result;
}
