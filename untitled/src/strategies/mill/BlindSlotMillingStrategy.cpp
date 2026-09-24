#include "BlindSlotMillingStrategy.h"

#include <QObject>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// 函数说明：rapidXY负责当前模块的核心处理。
static QString rapidXY(double x, double y)
{
    return QStringLiteral("G0 X%1 Y%2\n").arg(x, 0, 'f', 6).arg(y, 0, 'f', 6);
}

// 函数说明：feedXY负责当前模块的核心处理。
static QString feedXY(double x, double y, double feed)
{
    return QStringLiteral("G1 X%1 Y%2 F%3\n")
        .arg(x, 0, 'f', 6)
        .arg(y, 0, 'f', 6)
        .arg(int(feed));
}

// 函数说明：feedXYZ负责当前模块的核心处理。
static QString feedXYZ(double x, double y, double z, double feed)
{
    return QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
        .arg(x, 0, 'f', 6)
        .arg(y, 0, 'f', 6)
        .arg(z, 0, 'f', 6)
        .arg(int(feed));
}

// 函数说明：slotLength负责当前模块的核心处理。
static double slotLength(const ContourFeature &feature, const StrategyParams &params)
{
    return params.get(QStringLiteral("slotLength"),
                      feature.length > 0.0 ? feature.length : feature.radius * 2.0);
}

// 函数说明：slotWidth负责当前模块的核心处理。
static double slotWidth(const ContourFeature &feature, const StrategyParams &params)
{
    return params.get(QStringLiteral("slotWidth"),
                      feature.width > 0.0 ? feature.width : feature.radius);
}

// 函数说明：slotAngleDeg负责当前模块的核心处理。
static double slotAngleDeg(const ContourFeature &feature, const StrategyParams &params)
{
    return params.get(QStringLiteral("angle"), feature.angle);
}

// 函数说明：roughScanOffsets负责当前模块的核心处理。
static std::vector<double> roughScanOffsets(double halfWidth, double stepover, double toolDiameter)
{
    std::vector<double> offsets;
    if (halfWidth <= 1.0e-6) {
        offsets.push_back(0.0);
        return offsets;
    }

    const double effectiveStep = std::max(0.01, std::min(stepover, toolDiameter * 0.45));
    offsets.push_back(-halfWidth);
    offsets.push_back(0.0);
    offsets.push_back(halfWidth);

    for (double v = -halfWidth + effectiveStep; v < halfWidth - 1.0e-6; v += effectiveStep) {
        offsets.push_back(v);
    }

    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end(), [](double a, double b) {
        return std::abs(a - b) <= 1.0e-6;
    }), offsets.end());
    return offsets;
}

// 函数说明：finishScanOffsets负责当前模块的核心处理。
static std::vector<double> finishScanOffsets(double halfWidth, double stepover, double toolDiameter)
{
    std::vector<double> offsets;
    if (halfWidth <= 1.0e-6) {
        offsets.push_back(0.0);
        return offsets;
    }

    const double effectiveStep = std::max(0.01, std::min(stepover * 0.5, toolDiameter * 0.35));
    offsets.push_back(-halfWidth);
    offsets.push_back(0.0);
    offsets.push_back(halfWidth);

    for (double value = -halfWidth + effectiveStep; value < halfWidth - 1.0e-6; value += effectiveStep) {
        offsets.push_back(value);
    }

    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end(), [](double a, double b) {
        return std::abs(a - b) <= 1.0e-6;
    }), offsets.end());
    return offsets;
}

// 函数说明：slotLayerRange负责当前模块的核心处理。
static void slotLayerRange(double halfLength,
                           double slopeStartLength,
                           double slopeEndLength,
                           double depthRatio,
                           double centerInset,
                           double &minU,
                           double &maxU)
{
    const double clampedRatio = std::max(0.0, std::min(depthRatio, 1.0));
    minU = -halfLength + std::max(0.0, slopeStartLength) * clampedRatio + centerInset;
    maxU =  halfLength - std::max(0.0, slopeEndLength) * clampedRatio - centerInset;
}

// 函数说明：slotLayerWidthRange负责当前模块的核心处理。
static void slotLayerWidthRange(double halfWidth,
                                double slopeMinWidth,
                                double slopeMaxWidth,
                                double depthRatio,
                                double centerInset,
                                double &minV,
                                double &maxV)
{
    const double clampedRatio = std::max(0.0, std::min(depthRatio, 1.0));
    minV = -halfWidth + std::max(0.0, slopeMinWidth) * clampedRatio + centerInset;
    maxV =  halfWidth - std::max(0.0, slopeMaxWidth) * clampedRatio - centerInset;
}

} // namespace

