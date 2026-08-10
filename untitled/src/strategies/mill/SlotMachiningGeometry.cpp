#include "SlotMachiningGeometry.h"

#include <QObject>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

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

} // namespace

bool refineSlotContourFromMeshData(const MachiningFeature &source,
                                   const MeshData &mesh,
                                   ContourFeature &contour)
{
    if (source.kind != FeatureKind::Slot || source.faceIndices.isEmpty() || mesh.isEmpty()) {
        return false;
    }

    QSet<int> faceSet;
    for (int faceIndex : source.faceIndices) {
        if (faceIndex > 0) {
            faceSet.insert(faceIndex);
        }
    }
    if (faceSet.isEmpty()) {
        return false;
    }

    const double sourceAngleRad = source.angle * std::acos(-1.0) / 180.0;
    const QVector3D sourceLengthDir(float(std::cos(sourceAngleRad)), float(std::sin(sourceAngleRad)), 0.0f);
    const QVector3D sourceWidthDir(float(-std::sin(sourceAngleRad)), float(std::cos(sourceAngleRad)), 0.0f);
    double minU =  std::numeric_limits<double>::max();
    double maxU = -std::numeric_limits<double>::max();
    double minV =  std::numeric_limits<double>::max();
    double maxV = -std::numeric_limits<double>::max();
    double minZ =  std::numeric_limits<double>::max();
    double maxZ = -std::numeric_limits<double>::max();
    int pointCount = 0;
    struct SlotSample {
        double u;
        double v;
        double z;
    };
    QVector<SlotSample> samples;
    QVector<QVector3D> rawPoints;

    auto includePoint = [&](const QVector3D &point) {
        rawPoints.append(point);
    };

    for (const Triangle &tri : mesh.triangles) {
        if (!faceSet.contains(tri.faceIndex)) {
            continue;
        }
        includePoint(tri.v0);
        includePoint(tri.v1);
        includePoint(tri.v2);
    }

    if (rawPoints.size() < 3) {
        return false;
    }

    double meanX = 0.0;
    double meanY = 0.0;
    for (const QVector3D &point : rawPoints) {
        meanX += double(point.x());
        meanY += double(point.y());
    }
    meanX /= rawPoints.size();
    meanY /= rawPoints.size();

    double covXX = 0.0;
    double covXY = 0.0;
    double covYY = 0.0;
    for (const QVector3D &point : rawPoints) {
        const double dx = double(point.x()) - meanX;
        const double dy = double(point.y()) - meanY;
        covXX += dx * dx;
        covXY += dx * dy;
        covYY += dy * dy;
    }

    double angleRad = sourceAngleRad;
    if (std::abs(covXY) > 1.0e-9 || std::abs(covXX - covYY) > 1.0e-9) {
        angleRad = 0.5 * std::atan2(2.0 * covXY, covXX - covYY);
    }
    QVector3D lengthDir(float(std::cos(angleRad)), float(std::sin(angleRad)), 0.0f);
    QVector3D widthDir(float(-std::sin(angleRad)), float(std::cos(angleRad)), 0.0f);
    if (QVector3D::dotProduct(lengthDir, sourceLengthDir) < 0.0f) {
        lengthDir = -lengthDir;
        widthDir = -widthDir;
        angleRad += std::acos(-1.0);
    }

    for (const QVector3D &point : rawPoints) {
        const QVector3D rel = point - source.center;
        const double u = QVector3D::dotProduct(rel, lengthDir);
        const double v = QVector3D::dotProduct(rel, widthDir);
        minU = std::min(minU, u);
        maxU = std::max(maxU, u);
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
        minZ = std::min(minZ, double(point.z()));
        maxZ = std::max(maxZ, double(point.z()));
        samples.append({u, v, double(point.z())});
        ++pointCount;
    }

    const double spanU = maxU - minU;
    const double spanV = maxV - minV;
    if (spanU <= 1.0e-6 || spanV <= 1.0e-6) {
        return false;
    }

    double cutMinU = minU;
    double cutMaxU = maxU;
    double cutMinV = minV;
    double cutMaxV = maxV;
    const double meshDepth = maxZ - minZ;
    const double bottomTol = std::max(0.02, meshDepth * 0.08);
    double bottomMinU =  std::numeric_limits<double>::max();
    double bottomMaxU = -std::numeric_limits<double>::max();
    double bottomMinV =  std::numeric_limits<double>::max();
    double bottomMaxV = -std::numeric_limits<double>::max();
    int bottomPointCount = 0;
    for (const SlotSample &sample : samples) {
        if (sample.z > minZ + bottomTol) {
            continue;
        }
        bottomMinU = std::min(bottomMinU, sample.u);
        bottomMaxU = std::max(bottomMaxU, sample.u);
        bottomMinV = std::min(bottomMinV, sample.v);
        bottomMaxV = std::max(bottomMaxV, sample.v);
        ++bottomPointCount;
    }
    const double bottomSpanU = bottomMaxU - bottomMinU;
    const double bottomSpanV = bottomMaxV - bottomMinV;
    const bool lengthAlongU = spanU >= spanV;
    const double minBottomSpanU = std::max(0.02, spanU * 0.05);
    const double minBottomSpanV = std::max(0.02, spanV * 0.05);
    const bool hasBottomLengthProfile = bottomPointCount >= 3 &&
                                        (lengthAlongU
                                             ? (bottomSpanU > minBottomSpanU)
                                             : (bottomSpanV > minBottomSpanV));
    const bool hasBottomWidthProfile = bottomPointCount >= 3 &&
                                       (lengthAlongU
                                            ? (bottomSpanV > minBottomSpanV)
                                            : (bottomSpanU > minBottomSpanU));
    if (hasBottomLengthProfile || hasBottomWidthProfile) {
        if (hasBottomLengthProfile) {
            cutMinU = bottomMinU;
            cutMaxU = bottomMaxU;
        }
        if (hasBottomWidthProfile) {
            cutMinV = bottomMinV;
            cutMaxV = bottomMaxV;
        }
    }

    const double cutSpanU = cutMaxU - cutMinU;
    const double cutSpanV = cutMaxV - cutMinV;
    if (cutSpanU <= 1.0e-6 || cutSpanV <= 1.0e-6) {
        return false;
    }

    const double midU = (minU + maxU) * 0.5;
    const double midV = (minV + maxV) * 0.5;
    QVector3D center = source.center + lengthDir * float(midU) + widthDir * float(midV);
    center.setZ(float(maxZ));
    contour.center = center;

    if (source.depth > 0.0) {
        contour.depth = source.depth;
    } else if (meshDepth > 1.0e-6) {
        contour.depth = meshDepth;
    }

    if (lengthAlongU) {
        contour.length = spanU;
        contour.width = spanV;
        contour.angle = angleRad * 180.0 / std::acos(-1.0);
        contour.slopeStartLength = hasBottomLengthProfile ? std::max(0.0, bottomMinU - minU) : 0.0;
        contour.slopeEndLength = hasBottomLengthProfile ? std::max(0.0, maxU - bottomMaxU) : 0.0;
        contour.slopeMinWidth = hasBottomWidthProfile ? std::max(0.0, bottomMinV - minV) : 0.0;
        contour.slopeMaxWidth = hasBottomWidthProfile ? std::max(0.0, maxV - bottomMaxV) : 0.0;
    } else {
        contour.length = spanV;
        contour.width = spanU;
        contour.angle = angleRad * 180.0 / std::acos(-1.0) + 90.0;
        contour.slopeStartLength = hasBottomLengthProfile ? std::max(0.0, bottomMinV - minV) : 0.0;
        contour.slopeEndLength = hasBottomLengthProfile ? std::max(0.0, maxV - bottomMaxV) : 0.0;
        contour.slopeMinWidth = hasBottomWidthProfile ? std::max(0.0, bottomMinU - minU) : 0.0;
        contour.slopeMaxWidth = hasBottomWidthProfile ? std::max(0.0, maxU - bottomMaxU) : 0.0;
    }
    return true;
}

