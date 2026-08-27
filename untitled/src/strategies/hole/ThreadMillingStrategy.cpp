#include "ThreadMillingStrategy.h"

#include "HoleStrategyUtils.h"

#include <QObject>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

ProcessParameterDefinition NumberDefinition(const QString &id,
                                            double defaultValue,
                                            double minimum)
{
    ProcessParameterDefinition definition;
    definition.id = id;
    definition.defaultValue = defaultValue;
    definition.minimum = minimum;
    definition.hasMinimum = true;
    definition.required = true;
    return definition;
}

ProcessParameterDefinition IntegerDefinition(const QString &id,
                                             int defaultValue,
                                             int minimum,
                                             int maximum)
{
    ProcessParameterDefinition definition;
    definition.id = id;
    definition.type = ProcessParameterType::Integer;
    definition.defaultValue = defaultValue;
    definition.minimum = minimum;
    definition.maximum = maximum;
    definition.hasMinimum = true;
    definition.hasMaximum = true;
    definition.required = true;
    return definition;
}

} // namespace

ProcessParameterSchema ThreadMillingStrategy::parameterSchema() const
{
    ProcessParameterSchema schema = ProcessParameterSchema::CommonOperation();
    ProcessParameterDefinition pitch = NumberDefinition(
        QStringLiteral("threadPitch"), 1.5, 0.001);
    pitch.unit = ProcessParameterUnit::Millimeter;
    schema.addDefinition(pitch);
    schema.addDefinition(IntegerDefinition(QStringLiteral("threadSide"), 0, 0, 1));
    schema.addDefinition(IntegerDefinition(QStringLiteral("threadHand"), 0, 0, 1));
    schema.addDefinition(IntegerDefinition(QStringLiteral("cutDirection"), 0, 0, 1));
    schema.addDefinition(IntegerDefinition(QStringLiteral("toothCount"), 1, 1, 100));
    schema.addDefinition(NumberDefinition(QStringLiteral("diameterCorrection"), 0.0, -1000.0));
    ProcessParameterDefinition taper = NumberDefinition(
        QStringLiteral("taperAngle"), 0.0, 0.0);
    taper.maximum = 30.0;
    taper.hasMaximum = true;
    schema.addDefinition(taper);
    schema.addDefinition(IntegerDefinition(QStringLiteral("roughPasses"), 0, 0, 20));
    ProcessParameterDefinition finishAllowance = NumberDefinition(
        QStringLiteral("finishAllowance"), 0.0, 0.0);
    finishAllowance.unit = ProcessParameterUnit::Millimeter;
    schema.addDefinition(finishAllowance);
    ProcessParameterDefinition finishFeed = NumberDefinition(
        QStringLiteral("finishFeedRate"), 300.0, 0.001);
    finishFeed.unit = ProcessParameterUnit::MillimeterPerMinute;
    schema.addDefinition(finishFeed);
    ProcessParameterDefinition finishSpindle = NumberDefinition(
        QStringLiteral("finishSpindleSpeed"), 2500.0, 0.001);
    finishSpindle.unit = ProcessParameterUnit::RevolutionPerMinute;
    schema.addDefinition(finishSpindle);
    return schema;
}

StrategyParams ThreadMillingStrategy::defaultParams() const
{
    StrategyParams params;
    params.set(QStringLiteral("safeHeight"), 50.0);
    params.set(QStringLiteral("feedHeight"), 3.0);
    params.set(QStringLiteral("depth"), 10.0);
    params.set(QStringLiteral("spindleSpeed"), 2500.0);
    params.set(QStringLiteral("feedRate"), 300.0);
    params.set(QStringLiteral("plungeRate"), 100.0);
    params.set(QStringLiteral("threadPitch"), 1.5);
    params.set(QStringLiteral("threadSide"), 0.0);
    params.set(QStringLiteral("threadHand"), 0.0);
    params.set(QStringLiteral("cutDirection"), 0.0);
    params.set(QStringLiteral("toothCount"), 1.0);
    params.set(QStringLiteral("diameterCorrection"), 0.0);
    params.set(QStringLiteral("taperAngle"), 0.0);
    params.set(QStringLiteral("roughPasses"), 0.0);
    params.set(QStringLiteral("finishAllowance"), 0.0);
    params.set(QStringLiteral("finishFeedRate"), 300.0);
    params.set(QStringLiteral("finishSpindleSpeed"), 2500.0);
    return params;
}

ToolpathResult ThreadMillingStrategy::generate(
    const HoleFeature &feature,
    const ToolEntry &tool,
    const StrategyParams &params) const
{
    return generate(QVector<HoleFeature>{feature}, tool, params);
}

