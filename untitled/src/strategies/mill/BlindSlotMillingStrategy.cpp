#include "BlindSlotMillingStrategy.h"

#include <QObject>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

static QString rapidXY(double x, double y)
{
    return QStringLiteral("G0 X%1 Y%2\n").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3);
}

static QString feedXY(double x, double y, double feed)
{
    return QStringLiteral("G1 X%1 Y%2 F%3\n")
        .arg(x, 0, 'f', 3)
        .arg(y, 0, 'f', 3)
        .arg(int(feed));
}

static QString feedXYZ(double x, double y, double z, double feed)
{
    return QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
        .arg(x, 0, 'f', 3)
        .arg(y, 0, 'f', 3)
        .arg(z, 0, 'f', 3)
        .arg(int(feed));
}

static void rotateAround(double cx, double cy, double px, double py,
                         double cosA, double sinA,
                         double &outX, double &outY)
{
    const double dx = px - cx;
    const double dy = py - cy;
    outX = cx + dx * cosA - dy * sinA;
    outY = cy + dx * sinA + dy * cosA;
}

static double slotLength(const ContourFeature &feature, const StrategyParams &params)
{
    return params.get(QStringLiteral("slotLength"),
                      feature.length > 0.0 ? feature.length : feature.radius * 2.0);
}

static double slotWidth(const ContourFeature &feature, const StrategyParams &params)
{
    return params.get(QStringLiteral("slotWidth"),
                      feature.width > 0.0 ? feature.width : feature.radius);
}

static double slotAngleDeg(const ContourFeature &feature, const StrategyParams &params)
{
    return params.get(QStringLiteral("angle"), feature.angle);
}

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

static void clampSlopePair(double &first, double &second, double maxTotal)
{
    first = std::max(0.0, first);
    second = std::max(0.0, second);
    maxTotal = std::max(0.0, maxTotal);

    const double sum = first + second;
    if (sum <= maxTotal || sum <= 1.0e-9) {
        return;
    }

    const double scale = maxTotal / sum;
    first *= scale;
    second *= scale;
}

} // namespace

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

