#include "OuterContourChamferStrategy.h"

#include <QObject>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kGeometryTolerance = 1.0e-4;
constexpr double kPi = 3.14159265358979323846;

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
    return definition;
}

double edgeLength(const QVector3D &from, const QVector3D &to)
{
    return std::hypot(double(to.x() - from.x()), double(to.y() - from.y()));
}

double orientation(const QVector3D &first,
                   const QVector3D &second,
                   const QVector3D &third)
{
    return double(second.x() - first.x()) * double(third.y() - first.y()) -
           double(second.y() - first.y()) * double(third.x() - first.x());
}

bool pointOnSegment(const QVector3D &point,
                    const QVector3D &first,
                    const QVector3D &second)
{
    return std::abs(orientation(first, second, point)) <= kGeometryTolerance &&
           point.x() >= std::min(first.x(), second.x()) - kGeometryTolerance &&
           point.x() <= std::max(first.x(), second.x()) + kGeometryTolerance &&
           point.y() >= std::min(first.y(), second.y()) - kGeometryTolerance &&
           point.y() <= std::max(first.y(), second.y()) + kGeometryTolerance;
}

bool segmentsIntersect(const QVector3D &firstStart,
                       const QVector3D &firstEnd,
                       const QVector3D &secondStart,
                       const QVector3D &secondEnd)
{
    const double firstSide = orientation(firstStart, firstEnd, secondStart);
    const double secondSide = orientation(firstStart, firstEnd, secondEnd);
    const double thirdSide = orientation(secondStart, secondEnd, firstStart);
    const double fourthSide = orientation(secondStart, secondEnd, firstEnd);
    if (((firstSide > kGeometryTolerance && secondSide < -kGeometryTolerance) ||
         (firstSide < -kGeometryTolerance && secondSide > kGeometryTolerance)) &&
        ((thirdSide > kGeometryTolerance && fourthSide < -kGeometryTolerance) ||
         (thirdSide < -kGeometryTolerance && fourthSide > kGeometryTolerance))) {
        return true;
    }
    return (std::abs(firstSide) <= kGeometryTolerance &&
            pointOnSegment(secondStart, firstStart, firstEnd)) ||
           (std::abs(secondSide) <= kGeometryTolerance &&
            pointOnSegment(secondEnd, firstStart, firstEnd)) ||
           (std::abs(thirdSide) <= kGeometryTolerance &&
            pointOnSegment(firstStart, secondStart, secondEnd)) ||
           (std::abs(fourthSide) <= kGeometryTolerance &&
            pointOnSegment(firstEnd, secondStart, secondEnd));
}

bool hasSelfIntersection(const QVector<QVector3D> &points)
{
    const int count = points.size();
    for (int first = 0; first < count; ++first) {
        const int firstNext = (first + 1) % count;
        for (int second = first + 1; second < count; ++second) {
            const int secondNext = (second + 1) % count;
            if (first == second || firstNext == second || secondNext == first) {
                continue;
            }
            if (segmentsIntersect(points.at(first), points.at(firstNext),
                                  points.at(second), points.at(secondNext))) {
                return true;
            }
        }
    }
    return false;
}

bool isPlanarConvexLoop(const ContourFeature &feature)
{
    if (feature.points.size() < 3 ||
        std::abs(feature.axis.normalized().z()) < 0.999 ||
        hasSelfIntersection(feature.points)) {
        return false;
    }

    double turnSign = 0.0;
    for (int index = 0; index < feature.points.size(); ++index) {
        const QVector3D &previous = feature.points.at(
            (index + feature.points.size() - 1) % feature.points.size());
        const QVector3D &current = feature.points.at(index);
        const QVector3D &next = feature.points.at(
            (index + 1) % feature.points.size());
        if (std::abs(double(current.z()) - double(feature.center.z())) >
            kGeometryTolerance) {
            return false;
        }
        const double incomingX = current.x() - previous.x();
        const double incomingY = current.y() - previous.y();
        const double outgoingX = next.x() - current.x();
        const double outgoingY = next.y() - current.y();
        if (std::hypot(incomingX, incomingY) <= kGeometryTolerance ||
            std::hypot(outgoingX, outgoingY) <= kGeometryTolerance) {
            return false;
        }
        const double cross = incomingX * outgoingY - incomingY * outgoingX;
        if (std::abs(cross) <= kGeometryTolerance) {
            continue;
        }
        const double currentSign = cross > 0.0 ? 1.0 : -1.0;
        if (turnSign == 0.0) {
            turnSign = currentSign;
        } else if (currentSign != turnSign) {
            return false;
        }
    }
    return turnSign != 0.0;
}