ToolpathResult ThreadMillingStrategy::generate(
    const QVector<HoleFeature> &features,
    const ToolEntry &tool,
    const StrategyParams &params) const
{
    ToolpathResult result;
    if (features.isEmpty()) {
        result.errorMsg = QObject::tr("未选择圆形或圆柱螺纹几何。");
        return result;
    }
    if (tool.id <= 0 || tool.type != QStringLiteral("thread_mill") ||
        tool.diameter <= 0.0 || tool.pitch <= 0.0) {
        result.errorMsg = QObject::tr("铣牙必须使用已定义直径和牙距的螺纹铣刀。");
        return result;
    }

    const double pitch = params.get(QStringLiteral("threadPitch"), 1.5);
    const int threadSide = static_cast<int>(params.get(QStringLiteral("threadSide"), 0.0));
    const int threadHand = static_cast<int>(params.get(QStringLiteral("threadHand"), 0.0));
    const int cutDirection = static_cast<int>(params.get(QStringLiteral("cutDirection"), 0.0));
    const int toothCount = static_cast<int>(params.get(QStringLiteral("toothCount"), 1.0));
    const double diameterCorrection = params.get(
        QStringLiteral("diameterCorrection"), 0.0);
    const double taperAngle = params.get(QStringLiteral("taperAngle"), 0.0);
    const int roughPasses = static_cast<int>(
        params.get(QStringLiteral("roughPasses"), 0.0));
    const double finishAllowance = params.get(
        QStringLiteral("finishAllowance"), 0.0);
    const double feedRate = params.get(QStringLiteral("feedRate"), 300.0);
    const double finishFeedRate = params.get(
        QStringLiteral("finishFeedRate"), feedRate);
    const double finishSpindleSpeed = params.get(
        QStringLiteral("finishSpindleSpeed"),
        params.get(QStringLiteral("spindleSpeed"), 2500.0));
    const double safeHeight = params.get(QStringLiteral("safeHeight"), 50.0);
    const double requestedDepth = params.get(QStringLiteral("depth"), 0.0);
    if (pitch <= 0.0 || feedRate <= 0.0 || finishFeedRate <= 0.0 ||
        finishSpindleSpeed <= 0.0 || requestedDepth <= 0.0 ||
        taperAngle < 0.0 || taperAngle > 30.0 || roughPasses < 0 ||
        finishAllowance < 0.0) {
        result.errorMsg = QObject::tr("铣牙牙距、深度和进给必须大于零。");
        return result;
    }
    if (std::abs(tool.pitch - pitch) > std::max(0.01, pitch * 0.02)) {
        result.errorMsg = QObject::tr("螺纹铣刀牙距与工序牙距不匹配。");
        return result;
    }
    if (threadSide != 0) {
        result.errorMsg = QObject::tr("外螺纹铣削需要确认外圆毛坯边界，当前版本禁止生成。");
        return result;
    }
    if (toothCount != 1) {
        result.errorMsg = QObject::tr("多齿换层抬刀几何尚未定义，当前仅允许单齿螺纹铣刀。");
        return result;
    }
    if ((threadHand != 0 && threadHand != 1) ||
        (cutDirection != 0 && cutDirection != 1)) {
        result.errorMsg = QObject::tr("螺纹旋向或加工方向无效。");
        return result;
    }

    const QVector<HoleFeature> ordered = sortHolesByNearestNeighbor(features);
    QString gcode;
    gcode += QStringLiteral("T%1 M6\n").arg(tool.id);
    gcode += QStringLiteral("S%1 M3\n")
                 .arg(static_cast<int>(params.get(QStringLiteral("spindleSpeed"), 2500.0)));
    gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
    double totalLength = 0.0;
    for (const HoleFeature &feature : ordered) {
        const double holeRadius = feature.radius;
        const double maximumPathRadius = holeRadius - tool.diameter * 0.5;
        const double finalTopRadius = maximumPathRadius + diameterCorrection * 0.5;
        if (holeRadius <= 0.0 || feature.depth <= 0.0 || finalTopRadius <= 0.0 ||
            finalTopRadius > maximumPathRadius + 1.0e-6) {
            result.errorMsg = QObject::tr("螺纹铣刀直径必须小于选定内圆柱，且螺纹深度必须可确定。");
            return result;
        }
        const double depth = std::min(requestedDepth, feature.depth);
        if (tool.fluteLen > 0.0 && depth > tool.fluteLen + 0.01) {
            result.errorMsg = QObject::tr("螺纹深度超过刀具有效刃长。");
            return result;
        }
        const HoleZRange range = holeZRange(feature, depth, 0.0);
        const double taperPerDepth = std::tan(taperAngle * kPi / 180.0);
        const double finalBottomRadius = finalTopRadius - depth * taperPerDepth;
        if (finalBottomRadius <= 0.0 ||
            finishAllowance >= finalBottomRadius) {
            result.errorMsg = QObject::tr("锥度或精加工余量使螺纹刀具中心半径失效，禁止生成。");
            return result;
        }
        const double turns = depth / pitch;
        const int segments = std::max(48, static_cast<int>(std::ceil(turns * 48.0)));
        const double directionSign = threadHand == cutDirection ? -1.0 : 1.0;
        const double centerX = feature.center.x();
        const double centerY = feature.center.y();

        gcode += QStringLiteral(
                     ";CNEXT_THREAD_MILL side=internal hand=%1 direction=%2 pitch=%3 teeth=1\n")
                     .arg(threadHand == 0 ? QStringLiteral("right") : QStringLiteral("left"))
                     .arg(cutDirection == 0 ? QStringLiteral("top-down") : QStringLiteral("bottom-up"))
                     .arg(pitch, 0, 'f', 3);
        const int totalPasses = roughPasses + 1;
        for (int pass = 0; pass < totalPasses; ++pass) {
            const bool finishPass = pass == roughPasses;
            const double remainingStock = finishPass
                ? 0.0
                : finishAllowance * static_cast<double>(roughPasses - pass) /
                      std::max(1, roughPasses);
            const double passTopRadius = finalTopRadius - remainingStock;
            const double passBottomRadius = finalBottomRadius - remainingStock;
            if (passBottomRadius <= 0.0) {
                result.errorMsg = QObject::tr("粗加工分层使螺纹刀具中心半径失效，禁止生成。");
                return result;
            }
            const double passFeed = finishPass ? finishFeedRate : feedRate;
            const double startZ = cutDirection == 0 ? range.entryZ : range.bottomZ;
            const double endZ = cutDirection == 0 ? range.bottomZ : range.entryZ;
            const double startRadius = cutDirection == 0
                ? passTopRadius
                : passBottomRadius;

            if (finishPass) {
                gcode += QStringLiteral("S%1 M3\n")
                             .arg(static_cast<int>(finishSpindleSpeed));
            }
            gcode += QStringLiteral(";CNEXT_THREAD_PASS pass=%1\n")
                         .arg(finishPass
                                  ? QStringLiteral("finish")
                                  : QStringLiteral("rough-%1").arg(pass + 1));
            gcode += QStringLiteral("G0 X%1 Y%2\n")
                         .arg(centerX, 0, 'f', 3)
                         .arg(centerY, 0, 'f', 3);
            gcode += QStringLiteral("G0 Z%1\n").arg(startZ, 0, 'f', 3);
            gcode += QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
                         .arg(centerX + startRadius, 0, 'f', 3)
                         .arg(centerY, 0, 'f', 3)
                         .arg(startZ, 0, 'f', 3)
                         .arg(passFeed, 0, 'f', 3);
            for (int segment = 1; segment <= segments; ++segment) {
                const double fraction = static_cast<double>(segment) / segments;
                const double topDepth = cutDirection == 0
                    ? depth * fraction
                    : depth * (1.0 - fraction);
                const double radius = passTopRadius - topDepth * taperPerDepth;
                const double angle = directionSign * 2.0 * kPi * turns * fraction;
                gcode += QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
                             .arg(centerX + std::cos(angle) * radius, 0, 'f', 3)
                             .arg(centerY + std::sin(angle) * radius, 0, 'f', 3)
                             .arg(startZ + (endZ - startZ) * fraction, 0, 'f', 3)
                             .arg(passFeed, 0, 'f', 3);
            }
            gcode += QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
                         .arg(centerX, 0, 'f', 3)
                         .arg(centerY, 0, 'f', 3)
                         .arg(endZ, 0, 'f', 3)
                         .arg(passFeed, 0, 'f', 3);
            gcode += QStringLiteral("G0 Z%1\n").arg(safeHeight, 0, 'f', 3);
            totalLength += std::hypot(
                2.0 * kPi * turns * (passTopRadius + passBottomRadius) * 0.5,
                depth);
        }
    }

    result.gcode = gcode;
    result.ok = true;
    result.estimatedTimeS = totalLength / feedRate * 60.0;
    return result;
}