ToolpathResult BlindSlotMillingStrategy::generate(const HoleFeature &,
                                                  const ToolEntry &,
                                                  const StrategyParams &) const
{
    return {QString(), false, QObject::tr("Blind slot milling requires a contour feature."), 0.0};
}

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
    const double effectiveStepDown = std::min(stepDown, std::max(0.02, tool.diameter));

    const double halfLen = fullLength * 0.5;
    const double halfWid = fullWidth * 0.5;
    const double angleRad = angleDeg * std::acos(-1.0) / 180.0;
    const double maxHelixRadius = std::max(0.0, halfWid - tool.diameter * 0.5);
    double helixRadius = params.get(QStringLiteral("helixRadius"), 0.0);
    if (helixRadius > 0.0) {
        helixRadius = std::min(helixRadius, maxHelixRadius);
    } else {
        helixRadius = 0.0;
    }
    double helixPitch = params.get(QStringLiteral("helixPitch"), 0.0);
    if (helixRadius > 1.0e-6 && helixPitch <= 0.0) {
        helixPitch = std::min(stepDown * 0.5, tool.diameter * 0.15);
    }

    const double cx = feature.center.x();
    const double cy = feature.center.y();
    const double zTop = feature.center.z();
    const double cosA = std::cos(angleRad);
    const double sinA = std::sin(angleRad);
    const bool camOffset = std::abs(comp) < 1.0e-9;
    const double roughCenterInset = tool.diameter * 0.5 + stockToLeave;
    const double finishCenterInset = camOffset ? tool.diameter * 0.5 : 0.0;
    double slopeStartLength = feature.slopeStartLength;
    double slopeEndLength = feature.slopeEndLength;
    double slopeMinWidth = feature.slopeMinWidth;
    double slopeMaxWidth = feature.slopeMaxWidth;
    const double minCenterSpan = std::max(0.05, tool.diameter * 0.25);
    clampSlopePair(slopeStartLength,
                   slopeEndLength,
                   fullLength - 2.0 * roughCenterInset - minCenterSpan);
    clampSlopePair(slopeMinWidth,
                   slopeMaxWidth,
                   fullWidth - 2.0 * roughCenterInset - minCenterSpan);
    double finishMinU = 0.0;
    double finishMaxU = 0.0;
    slotLayerRange(halfLen,
                   slopeStartLength,
                   slopeEndLength,
                   1.0,
                   finishCenterInset,
                   finishMinU,
                   finishMaxU);
    double finishMinV = 0.0;
    double finishMaxV = 0.0;
    slotLayerWidthRange(halfWid,
                        slopeMinWidth,
                        slopeMaxWidth,
                        1.0,
                        finishCenterInset,
                        finishMinV,
                        finishMaxV);
    double bottomFinishMinU = 0.0;
    double bottomFinishMaxU = 0.0;
    slotLayerRange(halfLen,
                   slopeStartLength,
                   slopeEndLength,
                   1.0,
                   tool.diameter * 0.5,
                   bottomFinishMinU,
                   bottomFinishMaxU);
    double bottomFinishMinV = 0.0;
    double bottomFinishMaxV = 0.0;
    slotLayerWidthRange(halfWid,
                        slopeMinWidth,
                        slopeMaxWidth,
                        1.0,
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
        rotateAround(cx, cy, cx + u, cy + v, cosA, sinA, x, y);
    };

    auto appendRampPlunge = [&](QString &out,
                                double targetU,
                                double targetV,
                                double minU,
                                double maxU,
                                double targetZ) {
        const double angle = std::max(1.0, std::min(rampAngle, 10.0));
        const double verticalDrop = std::max(0.0, zTop - targetZ);
        const double desiredLength = verticalDrop / std::tan(angle * std::acos(-1.0) / 180.0);
        const double backward = targetU - minU;
        const double forward = maxU - targetU;
        double startU = targetU;
        if (desiredLength > 1.0e-6 && std::max(backward, forward) > 0.05) {
            if (backward >= forward) {
                startU = targetU - std::min(desiredLength, backward);
            } else {
                startU = targetU + std::min(desiredLength, forward);
            }
        }

        double startX = 0.0;
        double startY = 0.0;
        double endX = 0.0;
        double endY = 0.0;
        slotToWorld(startU, targetV, startX, startY);
        slotToWorld(targetU, targetV, endX, endY);
        out += rapidXY(startX, startY);
        out += QStringLiteral("G0 Z%1\n").arg(zTop + feedH, 0, 'f', 3);
        out += QStringLiteral("G1 Z%1 F%2\n").arg(zTop, 0, 'f', 3).arg(int(plungeRate));
        if (std::abs(startU - targetU) > 0.05 && targetZ < zTop - 1.0e-6) {
            out += QStringLiteral("; Blind slot ramp plunge angle %1\n").arg(angle, 0, 'f', 3);
            out += feedXYZ(endX, endY, targetZ, plungeRate);
        } else {
            out += QStringLiteral("G1 Z%1 F%2\n").arg(targetZ, 0, 'f', 3).arg(int(plungeRate));
        }
    };

    QString gcode;
    gcode += QStringLiteral("T%1 M6\n").arg(tool.id);
    gcode += QStringLiteral("S%1 M3\n").arg(int(spindleSpeed));
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
    gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);

    double totalLength = 0.0;

    for (int layer = 1; layer <= layerCount; ++layer) {
        const double zLayer = zTop - std::min(layer * effectiveStepDown, targetDepth);
        const double layerDepth = zTop - zLayer;
        const double depthRatio = targetDepth > 1.0e-6
                                      ? std::max(0.0, std::min(layerDepth / targetDepth, 1.0))
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

        if (roughMaxU > roughMinU && roughMaxV > roughMinV) {
            if (helixRadius > 1e-6) {
                double startX = 0.0;
                double startY = 0.0;
                slotToWorld(helixRadius, 0.0, startX, startY);
                gcode += rapidXY(startX, startY);
                gcode += QStringLiteral("G0 Z%1\n").arg(zTop + feedH, 0, 'f', 3);
                const int helixLoops = static_cast<int>(std::ceil(((zTop + feedH) - zLayer) / helixPitch));
                for (int loop = 1; loop <= helixLoops; ++loop) {
                    const double zNext = std::max(zLayer, (zTop + feedH) - loop * helixPitch);
                    gcode += QStringLiteral("G2 X%1 Y%2 I%3 J0.000 Z%4 F%5\n")
                                 .arg(startX, 0, 'f', 3)
                                 .arg(startY, 0, 'f', 3)
                                 .arg(-helixRadius, 0, 'f', 3)
                                 .arg(zNext, 0, 'f', 3)
                                 .arg(int(plungeRate));
                }
                gcode += QStringLiteral("G1 X%1 Y%2 F%3\n")
                             .arg(cx, 0, 'f', 3)
                             .arg(cy, 0, 'f', 3)
                             .arg(int(feedRate));
            } else {
                appendRampPlunge(gcode,
                                  (roughMinU + roughMaxU) * 0.5,
                                  (roughMinV + roughMaxV) * 0.5,
                                  roughMinU,
                                  roughMaxU,
                                  zLayer);
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
                         .arg(zLayer, 0, 'f', 3);
        }

        if (layer < layerCount) {
            gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
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
                bool firstLine = true;
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
                    if (firstLine) {
                        gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
                        appendRampPlunge(gcode, startU, layerV, bottomFinishMinU, bottomFinishMaxU, zLayer);
                        firstLine = false;
                    } else {
                        gcode += feedXY(x0, y0, feedRate);
                    }
                    gcode += feedXY(x1, y1, feedRate);
                    totalLength += spanU;
                    forward = !forward;
                }
            } else {
                const std::vector<double> scanOffsets = finishScanOffsets(spanU * 0.5,
                                                                          stepover,
                                                                          tool.diameter);
                const double midU = (bottomFinishMinU + bottomFinishMaxU) * 0.5;
                bool firstLine = true;
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
                    if (firstLine) {
                        gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
                        appendRampPlunge(gcode, layerU, startV, bottomFinishMinU, bottomFinishMaxU, zLayer);
                        firstLine = false;
                    } else {
                        gcode += feedXY(x0, y0, feedRate);
                    }
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
                        gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
                        gcode += rapidXY(x0, y0);
                        gcode += QStringLiteral("G0 Z%1\n").arg(zTop + feedH, 0, 'f', 3);
                        gcode += QStringLiteral("G1 Z%1 F%2\n").arg(z0, 0, 'f', 3).arg(int(plungeRate));
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
                        gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
                        gcode += rapidXY(x0, y0);
                        gcode += QStringLiteral("G0 Z%1\n").arg(zTop + feedH, 0, 'f', 3);
                        gcode += QStringLiteral("G1 Z%1 F%2\n").arg(z0, 0, 'f', 3).arg(int(plungeRate));
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
                        gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
                        gcode += rapidXY(x0, y0);
                        gcode += QStringLiteral("G0 Z%1\n").arg(zTop + feedH, 0, 'f', 3);
                        gcode += QStringLiteral("G1 Z%1 F%2\n").arg(z0, 0, 'f', 3).arg(int(plungeRate));
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
                        gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
                        gcode += rapidXY(x0, y0);
                        gcode += QStringLiteral("G0 Z%1\n").arg(zTop + feedH, 0, 'f', 3);
                        gcode += QStringLiteral("G1 Z%1 F%2\n").arg(z0, 0, 'f', 3).arg(int(plungeRate));
                        gcode += feedXYZ(x1, y1, z1, feedRate);
                        totalLength += std::sqrt((v1 - v0) * (v1 - v0) + (z1 - z0) * (z1 - z0));
                        forward = !forward;
                    }
                }
            }
        }

        if (!finishRangeValid) {
            gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
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

        gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
        appendRampPlunge(gcode, leadU, 0.0, finishMinU, finishMaxU, zLayer);
        if (camOffset) {
            gcode += QStringLiteral("G40\n");
        } else {
            const QString compCode = comp > 0.0 ? QStringLiteral("G41") : QStringLiteral("G42");
            gcode += QStringLiteral("%1 D%2\n").arg(compCode).arg(tool.id);
        }
        for (const Corner &corner : corners) {
            double x = 0.0;
            double y = 0.0;
            slotToWorld(corner.u, corner.v, x, y);
            gcode += feedXY(x, y, feedRate);
        }
        double firstX = 0.0;
        double firstY = 0.0;
        slotToWorld(corners[0].u, corners[0].v, firstX, firstY);
        gcode += feedXY(firstX, firstY, feedRate);
        gcode += feedXY(leadX, leadY, feedRate);
        gcode += QStringLiteral("G40\n");
        gcode += QStringLiteral("G1 X%1 Y%2 F%3\n")
                     .arg(cx, 0, 'f', 3)
                     .arg(cy, 0, 'f', 3)
                     .arg(int(feedRate));
        gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);

        totalLength += 2.0 * ((finishMaxU - finishMinU) + (finishMaxV - finishMinV)) +
                       2.0 * std::max(0.0, leadU - finishMinU);
    }

    res.gcode = gcode;
    res.ok = true;
    res.estimatedTimeS = (totalLength / feedRate * 60.0) + (targetDepth / plungeRate * 60.0);
    return res;
}