double roundedTipCompensation(double tipRadius, double angleRadians)
{
    return tipRadius * (1.0 / std::sin(angleRadians) - 1.0);
}

QString feedXY(double x, double y, double feedRate)
{
    return QStringLiteral("G1 X%1 Y%2 F%3\n")
        .arg(x, 0, 'f', 3)
        .arg(y, 0, 'f', 3)
        .arg(feedRate, 0, 'f', 0);
}

} // namespace

ProcessParameterSchema OuterContourChamferStrategy::parameterSchema() const
{
    ProcessParameterSchema schema = ProcessParameterSchema::CommonOperation();
    schema.addDefinition(numberDefinition(
        QStringLiteral("feedHeight"), ProcessParameterUnit::Millimeter, 3.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("chamferWidth"), ProcessParameterUnit::Millimeter, 1.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("chamferAngle"), ProcessParameterUnit::None, 45.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("toolTipRadius"), ProcessParameterUnit::Millimeter, 0.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("tipCompensation"), ProcessParameterUnit::Millimeter, 0.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("sideClearance"), ProcessParameterUnit::Millimeter, 0.5));
    schema.addDefinition(numberDefinition(
        QStringLiteral("overcut"), ProcessParameterUnit::Millimeter, 1.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("leadLength"), ProcessParameterUnit::Millimeter, 3.0));
    return schema;
}

StrategyParams OuterContourChamferStrategy::defaultParams() const
{
    StrategyParams params = parameterSchema().defaultParams();
    params.set(QStringLiteral("safeHeight"), 50.0);
    params.set(QStringLiteral("plungeHeight"), 3.0);
    params.set(QStringLiteral("referenceHeight"), 0.0);
    params.set(QStringLiteral("depth"), 1.0);
    params.set(QStringLiteral("stepOver"), 1.0);
    params.set(QStringLiteral("stepDown"), 0.5);
    params.set(QStringLiteral("feedRate"), 300.0);
    params.set(QStringLiteral("plungeRate"), 100.0);
    params.set(QStringLiteral("spindleSpeed"), 3000.0);
    return params;
}

ToolpathResult OuterContourChamferStrategy::generate(
    const HoleFeature &,
    const ToolEntry &,
    const StrategyParams &) const
{
    return {QString(), false, QObject::tr("外形倒角需要平面闭合轮廓。"), 0.0,
            ParametricToolpathProgram()};
}