// 函数说明：defaultParams负责当前模块的核心处理。
StrategyParams BlindSlotMillingStrategy::defaultParams() const
{
    StrategyParams p;
    p.set(QStringLiteral("safeHeight"),   50.0);
    p.set(QStringLiteral("feedHeight"),    3.0);
    p.set(QStringLiteral("depth"),        10.0);
    p.set(QStringLiteral("stepDown"),      2.0);
    p.set(QStringLiteral("stepover"),      5.0);
    p.set(QStringLiteral("spindleSpeed"), 2000.0);
    p.set(QStringLiteral("feedRate"),      800.0);
    p.set(QStringLiteral("plungeRate"),    200.0);
    p.set(QStringLiteral("stockToLeave"),  0.1);
    p.set(QStringLiteral("compensation"),  0.0);
    p.set(QStringLiteral("slotLength"),   50.0);
    p.set(QStringLiteral("slotWidth"),    10.0);
    p.set(QStringLiteral("angle"),         0.0);
    p.set(QStringLiteral("helixRadius"),   0.0);
    p.set(QStringLiteral("helixPitch"),    0.0);
    p.set(QStringLiteral("rampAngle"),     3.0);
    return p;
}

// 函数说明：generate负责当前模块的核心处理。
ToolpathResult BlindSlotMillingStrategy::generate(const HoleFeature &,
                                                  const ToolEntry &,
                                                  const StrategyParams &) const
{
    return {QString(), false, QObject::tr("Blind slot milling requires a contour feature."), 0.0};
}

