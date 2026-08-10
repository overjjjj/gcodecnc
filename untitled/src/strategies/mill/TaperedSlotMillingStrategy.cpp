#include "TaperedSlotMillingStrategy.h"

#include <QObject>
#include <algorithm>
#include <cmath>

namespace {

static QString rapidXY(double x, double y)
{
    return QStringLiteral("G0 X%1 Y%2\n").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3);
}

static QString cutXY(double x, double y, double feed)
{
    return QStringLiteral("G1 X%1 Y%2 F%3\n")
        .arg(x, 0, 'f', 3)
        .arg(y, 0, 'f', 3)
        .arg(int(feed));
}

static QString cutXYZ(double x, double y, double z, double feed)
{
    return QStringLiteral("G1 X%1 Y%2 Z%3 F%4\n")
        .arg(x, 0, 'f', 3)
        .arg(y, 0, 'f', 3)
        .arg(z, 0, 'f', 3)
        .arg(int(feed));
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

static bool useLengthSlopeProfile(const ContourFeature &feature)
{
    return feature.slopeStartLength > 1.0e-6 || feature.slopeEndLength > 1.0e-6;
}

static QVector<double> laneOffsets(double halfWidth, double stepover, double toolDiameter)
{
    QVector<double> offsets;
    if (halfWidth <= 1.0e-6) {
        offsets.append(0.0);
        return offsets;
    }

    const double step = std::max(0.01, std::min(stepover, toolDiameter * 0.45));
    offsets.append(-halfWidth);
    offsets.append(0.0);
    offsets.append(halfWidth);
    for (double v = -halfWidth + step; v < halfWidth - 1.0e-6; v += step) {
        offsets.append(v);
    }
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end(), [](double a, double b) {
        return std::abs(a - b) <= 1.0e-6;
    }), offsets.end());
    return offsets;
}

} // namespace

StrategyParams TaperedSlotMillingStrategy::defaultParams() const
{
    StrategyParams p;
    p.set(QStringLiteral("safeHeight"), 50.0);
    p.set(QStringLiteral("feedHeight"), 3.0);
    p.set(QStringLiteral("depth"), 10.0);
    p.set(QStringLiteral("stepDown"), 1.0);
    p.set(QStringLiteral("stepover"), 2.0);
    p.set(QStringLiteral("spindleSpeed"), 2000.0);
    p.set(QStringLiteral("feedRate"), 800.0);
    p.set(QStringLiteral("plungeRate"), 200.0);
    p.set(QStringLiteral("stockToLeave"), 0.1);
    p.set(QStringLiteral("finishStock"), 0.05);
    p.set(QStringLiteral("slotLength"), 50.0);
    p.set(QStringLiteral("slotWidth"), 10.0);
    p.set(QStringLiteral("angle"), 0.0);
    p.set(QStringLiteral("slopeDirection"), 1.0);
    return p;
}

ToolpathResult TaperedSlotMillingStrategy::generate(const HoleFeature &,
                                                    const ToolEntry &,
                                                    const StrategyParams &) const
{
    return {QString(), false, QObject::tr("Tapered slot milling requires a contour feature."), 0.0};
}