ToolpathResult OuterContourChamferStrategy::generate(
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
        tool.type != QStringLiteral("chamfer_mill")) {
        result.errorMsg = QObject::tr("外形倒角必须选择有效倒角刀。");
        return result;
    }
    if (!feature.islands.isEmpty() || !isPlanarConvexLoop(feature)) {
        result.errorMsg = QObject::tr(
            "当前外形倒角仅支持无孤岛、平面、凸闭合轮廓；其他几何已阻止生成。");
        return result;
    }

    const double width = params.get(QStringLiteral("chamferWidth"));
    const double angle = params.get(QStringLiteral("chamferAngle"));
    const double tipRadius = params.get(QStringLiteral("toolTipRadius"));
    const double requestedTipComp = params.get(QStringLiteral("tipCompensation"));
    const double sideClearance = params.get(QStringLiteral("sideClearance"));
    const double stepDown = params.get(QStringLiteral("stepDown"));
    const double overcut = params.get(QStringLiteral("overcut"));
    const double leadLength = params.get(QStringLiteral("leadLength"));
    const double safeHeight = params.get(QStringLiteral("safeHeight"));
    const double feedHeight = params.get(QStringLiteral("feedHeight"));
    const double feedRate = params.get(QStringLiteral("feedRate"));
    const double plungeRate = params.get(QStringLiteral("plungeRate"));
    const double spindleSpeed = params.get(QStringLiteral("spindleSpeed"));
    if (width <= 0.0 || angle <= 0.0 || angle >= 89.9 || tipRadius < 0.0 ||
        requestedTipComp < 0.0 || sideClearance < 0.0 || stepDown <= 0.0 ||
        overcut < 0.0 || leadLength <= 0.0 || feedHeight < 0.0 ||
        safeHeight <= feature.center.z() + feedHeight || feedRate <= 0.0 ||
        plungeRate <= 0.0 || spindleSpeed <= 0.0) {
        result.errorMsg = QObject::tr("外形倒角尺寸、分层、进退刀或安全高度参数无效。");
        return result;
    }

    bool angleOk = false;
    bool radiusOk = false;
    const double includedAngle =
        tool.extra.value(QStringLiteral("includedAngle")).toDouble(&angleOk);
    const double actualTipRadius =
        tool.extra.value(QStringLiteral("tipRadius")).toDouble(&radiusOk);
    if (!angleOk || !radiusOk || includedAngle <= 0.0 || actualTipRadius < 0.0 ||
        std::abs(includedAngle * 0.5 - angle) > 0.05 ||
        std::abs(actualTipRadius - tipRadius) > 0.01) {
        result.errorMsg = QObject::tr(
            "倒角刀缺少可验证的夹角/刀尖半径元数据，或与工序参数不一致。");
        return result;
    }

    const double angleRadians = angle * kPi / 180.0;
    const double calculatedTipComp =
        roundedTipCompensation(tipRadius, angleRadians);
    if (std::abs(requestedTipComp - calculatedTipComp) > 0.01) {
        result.errorMsg = QObject::tr(
            "刀尖补偿与刀尖半径及倒角角度的反算结果不一致。");
        return result;
    }
    if (tool.diameter * 0.5 + kGeometryTolerance < width + sideClearance) {
        result.errorMsg = QObject::tr("倒角刀包络不足以覆盖倒角宽度和侧面间隙。");
        return result;
    }

    const double firstEdgeLength =
        edgeLength(feature.points.first(), feature.points.at(1));
    if (overcut >= firstEdgeLength) {
        result.errorMsg = QObject::tr("闭合重叠必须小于起始边长度。");
        return result;
    }
    const double totalDepth = width / std::tan(angleRadians) + calculatedTipComp;
    if (totalDepth <= 0.0 ||
        (tool.fluteLen > 0.0 && totalDepth > tool.fluteLen + 0.01) ||
        (tool.totalLen > 0.0 && totalDepth >= tool.totalLen - 0.01)) {
        result.errorMsg = QObject::tr("反算倒角深度超出刀具可验证切削长度。");
        return result;
    }

    const QVector3D first = feature.points.first();
    const QVector3D second = feature.points.at(1);
    const double directionX = (second.x() - first.x()) / firstEdgeLength;
    const double directionY = (second.y() - first.y()) / firstEdgeLength;
    const double leadX = first.x() - directionX * leadLength;
    const double leadY = first.y() - directionY * leadLength;
    const int layerCount = int(std::ceil(totalDepth / stepDown));

    QString gcode;
    gcode += QStringLiteral("T%1 M6\n").arg(tool.id);
    gcode += QStringLiteral("S%1 M3\n").arg(spindleSpeed, 0, 'f', 0);
    gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
    double totalLength = 0.0;
    for (int layer = 1; layer <= layerCount; ++layer) {
        const double layerDepth = std::min(layer * stepDown, totalDepth);
        const double layerZ = feature.center.z() - layerDepth;
        gcode += QStringLiteral("(OUTER CHAMFER LAYER %1)\n").arg(layer);
        gcode += QStringLiteral("G0 X%1 Y%2\n")
                     .arg(leadX, 0, 'f', 3)
                     .arg(leadY, 0, 'f', 3);
        gcode += QStringLiteral("G0 Z%1\n")
                     .arg(feature.center.z() + feedHeight, 0, 'f', 3);
        gcode += QStringLiteral("G1 Z%1 F%2\n")
                     .arg(layerZ, 0, 'f', 3)
                     .arg(plungeRate, 0, 'f', 0);
        gcode += feedXY(first.x(), first.y(), feedRate);
        totalLength += leadLength;
        QVector3D previous = first;
        for (int index = 1; index < feature.points.size(); ++index) {
            const QVector3D point = feature.points.at(index);
            gcode += feedXY(point.x(), point.y(), feedRate);
            totalLength += edgeLength(previous, point);
            previous = point;
        }
        gcode += feedXY(first.x(), first.y(), feedRate);
        totalLength += edgeLength(previous, first);
        if (overcut > 0.0) {
            gcode += feedXY(first.x() + directionX * overcut,
                            first.y() + directionY * overcut, feedRate);
            totalLength += overcut;
        }
        gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
    }

    result.gcode = gcode;
    result.ok = true;
    result.estimatedTimeS = totalLength / feedRate * 60.0 +
                            totalDepth / plungeRate * 60.0;
    return result;
}