SlotMachiningGeometry buildSlotMachiningGeometry(const ContourFeature &feature,
                                                 const ToolEntry &tool,
                                                 const StrategyParams &params)
{
    SlotMachiningGeometry geometry;

    geometry.fullLength = slotLength(feature, params);
    geometry.fullWidth = slotWidth(feature, params);
    geometry.depth = feature.depth;

    if (tool.diameter <= 0.0) {
        geometry.errorMsg = QObject::tr("The selected tool diameter is invalid.");
        return geometry;
    }
    if (geometry.depth <= 0.0) {
        geometry.errorMsg = QObject::tr("Slot depth must be greater than 0.");
        return geometry;
    }
    if (geometry.fullLength <= 0.0 || geometry.fullWidth <= 0.0) {
        geometry.errorMsg = QObject::tr("Slot length and width must both be greater than 0.");
        return geometry;
    }
    if (tool.diameter >= geometry.fullWidth) {
        geometry.errorMsg = QObject::tr("The tool diameter must be smaller than the slot width.");
        return geometry;
    }

    const double stock = params.get(QStringLiteral("stockToLeave"), 0.1);
    geometry.radialClearance = std::max(stock, tool.diameter * 0.25);
    geometry.openSign = feature.openSide > 0.0 ? 1.0 : -1.0;
    const double closedEndInset = geometry.radialClearance + tool.diameter * 0.5;
    const double openEndInset = geometry.radialClearance;
    geometry.lengthCenterInset = closedEndInset;
    geometry.sideCenterInset = geometry.radialClearance + tool.diameter * 0.5;

    geometry.halfLength = geometry.fullLength * 0.5;
    geometry.halfWidth = geometry.fullWidth * 0.5;
    if (geometry.openSign < 0.0) {
        geometry.roughMinU = -geometry.halfLength + openEndInset;
        geometry.roughMaxU = geometry.halfLength - closedEndInset;
    } else {
        geometry.roughMinU = -geometry.halfLength + closedEndInset;
        geometry.roughMaxU = geometry.halfLength - openEndInset;
    }
    geometry.roughHalfWidth = geometry.halfWidth - geometry.sideCenterInset;

    if (geometry.roughMaxU <= geometry.roughMinU ||
        geometry.roughHalfWidth <= 0.0) {
        geometry.errorMsg = QObject::tr("The tool diameter or stock to leave is too large for slot machining.");
        return geometry;
    }

    double rawStartSlopeLen = std::max(0.0, feature.slopeStartLength);
    double rawEndSlopeLen = std::max(0.0, feature.slopeEndLength);
    const double rampCenterComp = tool.diameter * 0.5;
    if (rawStartSlopeLen > 1.0e-6) {
        rawStartSlopeLen += rampCenterComp;
    }
    if (rawEndSlopeLen > 1.0e-6) {
        rawEndSlopeLen += rampCenterComp;
    }
    const double availableLength = std::max(0.0, geometry.roughMaxU - geometry.roughMinU);
    geometry.slopeStartLength = std::min(rawStartSlopeLen, availableLength);
    geometry.slopeEndLength = std::min(rawEndSlopeLen,
                                       std::max(0.0, availableLength - geometry.slopeStartLength));
    geometry.bottomStartU = std::min(geometry.roughMaxU,
                                     geometry.roughMinU + geometry.slopeStartLength);
    geometry.bottomEndU = std::max(geometry.bottomStartU,
                                   geometry.roughMaxU - geometry.slopeEndLength);

    geometry.cx = feature.center.x();
    geometry.cy = feature.center.y();
    geometry.zTop = feature.center.z();
    geometry.angleRad = slotAngleDeg(feature, params) * std::acos(-1.0) / 180.0;
    geometry.cosA = std::cos(geometry.angleRad);
    geometry.sinA = std::sin(geometry.angleRad);
    geometry.valid = true;
    return geometry;
}