ToolpathResult TaperedSlotMillingStrategy::generate(const ContourFeature &feature,
                                                    const ToolEntry &tool,
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

    const double baseLength = slotLength(feature, params);
    const double baseWidth = slotWidth(feature, params);
    const double fullLength = baseLength;
    const double fullWidth = baseWidth;
    const double targetDepth = std::min(params.get(QStringLiteral("depth"), feature.depth), feature.depth);
    const double safe = params.get(QStringLiteral("safeHeight"), 50.0);
    const double feedH = params.get(QStringLiteral("feedHeight"), 3.0);
    const double stepDown = params.get(QStringLiteral("stepDown"), 1.0);
    const double stepover = params.get(QStringLiteral("stepover"), 2.0);
    const double spindleSpeed = params.get(QStringLiteral("spindleSpeed"), 2000.0);
    const double feedRate = params.get(QStringLiteral("feedRate"), 800.0);
    const double plungeRate = params.get(QStringLiteral("plungeRate"), 200.0);
    const double stockToLeave = std::max(0.0, params.get(QStringLiteral("stockToLeave"), 0.1));
    const double finishStock = std::max(0.0, params.get(QStringLiteral("finishStock"), 0.05));
    const double slopeDirection = params.get(QStringLiteral("slopeDirection"), 1.0);
    const double angleDeg = slotAngleDeg(feature, params);
    const bool useLengthSlope = useLengthSlopeProfile(feature);
    const bool useWidthSlope = feature.slopeMinWidth > 1.0e-6 || feature.slopeMaxWidth > 1.0e-6;

    if (fullLength <= 0.0 || fullWidth <= 0.0 || targetDepth <= 0.0) {
        res.errorMsg = QObject::tr("Slot length, width and depth must all be greater than 0.");
        return res;
    }
    if (tool.diameter >= fullWidth) {
        res.errorMsg = QObject::tr("The tool diameter must be smaller than the slot width.");
        return res;
    }
    if (stepDown <= 0.0 || stepover <= 0.0 || feedH <= 0.0 ||
        feedRate <= 0.0 || plungeRate <= 0.0) {
        res.errorMsg = QObject::tr("Machining parameters must all be greater than 0.");
        return res;
    }
    if (finishStock >= targetDepth) {
        res.errorMsg = QObject::tr("Finish stock must be smaller than the slot depth.");
        return res;
    }
    if (safe <= feature.center.z() + feedH) {
        res.errorMsg = QObject::tr("Safe height must be above the feed start height.");
        return res;
    }

    const double halfLen = fullLength * 0.5;
    const double halfWid = fullWidth * 0.5;
    const double roughInset = tool.diameter * 0.5 + stockToLeave;
    const double finishInset = tool.diameter * 0.5 + finishStock;
    const double roughMinU = -halfLen + roughInset;
    const double roughMaxU = halfLen - roughInset;
    const double roughMinV = -halfWid + roughInset;
    const double roughMaxV = halfWid - roughInset;
    const double finishMinU = -halfLen + finishInset;
    const double finishMaxU = halfLen - finishInset;
    const double finishMinV = -halfWid + finishInset;
    const double finishMaxV = halfWid - finishInset;
    if (roughMaxU <= roughMinU || roughMaxV <= roughMinV ||
        finishMaxU <= finishMinU || finishMaxV <= finishMinV) {
        res.errorMsg = QObject::tr("The slot is too small for the selected tool and stock settings.");
        return res;
    }

    const int layerCount = std::max(1, int(std::ceil(targetDepth / stepDown)));
    const double angleRad = angleDeg * std::acos(-1.0) / 180.0;
    const double cosA = std::cos(angleRad);
    const double sinA = std::sin(angleRad);
    const double cx = feature.center.x();
    const double cy = feature.center.y();
    const double zTop = feature.center.z();
    const double retractZ = zTop + feedH;
    const bool forwardSlope = slopeDirection >= 0.0;

    auto slotToWorld = [&](double u, double v, double &x, double &y) {
        x = cx + u * cosA - v * sinA;
        y = cy + u * sinA + v * cosA;
    };
    auto wedgeZAtU = [&](double u) {
        const double startSlope = std::max(0.0, feature.slopeStartLength);
        const double endSlope = std::max(0.0, feature.slopeEndLength);
        double ratio = 0.0;
        if (useLengthSlope && (startSlope > 1.0e-6 || endSlope > 1.0e-6)) {
            const double localMin = -halfLen;
            const double localMax = halfLen;
            if (startSlope > 1.0e-6 && u <= localMin + startSlope) {
                ratio = std::max(ratio, (localMin + startSlope - u) / std::max(1.0e-6, startSlope));
            }
            if (endSlope > 1.0e-6 && u >= localMax - endSlope) {
                ratio = std::max(ratio, (u - (localMax - endSlope)) / std::max(1.0e-6, endSlope));
            }
        } else {
            const double t = std::max(0.0, std::min((u - finishMinU) / std::max(1.0e-6, finishMaxU - finishMinU), 1.0));
            ratio = forwardSlope ? t : (1.0 - t);
        }
        ratio = std::max(0.0, std::min(ratio, 1.0));
        return zTop - targetDepth * ratio;
    };
    auto normalizedDepthRatioAtU = [&](double u) {
        const double z = wedgeZAtU(u);
        const double ratio = (zTop - z) / std::max(1.0e-6, targetDepth);
        return std::max(0.0, std::min(ratio, 1.0));
    };
    auto roughReachForLayer = [&](double zLayer, double v, double &uMin, double &uMax) {
        Q_UNUSED(v);
        if (zLayer >= zTop - 1.0e-6) {
            uMin = roughMinU;
            uMax = roughMinU;
            return;
        }
        const double layerRatio = std::max(0.0, std::min((zTop - zLayer) / std::max(1.0e-6, targetDepth), 1.0));
        uMin = roughMinU;
        uMax = roughMaxU;
        if (!useLengthSlope) {
            if (useWidthSlope) {
                return;
            }
            const double span = roughMaxU - roughMinU;
            if (forwardSlope) {
                uMin = roughMinU + span * layerRatio;
            } else {
                uMax = roughMaxU - span * layerRatio;
            }
            return;
        }

        const int samples = 160;
        bool found = false;
        double minReach = roughMaxU;
        double maxReach = roughMinU;
        for (int i = 0; i <= samples; ++i) {
            const double t = double(i) / double(samples);
            const double u = roughMinU + (roughMaxU - roughMinU) * t;
            if (normalizedDepthRatioAtU(u) >= layerRatio - 1.0e-6) {
                minReach = std::min(minReach, u);
                maxReach = std::max(maxReach, u);
                found = true;
            }
        }
        if (found) {
            uMin = minReach;
            uMax = maxReach;
        }
    };
    auto widthRangeForLayer = [&](double zLayer, double inset, double &vMin, double &vMax) {
        const double layerRatio = std::max(0.0, std::min((zTop - zLayer) / std::max(1.0e-6, targetDepth), 1.0));
        const double geomMinV = -halfWid + std::max(0.0, feature.slopeMinWidth) * layerRatio;
        const double geomMaxV =  halfWid - std::max(0.0, feature.slopeMaxWidth) * layerRatio;
        vMin = geomMinV + inset;
        vMax = geomMaxV - inset;
    };
    auto roughWidthForLayer = [&](double zLayer, double &vMin, double &vMax) {
        widthRangeForLayer(zLayer, roughInset, vMin, vMax);
    };
    QString gcode;
    gcode += QStringLiteral("T%1 M6\n").arg(tool.id);
    gcode += QStringLiteral("S%1 M3\n").arg(int(spindleSpeed));
    gcode += QStringLiteral("M8\n");
    gcode += QStringLiteral("; Tapered slot geometry: L=%1 W=%2 D=%3 A=%4 slopeDir=%5\n")
                 .arg(fullLength, 0, 'f', 3)
                 .arg(fullWidth, 0, 'f', 3)
                 .arg(targetDepth, 0, 'f', 3)
                 .arg(angleDeg, 0, 'f', 3)
                 .arg(forwardSlope ? QStringLiteral("start_to_end") : QStringLiteral("end_to_start"));
    gcode += QStringLiteral("; Tapered slot slopes: startU=%1 endU=%2 minV=%3 maxV=%4 mode=%5\n")
                 .arg(feature.slopeStartLength, 0, 'f', 3)
                 .arg(feature.slopeEndLength, 0, 'f', 3)
                 .arg(feature.slopeMinWidth, 0, 'f', 3)
                 .arg(feature.slopeMaxWidth, 0, 'f', 3)
                 .arg(useWidthSlope ? QStringLiteral("width-profile")
                                    : (useLengthSlope ? QStringLiteral("length-profile")
                                                      : QStringLiteral("linear-direction")));
    gcode += QStringLiteral("; Tapered slot axis-map: swapAxes=%1 baseL=%2 baseW=%3\n")
                 .arg(0)
                 .arg(baseLength, 0, 'f', 3)
                 .arg(baseWidth, 0, 'f', 3);
    gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);

    double totalLength = 0.0;
    for (int layer = 1; layer <= layerCount; ++layer) {
        const double zLayer = zTop - std::min(layer * stepDown, std::max(0.0, targetDepth - finishStock));
        double layerMinV = roughMinV;
        double layerMaxV = roughMaxV;
        if (useWidthSlope) {
            roughWidthForLayer(zLayer, layerMinV, layerMaxV);
        }
        if (layerMaxV <= layerMinV + 1.0e-6) {
            continue;
        }
        const double midV = (layerMinV + layerMaxV) * 0.5;
        const QVector<double> offsets = laneOffsets((layerMaxV - layerMinV) * 0.5, stepover, tool.diameter);
        double layerMinU = roughMinU;
        double layerMaxU = roughMaxU;
        roughReachForLayer(zLayer, midV, layerMinU, layerMaxU);
        if (layerMaxU <= layerMinU + 1.0e-6) {
            continue;
        }
        bool forward = true;
        double startX = 0.0;
        double startY = 0.0;
        slotToWorld(layerMinU, midV, startX, startY);
        gcode += QStringLiteral("; Tapered slot rough layer %1/%2 Z=%3\n")
                     .arg(layer)
                     .arg(layerCount)
                     .arg(zLayer, 0, 'f', 3);
        gcode += QStringLiteral("G0 Z%1\n").arg(retractZ, 0, 'f', 3);
        gcode += rapidXY(startX, startY);
        gcode += QStringLiteral("G1 Z%1 F%2\n").arg(zLayer, 0, 'f', 3).arg(int(plungeRate));
        for (double offset : offsets) {
            const double v = midV + offset;
            if (v < layerMinV || v > layerMaxV) {
                continue;
            }
            double laneMinU = roughMinU;
            double laneMaxU = roughMaxU;
            roughReachForLayer(zLayer, v, laneMinU, laneMaxU);
            if (laneMaxU <= laneMinU + 1.0e-6) {
                continue;
            }
            const double u0 = forward ? laneMinU : laneMaxU;
            const double u1 = forward ? laneMaxU : laneMinU;
            double x0 = 0.0;
            double y0 = 0.0;
            double x1 = 0.0;
            double y1 = 0.0;
            slotToWorld(u0, v, x0, y0);
            slotToWorld(u1, v, x1, y1);
            gcode += cutXY(x0, y0, feedRate);
            gcode += cutXY(x1, y1, feedRate);
            totalLength += std::abs(u1 - u0);
            forward = !forward;
        }
    }

    gcode += QStringLiteral("; Tapered slot floor finish\n");
    bool forward = true;
    double finishLayerMinV = finishMinV;
    double finishLayerMaxV = finishMaxV;
    if (useWidthSlope) {
        widthRangeForLayer(zTop - targetDepth, finishInset, finishLayerMinV, finishLayerMaxV);
    }
    if (finishLayerMaxV <= finishLayerMinV + 1.0e-6) {
        res.errorMsg = QObject::tr("The tapered slot bottom is too small for the selected tool and finish allowance.");
        return res;
    }
    const double finishMidV = (finishLayerMinV + finishLayerMaxV) * 0.5;
    const QVector<double> finishOffsets = laneOffsets((finishLayerMaxV - finishLayerMinV) * 0.5,
                                                      stepover * 0.5,
                                                      tool.diameter);
    for (double offset : finishOffsets) {
        const double v = finishMidV + offset;
        if (v < finishLayerMinV || v > finishLayerMaxV) {
            continue;
        }
        const double u0 = forward ? finishMinU : finishMaxU;
        const double u1 = forward ? finishMaxU : finishMinU;
        const double z0 = useWidthSlope ? zTop - targetDepth : wedgeZAtU(u0);
        const double z1 = useWidthSlope ? zTop - targetDepth : wedgeZAtU(u1);
        double x0 = 0.0;
        double y0 = 0.0;
        double x1 = 0.0;
        double y1 = 0.0;
        slotToWorld(u0, v, x0, y0);
        slotToWorld(u1, v, x1, y1);
        gcode += QStringLiteral("G0 Z%1\n").arg(retractZ, 0, 'f', 3);
        gcode += rapidXY(x0, y0);
        gcode += QStringLiteral("G1 Z%1 F%2\n").arg(z0, 0, 'f', 3).arg(int(plungeRate));
        gcode += cutXYZ(x1, y1, z1, feedRate);
        totalLength += std::sqrt((u1 - u0) * (u1 - u0) + (z1 - z0) * (z1 - z0));
        forward = !forward;
    }

    gcode += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
    res.gcode = gcode;
    res.ok = true;
    res.estimatedTimeS = (totalLength / feedRate * 60.0) + (targetDepth / plungeRate * 60.0);
    return res;
}