// 函数说明：generate负责当前模块的核心处理。
ToolpathResult BlindSlotMillingStrategy::generate(const ContourFeature &feature,
                                                  const ToolEntry      &tool,
                                                  const StrategyParams &params) const
{
    ToolpathResult res;

    if (tool.diameter <= 0.0) {
        res.errorMsg = QObject::tr("The selected tool diameter is invalid.");
        return res;
    }
    if (feature.depth <= 0.0) {
        res.errorMsg = QObject::tr("Slot depth must be greater than 0.");
        return res;
    }

    const double safe = params.get(QStringLiteral("safeHeight"), 50.0);
    const double feedH = params.get(QStringLiteral("feedHeight"), 3.0);
    const double stepDown = params.get(QStringLiteral("stepDown"), 2.0);
    const double stepover = params.get(QStringLiteral("stepover"), 5.0);
    const double spindleSpeed = params.get(QStringLiteral("spindleSpeed"), 2000.0);
    const double feedRate = params.get(QStringLiteral("feedRate"), 800.0);
    const double plungeRate = params.get(QStringLiteral("plungeRate"), 200.0);
    const double rampAngle = params.get(QStringLiteral("rampAngle"), 3.0);
    const double stockToLeave = std::max(0.0, params.get(QStringLiteral("stockToLeave"), 0.1));
    const double comp = params.get(QStringLiteral("compensation"), 0.0);
    const double targetDepth = std::min(params.get(QStringLiteral("depth"), feature.depth), feature.depth);
    const double fullLength = slotLength(feature, params);
    const double fullWidth = slotWidth(feature, params);
    const double angleDeg = slotAngleDeg(feature, params);

    if (fullLength <= 0.0 || fullWidth <= 0.0) {
        res.errorMsg = QObject::tr("Slot length and width must both be greater than 0.");
        return res;
    }
    if (stepDown <= 0.0 || stepover <= 0.0) {
        res.errorMsg = QObject::tr("Step down and stepover must both be greater than 0.");
        return res;
    }
    if (feedRate <= 0.0 || plungeRate <= 0.0) {
        res.errorMsg = QObject::tr("Feed rate and plunge rate must both be greater than 0.");
        return res;
    }
    if (targetDepth <= 0.0) {
        res.errorMsg = QObject::tr("Slot depth must be greater than 0.");
        return res;
    }
    if (tool.diameter >= fullWidth) {
        res.errorMsg = QObject::tr("The tool diameter must be smaller than the slot width.");
        return res;
    }
    const double halfLen = fullLength * 0.5;
    const double halfWid = fullWidth * 0.5;
    const double angleRad = angleDeg * std::acos(-1.0) / 180.0;
    const double helixRadius = params.get(QStringLiteral("helixRadius"), 0.0);
    double helixPitch = params.get(QStringLiteral("helixPitch"), 0.0);
    if (helixRadius > 1.0e-6 && helixPitch <= 0.0) {
        helixPitch = std::min(stepDown * 0.5, tool.diameter * 0.15);
    }

    const double cx = feature.center.x();
    const double cy = feature.center.y();
    const double zTop = feature.center.z();
    const double retractZ = zTop + feedH;
    const double cosA = std::cos(angleRad);
    const double sinA = std::sin(angleRad);
    if (isUnsupportedG42(comp)) {
        res.errorMsg = QObject::tr("G42 is not supported for this counterclockwise blind slot contour: it offsets the cutter outside the slot. Use CAM offset (compensation = 0).");
        return res;
    }
    const bool camOffset = std::abs(comp) < 1.0e-9;
    const double roughCenterInset = tool.diameter * 0.5 + stockToLeave;
    const double finishCenterInset = camOffset ? tool.diameter * 0.5 : 0.0;
    if (!std::isfinite(rampAngle) || rampAngle <= 0.0 || rampAngle > 10.0) {
        res.errorMsg = QObject::tr("Ramp angle must be greater than 0 and at most 10 degrees.");
        return res;
    }
    const double rampAngleForLimit = rampAngle;
    const double coordinateQuantum = 0.000001;
    const double rampTangent = std::tan(rampAngle * std::acos(-1.0) / 180.0);
    const double slopeStartLength = feature.slopeStartLength;
    const double slopeEndLength = feature.slopeEndLength;
    const double slopeMinWidth = feature.slopeMinWidth;
    const double slopeMaxWidth = feature.slopeMaxWidth;
    const double targetRatio = targetDepth / feature.depth;
    if (!camOffset && (slopeStartLength != 0.0 || slopeEndLength != 0.0 ||
                       slopeMinWidth != 0.0 || slopeMaxWidth != 0.0)) {
        res.errorMsg = QObject::tr("Controller compensation is not supported for sloped slot walls: the finish entry cutter envelope is not proven. Use CAM offset.");
        return res;
    }
    if (slopeStartLength < 0.0 || slopeEndLength < 0.0 ||
        slopeMinWidth < 0.0 || slopeMaxWidth < 0.0 || helixRadius < 0.0) {
        res.errorMsg = QObject::tr("Slot slopes and helix radius must not be negative.");
        return res;
    }
    // Inward linear walls are narrowest at the deepest requested layer.
    // Preserve the recognized geometry; an infeasible cutter must be rejected.
    const double bottomMinU = -halfLen + slopeStartLength * targetRatio + roughCenterInset;
    const double bottomMaxU = halfLen - slopeEndLength * targetRatio - roughCenterInset;
    const double bottomMinV = -halfWid + slopeMinWidth * targetRatio + roughCenterInset;
    const double bottomMaxV = halfWid - slopeMaxWidth * targetRatio - roughCenterInset;
    if (bottomMaxU <= bottomMinU || bottomMaxV <= bottomMinV) {
        res.errorMsg = QObject::tr("The true slot bottom cannot fit the selected tool and stock allowance; slope geometry was not changed.");
        return res;
    }
    if (bottomMinU > 0.0 || bottomMaxU < 0.0 || bottomMinV > 0.0 || bottomMaxV < 0.0 ||
        bottomMaxU - bottomMinU + 2.0 * stockToLeave < tool.diameter * 0.5) {
        res.errorMsg = QObject::tr("The slot does not support the strategy's center entry and wall finish lead with this tool.");
        return res;
    }
    if (helixRadius > 1.0e-6 &&
        (helixRadius > -bottomMinU || helixRadius > bottomMaxU ||
         helixRadius > -bottomMinV || helixRadius > bottomMaxV)) {
        res.errorMsg = QObject::tr("The requested helix and cutter envelope exceed the true slot walls or stock allowance.");
        return res;
    }
    // The deepest inward rectangle is contained in every previously cleared layer.
    // Reserve endpoint rounding: delta Z <= q, delta XY length <= sqrt(2)*q.
    const double centerRampLength = (bottomMaxU - bottomMinU) * 0.5;
    const double centerRampStepDown =
        (centerRampLength - 2.0 * std::sqrt(2.0) * coordinateQuantum) * rampTangent - coordinateQuantum;
    const double effectiveStepDown = helixRadius > 1.0e-6
                                         ? std::min(stepDown, targetDepth)
                                         : std::min(stepDown, centerRampStepDown);
    if (effectiveStepDown < 0.005) {
        res.errorMsg = QObject::tr("Slot is too short for ramp plunging with the selected tool and stock allowance.");
        return res;
    }
    double finishMinU = 0.0;
    double finishMaxU = 0.0;
    slotLayerRange(halfLen,
                   slopeStartLength,
                   slopeEndLength,
                   targetRatio,
                   finishCenterInset,
                   finishMinU,
                   finishMaxU);
    double finishMinV = 0.0;
    double finishMaxV = 0.0;
    slotLayerWidthRange(halfWid,
                        slopeMinWidth,
                        slopeMaxWidth,
                        targetRatio,
                        finishCenterInset,
                        finishMinV,
                        finishMaxV);
    double bottomFinishMinU = 0.0;
    double bottomFinishMaxU = 0.0;
    slotLayerRange(halfLen,
                   slopeStartLength,
                   slopeEndLength,
                   targetRatio,
                   tool.diameter * 0.5,
                   bottomFinishMinU,
                   bottomFinishMaxU);
    double bottomFinishMinV = 0.0;
    double bottomFinishMaxV = 0.0;
    slotLayerWidthRange(halfWid,
                        slopeMinWidth,
                        slopeMaxWidth,
                        targetRatio,
                        tool.diameter * 0.5,
                        bottomFinishMinV,
                        bottomFinishMaxV);
    const int layerCount = static_cast<int>(std::ceil(targetDepth / effectiveStepDown));

    const bool finishRangeValid = finishMaxU > finishMinU && finishMaxV > finishMinV;
    const bool bottomFinishRangeValid = bottomFinishMaxU > bottomFinishMinU &&
                                        bottomFinishMaxV > bottomFinishMinV;
    const double topFinishMinU = -halfLen + tool.diameter * 0.5;
    const double topFinishMaxU =  halfLen - tool.diameter * 0.5;
    const double topFinishMinV = -halfWid + tool.diameter * 0.5;
    const double topFinishMaxV =  halfWid - tool.diameter * 0.5;
    const double slopeFinishMinLength = std::max(0.05, tool.diameter * 0.05);

    auto slotToWorld = [&](double u, double v, double &x, double &y) {
        x = cx + u * cosA - v * sinA;
        y = cy + u * sinA + v * cosA;
    };

    auto appendRampPlunge = [&](QString &out,
                                double targetU,
                                double targetV,
                                double minU,
                                double maxU,
                                double startZ,
                                double targetZ) {
        const double verticalDrop = startZ - targetZ;
        const double desiredLength = (verticalDrop + coordinateQuantum) / rampTangent +
                                     std::sqrt(2.0) * coordinateQuantum;
        const double backward = targetU - minU;
        const double forward = maxU - targetU;
        if (verticalDrop <= 0.0 || desiredLength > std::max(backward, forward)) {
            return false;
        }
        const double startU = backward >= forward ? targetU - desiredLength : targetU + desiredLength;
        double startX = 0.0;
        double startY = 0.0;
        double endX = 0.0;
        double endY = 0.0;
        slotToWorld(startU, targetV, startX, startY);
        slotToWorld(targetU, targetV, endX, endY);
        out += QStringLiteral("G0 Z%1\n").arg(zTop + feedH, 0, 'f', 6);
        out += rapidXY(startX, startY);
        out += QStringLiteral("G1 Z%1 F%2\n").arg(startZ, 0, 'f', 6).arg(int(plungeRate));
        out += QStringLiteral("; Blind slot ramp plunge angle %1\n").arg(rampAngle, 0, 'f', 3);
        out += feedXYZ(endX, endY, targetZ, plungeRate);
        return true;
    };

    QString gcode;
    gcode += QStringLiteral("T%1 M6\n").arg(tool.id);
    gcode += QStringLiteral("S%1 M3\n").arg(int(spindleSpeed));
    gcode += QStringLiteral("M8\n");
    gcode += QStringLiteral("; Blind slot geometry: L=%1 W=%2 D=%3 A=%4\n")
                 .arg(fullLength, 0, 'f', 3)
                 .arg(fullWidth, 0, 'f', 3)
                 .arg(targetDepth, 0, 'f', 3)
                 .arg(angleDeg, 0, 'f', 3);
    gcode += QStringLiteral("; Blind slot slopes: startU=%1 endU=%2 minV=%3 maxV=%4\n")
                 .arg(slopeStartLength, 0, 'f', 3)
                 .arg(slopeEndLength, 0, 'f', 3)
                 .arg(slopeMinWidth, 0, 'f', 3)
                 .arg(slopeMaxWidth, 0, 'f', 3);
    gcode += QStringLiteral("; Blind slot finish range: U[%1,%2] V[%3,%4]\n")
                 .arg(finishMinU, 0, 'f', 3)
                 .arg(finishMaxU, 0, 'f', 3)
                 .arg(finishMinV, 0, 'f', 3)
                 .arg(finishMaxV, 0, 'f', 3);
    gcode += QStringLiteral("; Blind slot rough stock to leave: %1\n")
                 .arg(stockToLeave, 0, 'f', 3);
    if (helixRadius <= 1.0e-6 && effectiveStepDown < stepDown - 1.0e-6) {
        gcode += QStringLiteral("; Blind slot stepdown limited by %1 deg ramp: requested=%2 effective=%3\n")
                     .arg(rampAngleForLimit, 0, 'f', 3)
                     .arg(stepDown, 0, 'f', 3)
                     .arg(effectiveStepDown, 0, 'f', 3);
    }
    gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 6);

    double totalLength = 0.0;

    for (int layer = 1; layer <= layerCount; ++layer) {
        const double zLayer = zTop - std::min(layer * effectiveStepDown, targetDepth);
        const double layerDepth = zTop - zLayer;
        const double depthRatio = targetDepth > 1.0e-6
                                      ? std::max(0.0, std::min(layerDepth / feature.depth, 1.0))
                                      : 1.0;
        double roughMinU = 0.0;
        double roughMaxU = 0.0;
        slotLayerRange(halfLen,
                       slopeStartLength,
                       slopeEndLength,
                       depthRatio,
                       roughCenterInset,
                       roughMinU,
                       roughMaxU);
        double roughMinV = 0.0;
        double roughMaxV = 0.0;
        slotLayerWidthRange(halfWid,
                            slopeMinWidth,
                            slopeMaxWidth,
                            depthRatio,
                            roughCenterInset,
                            roughMinV,
                            roughMaxV);
        gcode += QStringLiteral("; Blind slot rough layer %1/%2 Z=%3 U[%4,%5] V[%6,%7]\n")
                     .arg(layer)
                     .arg(layerCount)
                     .arg(zLayer, 0, 'f', 6)
                     .arg(roughMinU, 0, 'f', 3)
                     .arg(roughMaxU, 0, 'f', 3)
                     .arg(roughMinV, 0, 'f', 3)
                     .arg(roughMaxV, 0, 'f', 3);

        if (roughMaxU > roughMinU && roughMaxV > roughMinV) {
            if (helixRadius > 1e-6) {
                double startX = 0.0;
                double startY = 0.0;
                slotToWorld(helixRadius, 0.0, startX, startY);
                gcode += QStringLiteral("G0 Z%1\n").arg(zTop + feedH, 0, 'f', 6);
                gcode += rapidXY(startX, startY);
                const int helixLoops = static_cast<int>(std::ceil(((zTop + feedH) - zLayer) / helixPitch));
                for (int loop = 1; loop <= helixLoops; ++loop) {
                    const double zNext = std::max(zLayer, (zTop + feedH) - loop * helixPitch);
                    gcode += QStringLiteral("G2 X%1 Y%2 I%3 J%4 Z%5 F%6\n")
                                 .arg(startX, 0, 'f', 6)
                                 .arg(startY, 0, 'f', 6)
                                 .arg(-helixRadius * cosA, 0, 'f', 6)
                                 .arg(-helixRadius * sinA, 0, 'f', 6)
                                 .arg(zNext, 0, 'f', 6)
                                 .arg(int(plungeRate));
                }
                gcode += QStringLiteral("G1 X%1 Y%2 F%3\n")
                             .arg(cx, 0, 'f', 6)
                             .arg(cy, 0, 'f', 6)
                             .arg(int(feedRate));
            } else {
                const double clearedZ = zTop - std::min((layer - 1) * effectiveStepDown, targetDepth);
                if (!appendRampPlunge(gcode,
                                  (roughMinU + roughMaxU) * 0.5,
                                  (bottomMinV + bottomMaxV) * 0.5,
                                  roughMinU,
                                  roughMaxU,
                                  clearedZ,
                                  zLayer)) {
                    res.errorMsg = QObject::tr("The requested ramp cannot fit inside the current slot layer.");
                    return res;
                }
                // Sweep the same centerline at every depth. Its capsule proves that
                // the entire cutter can descend to this layer before the next ramp,
                // even when asymmetric slopes shift the ordinary raster scan lines.
                double entryX = 0.0;
                double entryY = 0.0;
                const double entryV = (bottomMinV + bottomMaxV) * 0.5;
                slotToWorld(roughMinU, entryV, entryX, entryY);
                gcode += feedXY(entryX, entryY, feedRate);
                slotToWorld(roughMaxU, entryV, entryX, entryY);
                gcode += feedXY(entryX, entryY, feedRate);
                totalLength += roughMaxU - roughMinU;
            }

            bool leftToRight = true;
            const double spanU = roughMaxU - roughMinU;
            const double spanV = roughMaxV - roughMinV;
            if (spanU >= spanV) {
                const std::vector<double> scanOffsets = roughScanOffsets(spanV * 0.5,
                                                                         stepover,
                                                                         tool.diameter);
                const double midV = (roughMinV + roughMaxV) * 0.5;
                for (double v : scanOffsets) {
                    const double layerV = midV + v;
                    double x0 = 0.0;
                    double y0 = 0.0;
                    double x1 = 0.0;
                    double y1 = 0.0;
                    if (leftToRight) {
                        slotToWorld(roughMinU, layerV, x0, y0);
                        slotToWorld(roughMaxU, layerV, x1, y1);
                    } else {
                        slotToWorld(roughMaxU, layerV, x0, y0);
                        slotToWorld(roughMinU, layerV, x1, y1);
                    }
                    gcode += feedXY(x0, y0, feedRate);
                    gcode += feedXY(x1, y1, feedRate);
                    totalLength += spanU;
                    leftToRight = !leftToRight;
                }
            } else {
                const std::vector<double> scanOffsets = roughScanOffsets(spanU * 0.5,
                                                                         stepover,
                                                                         tool.diameter);
                const double midU = (roughMinU + roughMaxU) * 0.5;
                for (double u : scanOffsets) {
                    const double layerU = midU + u;
                    double x0 = 0.0;
                    double y0 = 0.0;
                    double x1 = 0.0;
                    double y1 = 0.0;
                    if (leftToRight) {
                        slotToWorld(layerU, roughMinV, x0, y0);
                        slotToWorld(layerU, roughMaxV, x1, y1);
                    } else {
                        slotToWorld(layerU, roughMaxV, x0, y0);
                        slotToWorld(layerU, roughMinV, x1, y1);
                    }
                    gcode += feedXY(x0, y0, feedRate);
                    gcode += feedXY(x1, y1, feedRate);
                    totalLength += spanV;
                    leftToRight = !leftToRight;
                }
            }
        } else {
            gcode += QStringLiteral("; Blind slot rough skipped: no valid stock-to-leave range at Z=%1\n")
                         .arg(zLayer, 0, 'f', 6);
        }

        if (layer < layerCount) {
            gcode += QStringLiteral("G0 Z%1\n").arg(retractZ, 0, 'f', 6);
            continue;
        }

        if (bottomFinishRangeValid) {
            gcode += QStringLiteral("; Blind slot bottom finish\n");
            const double spanU = bottomFinishMaxU - bottomFinishMinU;
            const double spanV = bottomFinishMaxV - bottomFinishMinV;
            bool forward = true;

            if (spanU >= spanV) {
                const std::vector<double> scanOffsets = finishScanOffsets(spanV * 0.5,
                                                                          stepover,
                                                                          tool.diameter);
                const double midV = (bottomFinishMinV + bottomFinishMaxV) * 0.5;
                for (double offset : scanOffsets) {
                    const double layerV = midV + offset;
                    const double startU = forward ? bottomFinishMinU : bottomFinishMaxU;
                    const double endU = forward ? bottomFinishMaxU : bottomFinishMinU;
                    double x0 = 0.0;
                    double y0 = 0.0;
                    double x1 = 0.0;
                    double y1 = 0.0;
                    slotToWorld(startU, layerV, x0, y0);
                    slotToWorld(endU, layerV, x1, y1);
                    // Rough and finish rectangles are convex; this stays at the cleared bottom.
                    gcode += feedXY(x0, y0, feedRate);
                    gcode += feedXY(x1, y1, feedRate);
                    totalLength += spanU;
                    forward = !forward;
                }
            } else {
                const std::vector<double> scanOffsets = finishScanOffsets(spanU * 0.5,
                                                                          stepover,
                                                                          tool.diameter);
                const double midU = (bottomFinishMinU + bottomFinishMaxU) * 0.5;
                for (double offset : scanOffsets) {
                    const double layerU = midU + offset;
                    const double startV = forward ? bottomFinishMinV : bottomFinishMaxV;
                    const double endV = forward ? bottomFinishMaxV : bottomFinishMinV;
                    double x0 = 0.0;
                    double y0 = 0.0;
                    double x1 = 0.0;
                    double y1 = 0.0;
                    slotToWorld(layerU, startV, x0, y0);
                    slotToWorld(layerU, endV, x1, y1);
                    // Rough and finish rectangles are convex; this stays at the cleared bottom.
                    gcode += feedXY(x0, y0, feedRate);
                    gcode += feedXY(x1, y1, feedRate);
                    totalLength += spanV;
                    forward = !forward;
                }
            }
        }

        if (bottomFinishRangeValid) {
            const double slopeMidV = (bottomFinishMinV + bottomFinishMaxV) * 0.5;
            const double slopeHalfV = (bottomFinishMaxV - bottomFinishMinV) * 0.5;
            if (slopeHalfV > 1.0e-6) {
                const std::vector<double> slopeOffsets = finishScanOffsets(slopeHalfV,
                                                                           stepover,
                                                                           tool.diameter);
                if (slopeStartLength > slopeFinishMinLength &&
                    bottomFinishMinU > topFinishMinU + slopeFinishMinLength) {
                    gcode += QStringLiteral("; Blind slot start slope finish\n");
                    bool forward = true;
                    for (double offset : slopeOffsets) {
                        const double layerV = slopeMidV + offset;
                        const double u0 = forward ? topFinishMinU : bottomFinishMinU;
                        const double u1 = forward ? bottomFinishMinU : topFinishMinU;
                        const double z0 = forward ? zTop : zLayer;
                        const double z1 = forward ? zLayer : zTop;
                        double x0 = 0.0;
                        double y0 = 0.0;
                        double x1 = 0.0;
                        double y1 = 0.0;
                        slotToWorld(u0, layerV, x0, y0);
                        slotToWorld(u1, layerV, x1, y1);
                        gcode += QStringLiteral("G0 Z%1\n").arg(retractZ, 0, 'f', 6);
                        gcode += rapidXY(x0, y0);
                        gcode += QStringLiteral("G0 Z%1\n").arg(zTop + feedH, 0, 'f', 6);
                        gcode += QStringLiteral("G1 Z%1 F%2\n").arg(z0, 0, 'f', 6).arg(int(plungeRate));
                        gcode += feedXYZ(x1, y1, z1, feedRate);
                        totalLength += std::sqrt((u1 - u0) * (u1 - u0) + (z1 - z0) * (z1 - z0));
                        forward = !forward;
                    }
                }

                if (slopeEndLength > slopeFinishMinLength &&
                    topFinishMaxU > bottomFinishMaxU + slopeFinishMinLength) {
                    gcode += QStringLiteral("; Blind slot end slope finish\n");
                    bool forward = true;
                    for (double offset : slopeOffsets) {
                        const double layerV = slopeMidV + offset;
                        const double u0 = forward ? bottomFinishMaxU : topFinishMaxU;
                        const double u1 = forward ? topFinishMaxU : bottomFinishMaxU;
                        const double z0 = forward ? zLayer : zTop;
                        const double z1 = forward ? zTop : zLayer;
                        double x0 = 0.0;
                        double y0 = 0.0;
                        double x1 = 0.0;
                        double y1 = 0.0;
                        slotToWorld(u0, layerV, x0, y0);
                        slotToWorld(u1, layerV, x1, y1);
                        gcode += QStringLiteral("G0 Z%1\n").arg(retractZ, 0, 'f', 6);
                        gcode += rapidXY(x0, y0);
                        gcode += QStringLiteral("G0 Z%1\n").arg(zTop + feedH, 0, 'f', 6);
                        gcode += QStringLiteral("G1 Z%1 F%2\n").arg(z0, 0, 'f', 6).arg(int(plungeRate));
                        gcode += feedXYZ(x1, y1, z1, feedRate);
                        totalLength += std::sqrt((u1 - u0) * (u1 - u0) + (z1 - z0) * (z1 - z0));
                        forward = !forward;
                    }
                }
            }

            const double slopeMidU = (bottomFinishMinU + bottomFinishMaxU) * 0.5;
            const double slopeHalfU = (bottomFinishMaxU - bottomFinishMinU) * 0.5;
            if (slopeHalfU > 1.0e-6) {
                const std::vector<double> sideSlopeOffsets = finishScanOffsets(slopeHalfU,
                                                                               stepover,
                                                                               tool.diameter);
                if (slopeMinWidth > slopeFinishMinLength &&
                    bottomFinishMinV > topFinishMinV + slopeFinishMinLength) {
                    gcode += QStringLiteral("; Blind slot left side slope finish\n");
                    bool forward = true;
                    for (double offset : sideSlopeOffsets) {
                        const double layerU = slopeMidU + offset;
                        const double v0 = forward ? topFinishMinV : bottomFinishMinV;
                        const double v1 = forward ? bottomFinishMinV : topFinishMinV;
                        const double z0 = forward ? zTop : zLayer;
                        const double z1 = forward ? zLayer : zTop;
                        double x0 = 0.0;
                        double y0 = 0.0;
                        double x1 = 0.0;
                        double y1 = 0.0;
                        slotToWorld(layerU, v0, x0, y0);
                        slotToWorld(layerU, v1, x1, y1);
                        gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 6);
                        gcode += rapidXY(x0, y0);
                        gcode += QStringLiteral("G0 Z%1\n").arg(zTop + feedH, 0, 'f', 6);
                        gcode += QStringLiteral("G1 Z%1 F%2\n").arg(z0, 0, 'f', 6).arg(int(plungeRate));
                        gcode += feedXYZ(x1, y1, z1, feedRate);
                        totalLength += std::sqrt((v1 - v0) * (v1 - v0) + (z1 - z0) * (z1 - z0));
                        forward = !forward;
                    }
                }

                if (slopeMaxWidth > slopeFinishMinLength &&
                    topFinishMaxV > bottomFinishMaxV + slopeFinishMinLength) {
                    gcode += QStringLiteral("; Blind slot right side slope finish\n");
                    bool forward = true;
                    for (double offset : sideSlopeOffsets) {
                        const double layerU = slopeMidU + offset;
                        const double v0 = forward ? bottomFinishMaxV : topFinishMaxV;
                        const double v1 = forward ? topFinishMaxV : bottomFinishMaxV;
                        const double z0 = forward ? zLayer : zTop;
                        const double z1 = forward ? zTop : zLayer;
                        double x0 = 0.0;
                        double y0 = 0.0;
                        double x1 = 0.0;
                        double y1 = 0.0;
                        slotToWorld(layerU, v0, x0, y0);
                        slotToWorld(layerU, v1, x1, y1);
                        gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 6);
                        gcode += rapidXY(x0, y0);
                        gcode += QStringLiteral("G0 Z%1\n").arg(zTop + feedH, 0, 'f', 6);
                        gcode += QStringLiteral("G1 Z%1 F%2\n").arg(z0, 0, 'f', 6).arg(int(plungeRate));
                        gcode += feedXYZ(x1, y1, z1, feedRate);
                        totalLength += std::sqrt((v1 - v0) * (v1 - v0) + (z1 - z0) * (z1 - z0));
                        forward = !forward;
                    }
                }
            }
        }

        if (!finishRangeValid) {
            gcode += QStringLiteral("G0 Z%1\n").arg(retractZ, 0, 'f', 6);
            continue;
        }

        struct Corner { double u; double v; };
        const Corner corners[4] = {
            {finishMinU, finishMinV},
            {finishMaxU, finishMinV},
            {finishMaxU, finishMaxV},
            {finishMinU, finishMaxV}
        };
        const double leadU = finishMinU + tool.diameter * 0.5;
        double leadX = 0.0;
        double leadY = 0.0;
        slotToWorld(leadU, 0.0, leadX, leadY);

        gcode += QStringLiteral("; Blind slot wall finish\n");
        // Slope finishing may end at the top. Re-enter the fully swept bottom at its center.
        double entryX = 0.0;
        double entryY = 0.0;
        slotToWorld((bottomFinishMinU + bottomFinishMaxU) * 0.5,
                    (bottomFinishMinV + bottomFinishMaxV) * 0.5, entryX, entryY);
        gcode += QStringLiteral("G0 Z%1\n").arg(retractZ, 0, 'f', 6);
        gcode += rapidXY(entryX, entryY);
        gcode += QStringLiteral("G1 Z%1 F%2\n").arg(zLayer, 0, 'f', 6).arg(int(plungeRate));
        gcode += feedXY(leadX, leadY, feedRate);
        if (camOffset) {
            gcode += QStringLiteral("G40\n");
        }
        for (int cornerIndex = 0; cornerIndex < 4; ++cornerIndex) {
            const Corner &corner = corners[cornerIndex];
            double x = 0.0;
            double y = 0.0;
            slotToWorld(corner.u, corner.v, x, y);
            if (!camOffset && cornerIndex == 0) {
                const QString compCode = comp > 0.0 ? QStringLiteral("G41") : QStringLiteral("G42");
                gcode += QStringLiteral("G1 %1 D%2 X%3 Y%4 F%5\n")
                             .arg(compCode)
                             .arg(tool.id)
                             .arg(x, 0, 'f', 6)
                             .arg(y, 0, 'f', 6)
                             .arg(int(feedRate));
            } else {
                gcode += feedXY(x, y, feedRate);
            }
        }
        double firstX = 0.0;
        double firstY = 0.0;
        slotToWorld(corners[0].u, corners[0].v, firstX, firstY);
        gcode += feedXY(firstX, firstY, feedRate);
        if (camOffset) {
            gcode += feedXY(leadX, leadY, feedRate);
            gcode += QStringLiteral("G40\n");
        } else {
            gcode += QStringLiteral("G1 G40 X%1 Y%2 F%3\n")
                         .arg(leadX, 0, 'f', 6)
                         .arg(leadY, 0, 'f', 6)
                         .arg(int(feedRate));
        }
        gcode += QStringLiteral("G1 X%1 Y%2 F%3\n")
                     .arg(cx, 0, 'f', 6)
                     .arg(cy, 0, 'f', 6)
                     .arg(int(feedRate));
        gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 6);

        totalLength += 2.0 * ((finishMaxU - finishMinU) + (finishMaxV - finishMinV)) +
                       2.0 * std::max(0.0, leadU - finishMinU);
    }

    res.gcode = gcode;
    res.ok = true;
    res.estimatedTimeS = (totalLength / feedRate * 60.0) + (targetDepth / plungeRate * 60.0);
    return res;
}