void slotLocalToWorld(const SlotMachiningGeometry &geometry,
                      double u,
                      double v,
                      double &x,
                      double &y)
{
    x = geometry.cx + u * geometry.cosA - v * geometry.sinA;
    y = geometry.cy + u * geometry.sinA + v * geometry.cosA;
}

QString validateSlotMachiningGeometry(const SlotMachiningGeometry &geometry,
                                      const ToolEntry &tool,
                                      const StrategyParams &params)
{
    if (!geometry.valid) {
        return geometry.errorMsg.isEmpty()
                   ? QObject::tr("The slot machining geometry is invalid.")
                   : geometry.errorMsg;
    }

    const double safeHeight = params.get(QStringLiteral("safeHeight"), 50.0);
    const double feedHeight = params.get(QStringLiteral("feedHeight"), 3.0);
    const double stepDown = params.get(QStringLiteral("stepDown"), 2.0);
    const double stepover = params.get(QStringLiteral("stepover"), 5.0);
    const double feedRate = params.get(QStringLiteral("feedRate"), 800.0);
    const double plungeRate = params.get(QStringLiteral("plungeRate"), 200.0);

    if (tool.diameter <= 0.0) {
        return QObject::tr("The selected tool diameter is invalid.");
    }
    if (geometry.depth <= 0.0) {
        return QObject::tr("Slot depth must be greater than 0.");
    }
    if (geometry.fullLength <= 0.0 || geometry.fullWidth <= 0.0) {
        return QObject::tr("Slot length and width must both be greater than 0.");
    }
    if (tool.diameter >= geometry.fullWidth) {
        return QObject::tr("The tool diameter must be smaller than the slot width.");
    }
    if (stepDown <= 0.0 || stepover <= 0.0) {
        return QObject::tr("Step down and stepover must both be greater than 0.");
    }
    if (feedRate <= 0.0 || plungeRate <= 0.0) {
        return QObject::tr("Feed rate and plunge rate must both be greater than 0.");
    }
    if (feedHeight <= 0.0) {
        return QObject::tr("Feed height must be greater than 0.");
    }
    if (safeHeight <= geometry.zTop + feedHeight) {
        return QObject::tr("Safe height must be higher than the feed start height.");
    }
    if (geometry.roughMaxU <= geometry.roughMinU || geometry.roughHalfWidth <= 0.0) {
        return QObject::tr("The tool diameter or stock to leave is too large for slot machining.");
    }

    const double roughLength = geometry.roughMaxU - geometry.roughMinU;
    const double flatBottomLength = geometry.bottomEndU - geometry.bottomStartU;
    const double minFlatBottomLength = std::max(0.10, tool.diameter * 0.10);
    if (roughLength <= 1.0e-6) {
        return QObject::tr("The slot roughing range is invalid.");
    }
    if (flatBottomLength < minFlatBottomLength) {
        return QObject::tr("The recognized slot bottom is too short for stable open-slot machining. Please re-check the machining face or slot feature.");
    }

    return QString();
}
