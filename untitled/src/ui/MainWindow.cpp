#include "MainWindow.h"

#include "BottomBar.h"
#include "CncSendDialog.h"
#include "ContourFeatureGrouping.h"
#include "ContourMachiningChoiceDialog.h"
#include "FeatureListPanel.h"
#include "GCodeEditor.h"
#include "HoleFeatureGrouping.h"
#include "MachineProfileDialog.h"
#include "OperationListPanel.h"
#include "SetupOriginDialog.h"
#include "StockDefinitionDialog.h"
#include "StrategyPanel.h"
#include "ToolOperationCompatibility.h"
#include "ToolLibraryPanel.h"
#include "ViewportWidget.h"
#include "../core/AppController.h"
#include "../core/SetupOrientation.h"
#include "../core/Settings.h"
#include "../gcode/GCodeSafetyValidator.h"
#include "../gcode/ProgramPackageExporter.h"
#include "../gcode/ProgramSnapshotFingerprint.h"
#include "../gcode/ProgramSnapshotStatus.h"
#include "../postprocessor/PostProcessorBase.h"
#include "../postprocessor/PostProcessorRegistry.h"
#include "../simulation/SimulationController.h"
#include "../services/ProgramGenerationService.h"
#include "../strategies/StrategyFactory.h"
#include "../strategies/hole/HoleStrategyUtils.h"
#include "../strategies/mill/SlotMachiningGeometry.h"
#include "../tool/ToolLibrary.h"

#include <QApplication>
#include <QFrame>
#include <QGroupBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLibraryInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QDebug>
#include <QQuaternion>
#include <QSignalBlocker>
#include <QSet>
#include <QStackedWidget>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <limits>

namespace {

static bool isHoleFeature(const MachiningFeature &feature)
{
    return feature.kind == FeatureKind::Hole || feature.kind == FeatureKind::Thread;
}

static bool isSlotFeature(const MachiningFeature &feature)
{
    return feature.kind == FeatureKind::Slot;
}

static void showFeatureInStrategyPanel(StrategyPanel *panel, const MachiningFeature &feature)
{
    if (!panel) {
        return;
    }
    if (isHoleFeature(feature)) {
        panel->setFeature(feature);
    } else {
        panel->setContourFeature(feature);
    }
}

static void clearStrategyPanelSelection(StrategyPanel *panel)
{
    if (!panel) {
        return;
    }
    panel->clearFeature();
    panel->clearContourFeature();
}

static QVector3D normalizedOr(const QVector3D &vector,
                              const QVector3D &fallback = QVector3D(0.0f, 0.0f, 1.0f))
{
    const float len2 = vector.lengthSquared();
    if (len2 <= 1.0e-8f) {
        return fallback;
    }
    QVector3D normalized = vector;
    normalized.normalize();
    return normalized;
}

static QVector3D averageFaceNormal(const MeshData &mesh, int faceIndex)
{
    QVector3D normal;
    for (const Triangle &tri : mesh.triangles) {
        if (tri.faceIndex == faceIndex) {
            normal += tri.normal;
        }
    }
    return normalizedOr(normal, QVector3D());
}

#ifndef CNEXT_ENABLE_OCC
static FaceRegion regionFromSetupAxis(const QVector3D &axis)
{
    const QVector3D n = normalizedOr(axis, QVector3D());
    if (n.lengthSquared() <= 1.0e-8f) {
        return FaceRegion::Unknown;
    }

    if (std::abs(n.z()) >= 0.65f) {
        return n.z() >= 0.0f ? FaceRegion::Front : FaceRegion::Back;
    }
    return FaceRegion::Side;
}

static int regionPriority(FaceRegion region)
{
    switch (region) {
    case FaceRegion::Front: return 3;
    case FaceRegion::Back:  return 2;
    case FaceRegion::Side:  return 1;
    case FaceRegion::Unknown:
    default:
        return 0;
    }
}

static MeshData rotateMesh(const MeshData &mesh, const QQuaternion &rotation)
{
    MeshData rotated = mesh;
    rotated.bbMin = QVector3D( 1.0e9f,  1.0e9f,  1.0e9f);
    rotated.bbMax = QVector3D(-1.0e9f, -1.0e9f, -1.0e9f);

    auto updateBounds = [&rotated](const QVector3D &point) {
        rotated.bbMin.setX(std::min(rotated.bbMin.x(), point.x()));
        rotated.bbMin.setY(std::min(rotated.bbMin.y(), point.y()));
        rotated.bbMin.setZ(std::min(rotated.bbMin.z(), point.z()));
        rotated.bbMax.setX(std::max(rotated.bbMax.x(), point.x()));
        rotated.bbMax.setY(std::max(rotated.bbMax.y(), point.y()));
        rotated.bbMax.setZ(std::max(rotated.bbMax.z(), point.z()));
    };

    for (Triangle &tri : rotated.triangles) {
        tri.v0 = rotation.rotatedVector(tri.v0);
        tri.v1 = rotation.rotatedVector(tri.v1);
        tri.v2 = rotation.rotatedVector(tri.v2);
        tri.normal = normalizedOr(rotation.rotatedVector(tri.normal), tri.normal);
        updateBounds(tri.v0);
        updateBounds(tri.v1);
        updateBounds(tri.v2);
    }

    if (rotated.triangles.isEmpty()) {
        rotated.bbMin = QVector3D();
        rotated.bbMax = QVector3D();
    }
    return rotated;
}

static FaceRegion dominantFaceRegionFromMesh(const MeshData &mesh, const QVector<int> &faceIndices)
{
    if (faceIndices.isEmpty()) {
        return FaceRegion::Unknown;
    }

    QSet<int> faceSet;
    for (int faceIndex : faceIndices) {
        if (faceIndex > 0) {
            faceSet.insert(faceIndex);
        }
    }

    int front = 0;
    int side = 0;
    int back = 0;
    for (const Triangle &tri : mesh.triangles) {
        if (!faceSet.contains(tri.faceIndex)) {
            continue;
        }
        switch (regionFromSetupAxis(tri.normal)) {
        case FaceRegion::Front: ++front; break;
        case FaceRegion::Side:  ++side;  break;
        case FaceRegion::Back:  ++back;  break;
        case FaceRegion::Unknown:
        default:
            break;
        }
    }

    if (front == 0 && side == 0 && back == 0) {
        return FaceRegion::Unknown;
    }
    if (front >= side && front >= back) {
        return FaceRegion::Front;
    }
    if (back >= side && back >= front) {
        return FaceRegion::Back;
    }
    return FaceRegion::Side;
}

static QString planarSurfaceSubtype(FaceRegion region, double z, double minZ, double maxZ)
{
    constexpr double zTol = 1.0e-3;
    if (region == FaceRegion::Front && std::abs(z - maxZ) <= zTol) {
        return QStringLiteral("top_surface");
    }
    if (region == FaceRegion::Back && std::abs(z - minZ) <= zTol) {
        return QStringLiteral("bottom_surface");
    }
    if (region == FaceRegion::Side) {
        return QStringLiteral("side_surface");
    }
    return QStringLiteral("step_surface");
}

struct MeshZRange {
    bool valid = false;
    double minZ = 0.0;
    double maxZ = 0.0;
};

static MeshZRange featureFaceZRange(const MeshData &mesh, const QVector<int> &faceIndices)
{
    MeshZRange range;
    if (mesh.isEmpty() || faceIndices.isEmpty()) {
        return range;
    }

    QSet<int> faceSet;
    for (int faceIndex : faceIndices) {
        if (faceIndex > 0) {
            faceSet.insert(faceIndex);
        }
    }
    if (faceSet.isEmpty()) {
        return range;
    }

    double minZ =  std::numeric_limits<double>::max();
    double maxZ = -std::numeric_limits<double>::max();
    bool hasPoint = false;
    for (const Triangle &tri : mesh.triangles) {
        if (!faceSet.contains(tri.faceIndex)) {
            continue;
        }
        const QVector3D points[3] = {tri.v0, tri.v1, tri.v2};
        for (const QVector3D &point : points) {
            minZ = std::min(minZ, double(point.z()));
            maxZ = std::max(maxZ, double(point.z()));
            hasPoint = true;
        }
    }

    range.valid = hasPoint;
    if (range.valid) {
        range.minZ = minZ;
        range.maxZ = maxZ;
    }
    return range;
}

static FaceRegion slotRegionForCurrentSetup(const MeshData &mesh,
                                            const MachiningFeature &feature,
                                            FaceRegion axisRegion,
                                            FaceRegion faceRegion)
{
    const MeshZRange zRange = featureFaceZRange(mesh, feature.faceIndices);
    if (zRange.valid) {
        const double slotSpan = zRange.maxZ - zRange.minZ;
        const double tolerance = std::max(0.35, std::max(feature.depth, feature.width) * 0.15);
        if (slotSpan > 0.05 &&
            std::abs(double(mesh.bbMax.z()) - zRange.maxZ) <= tolerance) {
            return FaceRegion::Front;
        }
        if (slotSpan > 0.05 &&
            std::abs(zRange.minZ - double(mesh.bbMin.z())) <= tolerance) {
            return FaceRegion::Back;
        }
    }

    if (axisRegion == FaceRegion::Side) {
        return faceRegion != FaceRegion::Unknown ? faceRegion : axisRegion;
    }

    if (feature.depth > 0.0) {
        const double halfDepth = feature.depth * 0.5;
        const double topZ = double(feature.center.z()) + halfDepth;
        const double bottomZ = double(feature.center.z()) - halfDepth;
        const double tolerance = std::max(0.35, feature.depth * 0.15);
        if (std::abs(double(mesh.bbMax.z()) - topZ) <= tolerance) {
            return FaceRegion::Front;
        }
        if (std::abs(bottomZ - double(mesh.bbMin.z())) <= tolerance) {
            return FaceRegion::Back;
        }
    }

    if (axisRegion == FaceRegion::Front || axisRegion == FaceRegion::Back) {
        return axisRegion;
    }
    return faceRegion != FaceRegion::Unknown ? faceRegion : axisRegion;
}

static void reclassifyFeaturesForSetup(const MeshData &mesh, QVector<MachiningFeature> &features)
{
    double minFlatZ =  1.0e100;
    double maxFlatZ = -1.0e100;
    for (const MachiningFeature &feature : features) {
        if (feature.kind == FeatureKind::FlatSurface) {
            minFlatZ = std::min(minFlatZ, double(feature.center.z()));
            maxFlatZ = std::max(maxFlatZ, double(feature.center.z()));
        }
    }

    for (MachiningFeature &feature : features) {
        const FaceRegion axisRegion = regionFromSetupAxis(feature.axis);
        const FaceRegion faceRegion = dominantFaceRegionFromMesh(mesh, feature.faceIndices);

        if (feature.kind == FeatureKind::Hole ||
            feature.kind == FeatureKind::Thread ||
            feature.kind == FeatureKind::Boss) {
            if (feature.subType.contains(QStringLiteral("through_hole"))) {
                const double dzFront = std::abs(double(mesh.bbMax.z() - feature.center.z()));
                const double dzBack = std::abs(double(feature.center.z() - mesh.bbMin.z()));
                feature.region = dzFront <= dzBack ? FaceRegion::Front : FaceRegion::Back;
            } else {
                const FaceRegion openingRegion = regionFromSetupAxis(-feature.axis);
                feature.region = openingRegion != FaceRegion::Unknown ? openingRegion : axisRegion;
            }
            continue;
        }

        if (feature.kind == FeatureKind::Slot) {
            feature.region = slotRegionForCurrentSetup(mesh, feature, axisRegion, faceRegion);
            continue;
        }

        if (feature.kind == FeatureKind::FlatSurface) {
            feature.region = faceRegion != FaceRegion::Unknown ? faceRegion : axisRegion;
            feature.subType = planarSurfaceSubtype(feature.region,
                                                   double(feature.center.z()),
                                                   minFlatZ,
                                                   maxFlatZ);
            continue;
        }

        if (faceRegion != FaceRegion::Unknown &&
            regionPriority(faceRegion) >= regionPriority(axisRegion)) {
            feature.region = faceRegion;
        } else if (faceRegion != FaceRegion::Unknown) {
            feature.region = faceRegion;
        } else {
            feature.region = axisRegion;
        }
    }
}

static QVector<MachiningFeature> rotateFeatures(const QVector<MachiningFeature> &features,
                                                const MeshData &rotatedMesh,
                                                const QQuaternion &rotation)
{
    QVector<MachiningFeature> rotated = features;
    for (MachiningFeature &feature : rotated) {
        const double oldAngle = feature.angle;
        feature.center = rotation.rotatedVector(feature.center);
        feature.axis = normalizedOr(rotation.rotatedVector(feature.axis), feature.axis);
        if (feature.kind == FeatureKind::Slot) {
            const double angleRad = oldAngle * std::acos(-1.0) / 180.0;
            const QVector3D oldDir(float(std::cos(angleRad)), float(std::sin(angleRad)), 0.0f);
            QVector3D newDir = rotation.rotatedVector(oldDir);
            newDir.setZ(0.0f);
            if (newDir.lengthSquared() > 1.0e-8f) {
                newDir.normalize();
                feature.angle = std::atan2(double(newDir.y()), double(newDir.x())) *
                                (180.0 / std::acos(-1.0));
            }
        }
    }
    reclassifyFeaturesForSetup(rotatedMesh, rotated);
    return rotated;
}
#endif

static double distanceToMeshBoundaryXY(const MeshData &mesh, const QVector3D &point)
{
    if (mesh.isEmpty()) {
        return std::numeric_limits<double>::max();
    }
    const double dxMin = std::abs(double(point.x() - mesh.bbMin.x()));
    const double dxMax = std::abs(double(mesh.bbMax.x() - point.x()));
    const double dyMin = std::abs(double(point.y() - mesh.bbMin.y()));
    const double dyMax = std::abs(double(mesh.bbMax.y() - point.y()));
    return std::min(std::min(dxMin, dxMax), std::min(dyMin, dyMax));
}

static double detectOpenSlotSide(const MeshData &mesh, const MachiningFeature &feature)
{
    if (feature.kind != FeatureKind::Slot || feature.length <= 0.0) {
        return 0.0;
    }

    // Side-derived slots promoted to front machining are still blind-slot geometry here.
    // Auto-converting them to open_slot causes the toolpath center and strategy semantics to drift.
    if (feature.subType == QStringLiteral("straight_slot") &&
        std::abs(feature.axis.z()) < 0.5f) {
        return 0.0;
    }

    const double angleRad = feature.angle * std::acos(-1.0) / 180.0;
    const QVector3D lengthDir(float(std::cos(angleRad)), float(std::sin(angleRad)), 0.0f);
    const QVector3D widthDir(float(-std::sin(angleRad)), float(std::cos(angleRad)), 0.0f);
    const float halfLength = float(feature.length * 0.5);
    const float widthSample = float(std::max(0.0, feature.width * 0.35));
    const double slotHalfWidth = std::max(0.1, feature.width * 0.5);

    auto edgeBoundaryScore = [&](const QVector3D &edgeCenter) {
        const QVector3D samples[3] = {
            edgeCenter,
            edgeCenter + widthDir * widthSample,
            edgeCenter - widthDir * widthSample
        };

        double minDistance = std::numeric_limits<double>::max();
        double sumDistance = 0.0;
        int count = 0;
        for (const QVector3D &sample : samples) {
            const double distance = distanceToMeshBoundaryXY(mesh, sample);
            minDistance = std::min(minDistance, distance);
            sumDistance += distance;
            ++count;
        }
        return minDistance * 0.7 + (count > 0 ? (sumDistance / double(count)) * 0.3 : 0.0);
    };

    auto materialCoverageScore = [&](const QVector3D &edgeCenter, const QVector3D &outwardDir) {
        const double sampleOffset = std::max(0.3, std::min(feature.width, feature.length) * 0.3);
        const QVector3D probeCenter = edgeCenter + outwardDir * float(sampleOffset);
        int nearbyCount = 0;
        for (const Triangle &tri : mesh.triangles) {
            const QVector3D triCenter = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
            const QVector3D delta = triCenter - probeCenter;
            const double along = std::abs(QVector3D::dotProduct(delta, outwardDir));
            const double cross = std::abs(QVector3D::dotProduct(delta, widthDir));
            if (along <= sampleOffset * 0.9 && cross <= slotHalfWidth * 0.9) {
                ++nearbyCount;
            }
        }
        return nearbyCount;
    };

    const QVector3D plusPoint = feature.center + lengthDir * halfLength;
    const QVector3D minusPoint = feature.center - lengthDir * halfLength;
    const double plusDistance = edgeBoundaryScore(plusPoint);
    const double minusDistance = edgeBoundaryScore(minusPoint);
    const int plusCoverage = materialCoverageScore(plusPoint, lengthDir);
    const int minusCoverage = materialCoverageScore(minusPoint, -lengthDir);
    if (feature.subType != QStringLiteral("open_slot")) {
        const double width = feature.width > 0.0 ? feature.width : feature.length;
        const double tolerance = std::max(0.35, std::min(width, feature.length) * 0.35);
        const bool boundarySuggestsClosed = plusDistance > tolerance && minusDistance > tolerance;
        const bool coverageSuggestsClosed = plusCoverage > 3 && minusCoverage > 3;
        if (boundarySuggestsClosed && coverageSuggestsClosed) {
            return 0.0;
        }
    }
    if (plusCoverage <= 1 && minusCoverage > plusCoverage + 1) {
        return 1.0;
    }
    if (minusCoverage <= 1 && plusCoverage > minusCoverage + 1) {
        return -1.0;
    }
    return plusDistance <= minusDistance ? 1.0 : -1.0;
}

static double machiningTopZ(const MachiningFeature &feature)
{
    if (feature.kind == FeatureKind::Slot && feature.depth > 0.0) {
        const bool sideDerivedSlot = feature.region == FaceRegion::Side ||
                                     std::abs(feature.axis.z()) < 0.5f;
        if (sideDerivedSlot && feature.width > 0.0) {
            return double(feature.center.z()) + feature.width * 0.5;
        }
        return double(feature.center.z()) + feature.depth * 0.5;
    }
    return double(feature.center.z());
}

static bool isFrontReachableSlot(const MeshData &mesh, const MachiningFeature &feature)
{
    if (feature.kind != FeatureKind::Slot || mesh.isEmpty() || feature.depth <= 0.0) {
        return false;
    }
    const double topZ = machiningTopZ(feature);
    const double tolerance = std::max(0.35, feature.depth * 0.15);
    return std::abs(double(mesh.bbMax.z()) - topZ) <= tolerance;
}

static bool nearlyEqual(double a, double b, double tolerance = 1.0e-6)
{
    return std::abs(a - b) <= tolerance;
}

static int activeRegionOrder(FaceRegion featureRegion, FaceRegion activeRegion)
{
    if (activeRegion == FaceRegion::Unknown) {
        return 0;
    }
    return featureRegion == activeRegion ? 0 : 1;
}

static QVector<int> prioritizeFeatureIndicesByActiveRegion(QVector<int> indices,
                                                           const QVector<MachiningFeature> &features,
                                                           FaceRegion activeRegion)
{
    if (activeRegion == FaceRegion::Unknown) {
        return indices;
    }

    std::stable_sort(indices.begin(), indices.end(),
                     [&](int a, int b) {
        const FaceRegion regionA = (a >= 0 && a < features.size())
            ? features[a].region
            : FaceRegion::Unknown;
        const FaceRegion regionB = (b >= 0 && b < features.size())
            ? features[b].region
            : FaceRegion::Unknown;
        return activeRegionOrder(regionA, activeRegion) <
               activeRegionOrder(regionB, activeRegion);
    });
    return indices;
}

static QVector<HoleFeature> sortHolesByActiveRegionThenNearest(QVector<HoleFeature> holes,
                                                               FaceRegion activeRegion)
{
    if (holes.size() <= 1 || activeRegion == FaceRegion::Unknown) {
        return sortHolesByNearestNeighbor(holes);
    }

    QVector<HoleFeature> active;
    QVector<HoleFeature> other;
    for (const HoleFeature &hole : holes) {
        if (hole.region == activeRegion) {
            active.append(hole);
        } else {
            other.append(hole);
        }
    }

    active = sortHolesByNearestNeighbor(active);
    other = sortHolesByNearestNeighbor(other);
    active += other;
    return active;
}

static ContourFeature adaptSideTaggedSlotForBlindFrontMilling(ContourFeature feature,
                                                              const QString &strategyId)
{
    Q_UNUSED(strategyId);
    if (feature.region == FaceRegion::Front &&
        std::abs(feature.axis.z()) < 0.5f) {
        feature.angle = std::fmod(feature.angle + 90.0, 360.0);
        if (feature.angle < 0.0) {
            feature.angle += 360.0;
        }
    }
    return feature;
}

static double sideSlotFrontDepthLimit(const MachiningFeature &feature)
{
    if (feature.kind == FeatureKind::Slot &&
        (feature.region == FaceRegion::Side || std::abs(feature.axis.z()) < 0.5f) &&
        feature.width > 0.0) {
        return std::min(feature.depth > 0.0 ? feature.depth : feature.width, feature.width);
    }
    return feature.depth;
}

static bool refineSideTaggedSlotProjection(const MachiningFeature &source,
                                           const MeshData &mesh,
                                           ContourFeature &contour)
{
    if (source.kind != FeatureKind::Slot ||
        (source.region != FaceRegion::Side && std::abs(source.axis.z()) >= 0.5f) ||
        source.faceIndices.isEmpty() ||
        mesh.isEmpty()) {
        return false;
    }

    auto collectProjection = [&](const QVector<int> &faceIndices,
                                 double &minU,
                                 double &maxU,
                                 double &minV,
                                 double &maxV,
                                 int &pointCount) {
        QSet<int> faceSet;
        for (int faceIndex : faceIndices) {
            if (faceIndex > 0) {
                faceSet.insert(faceIndex);
            }
        }
        if (faceSet.isEmpty()) {
            return false;
        }

        minU =  std::numeric_limits<double>::max();
        maxU = -std::numeric_limits<double>::max();
        minV =  std::numeric_limits<double>::max();
        maxV = -std::numeric_limits<double>::max();
        pointCount = 0;

        const double angleRad = contour.angle * std::acos(-1.0) / 180.0;
        const QVector3D lengthDir(float(std::cos(angleRad)), float(std::sin(angleRad)), 0.0f);
        const QVector3D widthDir(float(-std::sin(angleRad)), float(std::cos(angleRad)), 0.0f);

        for (const Triangle &tri : mesh.triangles) {
            if (!faceSet.contains(tri.faceIndex)) {
                continue;
            }
            const QVector3D points[3] = {tri.v0, tri.v1, tri.v2};
            for (const QVector3D &point : points) {
                const double u = QVector3D::dotProduct(point, lengthDir);
                const double v = QVector3D::dotProduct(point, widthDir);
                minU = std::min(minU, u);
                maxU = std::max(maxU, u);
                minV = std::min(minV, v);
                maxV = std::max(maxV, v);
                ++pointCount;
            }
        }

        return pointCount >= 3 && maxU > minU && maxV > minV;
    };

    auto acceptProjection = [&](double minU,
                                double maxU,
                                double minV,
                                double maxV,
                                bool forceApply,
                                bool swapAxes,
                                const char *sourceLabel) {
        const double projectedSpanU = maxU - minU;
        const double projectedSpanV = maxV - minV;
        const double projectedLength = swapAxes ? projectedSpanV : projectedSpanU;
        const double projectedWidth = swapAxes ? projectedSpanU : projectedSpanV;
        const double expectedLength = contour.length > 0.0 ? contour.length : source.length;
        const double expectedWidth = contour.width > 0.0 ? contour.width : source.width;
        const bool lengthOk = expectedLength <= 0.0 ||
                              (projectedLength <= expectedLength * 1.8 &&
                               projectedLength >= expectedLength * 0.25);
        const bool widthOk = expectedWidth <= 0.0 ||
                             (projectedWidth <= expectedWidth * 1.8 &&
                              projectedWidth >= expectedWidth * 0.25);

        qDebug().noquote()
            << QStringLiteral("[slot-debug] footprint-candidate source=%1 U[%2,%3] V[%4,%5] L=%6 W=%7 expectedL=%8 expectedW=%9 accepted=%10")
                  .arg(QString::fromLatin1(sourceLabel))
                  .arg(minU, 0, 'f', 3)
                  .arg(maxU, 0, 'f', 3)
                  .arg(minV, 0, 'f', 3)
                  .arg(maxV, 0, 'f', 3)
                  .arg(projectedLength, 0, 'f', 3)
                  .arg(projectedWidth, 0, 'f', 3)
                  .arg(expectedLength, 0, 'f', 3)
                  .arg(expectedWidth, 0, 'f', 3)
                  .arg((lengthOk && widthOk) || forceApply ? QStringLiteral("yes") : QStringLiteral("no"));

        if ((!lengthOk || !widthOk) && !forceApply) {
            return false;
        }

        const double appliedAngleDeg = swapAxes ? contour.angle + 90.0 : contour.angle;
        const double angleRad = appliedAngleDeg * std::acos(-1.0) / 180.0;
        const QVector3D lengthDir(float(std::cos(angleRad)), float(std::sin(angleRad)), 0.0f);
        const QVector3D widthDir(float(-std::sin(angleRad)), float(std::cos(angleRad)), 0.0f);
        const double centerAlongLength = swapAxes ? (minV + maxV) * 0.5 : (minU + maxU) * 0.5;
        const double centerAlongWidth = swapAxes ? (minU + maxU) * 0.5 : (minV + maxV) * 0.5;
        QVector3D center = lengthDir * float(centerAlongLength) + widthDir * float(centerAlongWidth);
        center.setZ(contour.center.z());

        qDebug().noquote()
            << QStringLiteral("[slot-debug] footprint-applied source=%1 oldCenter=(%2,%3,%4) newCenter=(%5,%6,%7) L=%8 W=%9 A=%10 swap=%11")
                  .arg(QString::fromLatin1(sourceLabel))
                  .arg(contour.center.x(), 0, 'f', 3)
                  .arg(contour.center.y(), 0, 'f', 3)
                  .arg(contour.center.z(), 0, 'f', 3)
                  .arg(center.x(), 0, 'f', 3)
                  .arg(center.y(), 0, 'f', 3)
                  .arg(center.z(), 0, 'f', 3)
                  .arg(projectedLength, 0, 'f', 3)
                  .arg(projectedWidth, 0, 'f', 3)
                  .arg(std::fmod(appliedAngleDeg + 360.0, 360.0), 0, 'f', 3)
                  .arg(swapAxes ? QStringLiteral("yes") : QStringLiteral("no"));

        contour.center = center;
        contour.length = projectedLength;
        contour.width = projectedWidth;
        contour.angle = std::fmod(appliedAngleDeg + 360.0, 360.0);
        return true;
    };

    auto acceptProjectionCenterOnly = [&](double minU,
                                          double maxU,
                                          double minV,
                                          double maxV,
                                          const char *sourceLabel) {
        if (source.subType == QStringLiteral("straight_slot")) {
            qDebug().noquote()
                << QStringLiteral("[slot-debug] footprint-center-candidate source=%1 skipped=straight-slot")
                      .arg(QString::fromLatin1(sourceLabel));
            return false;
        }

        const double projectedLength = maxU - minU;
        const double projectedWidth = maxV - minV;
        const double expectedLength = contour.length > 0.0 ? contour.length : source.length;
        const double expectedWidth = contour.width > 0.0 ? contour.width : source.width;
        const bool lengthOk = expectedLength <= 0.0 ||
                              (projectedLength <= expectedLength * 1.8 &&
                               projectedLength >= expectedLength * 0.25);
        const bool widthIsGeometrySpan = expectedWidth > 0.0 &&
                                         projectedWidth > expectedWidth * 1.8;

        qDebug().noquote()
            << QStringLiteral("[slot-debug] footprint-center-candidate source=%1 L=%2 W=%3 expectedL=%4 expectedW=%5 accepted=%6")
                  .arg(QString::fromLatin1(sourceLabel))
                  .arg(projectedLength, 0, 'f', 3)
                  .arg(projectedWidth, 0, 'f', 3)
                  .arg(expectedLength, 0, 'f', 3)
                  .arg(expectedWidth, 0, 'f', 3)
                  .arg(lengthOk && widthIsGeometrySpan ? QStringLiteral("yes") : QStringLiteral("no"));

        if (!lengthOk || !widthIsGeometrySpan) {
            return false;
        }

        const double angleRad = contour.angle * std::acos(-1.0) / 180.0;
        const QVector3D lengthDir(float(std::cos(angleRad)), float(std::sin(angleRad)), 0.0f);
        const QVector3D widthDir(float(-std::sin(angleRad)), float(std::cos(angleRad)), 0.0f);
        const double midU = (minU + maxU) * 0.5;
        const double midV = (minV + maxV) * 0.5;
        QVector3D center = lengthDir * float(midU) + widthDir * float(midV);
        center.setZ(contour.center.z());

        qDebug().noquote()
            << QStringLiteral("[slot-debug] footprint-center-applied source=%1 oldCenter=(%2,%3,%4) newCenter=(%5,%6,%7) keepL=%8 keepW=%9")
                  .arg(QString::fromLatin1(sourceLabel))
                  .arg(contour.center.x(), 0, 'f', 3)
                  .arg(contour.center.y(), 0, 'f', 3)
                  .arg(contour.center.z(), 0, 'f', 3)
                  .arg(center.x(), 0, 'f', 3)
                  .arg(center.y(), 0, 'f', 3)
                  .arg(center.z(), 0, 'f', 3)
                  .arg(contour.length, 0, 'f', 3)
                  .arg(contour.width, 0, 'f', 3);

        contour.center = center;
        return true;
    };

    QVector<int> bottomOnly;
    if (!source.faceIndices.isEmpty()) {
        bottomOnly.append(source.faceIndices.first());
    }

    double minU = 0.0;
    double maxU = 0.0;
    double minV = 0.0;
    double maxV = 0.0;
    int pointCount = 0;
    if (collectProjection(bottomOnly, minU, maxU, minV, maxV, pointCount) &&
        acceptProjection(minU, maxU, minV, maxV, false, false, "bottom-face")) {
        return true;
    }

    if (collectProjection(source.faceIndices, minU, maxU, minV, maxV, pointCount) &&
        acceptProjection(minU, maxU, minV, maxV, false, false, "all-feature-faces")) {
        return true;
    }
    if (collectProjection(source.faceIndices, minU, maxU, minV, maxV, pointCount) &&
        acceptProjectionCenterOnly(minU, maxU, minV, maxV, "all-feature-faces")) {
        return true;
    }

    qDebug().noquote()
        << QStringLiteral("[slot-debug] footprint-not-applied faceCount=%1")
              .arg(source.faceIndices.size());
    return false;
}

static void refineSlotContourFromMesh(const MachiningFeature &source,
                                      const MeshData &mesh,
                                      ContourFeature &contour)
{
    if ((source.subType == QStringLiteral("straight_slot") ||
         source.subType == QStringLiteral("arc_slot")) &&
        contour.region != FaceRegion::Front) {
        return;
    }
    refineSlotContourFromMeshData(source, mesh, contour);
}

struct FrontSlotLocalSample {
    QVector3D point;
    double u = 0.0;
    double v = 0.0;
    double z = 0.0;
    double normalZ = 0.0;
};

static QVector<FrontSlotLocalSample> collectFrontSlotLocalSamples(const MachiningFeature &source,
                                                                  const MeshData &mesh,
                                                                  const ContourFeature &contour,
                                                                  double *bottomMinUOut = nullptr,
                                                                  double *bottomMaxUOut = nullptr,
                                                                  double *bottomMinVOut = nullptr,
                                                                  double *bottomMaxVOut = nullptr);

static bool refineFrontStraightSlotFromLocalSamples(const MachiningFeature &source,
                                                    const MeshData &mesh,
                                                    ContourFeature &contour)
{
    if (contour.region != FaceRegion::Front ||
        (source.subType != QStringLiteral("straight_slot") &&
         source.subType != QStringLiteral("arc_slot"))) {
        return false;
    }

    double bottomMinU = 0.0;
    double bottomMaxU = 0.0;
    double bottomMinV = 0.0;
    double bottomMaxV = 0.0;
    const QVector<FrontSlotLocalSample> samples =
        collectFrontSlotLocalSamples(source, mesh, contour,
                                     &bottomMinU, &bottomMaxU, &bottomMinV, &bottomMaxV);
    if (samples.size() < 3) {
        return false;
    }

    double minU = std::numeric_limits<double>::max();
    double maxU = -std::numeric_limits<double>::max();
    double minV = std::numeric_limits<double>::max();
    double maxV = -std::numeric_limits<double>::max();
    double minZ = std::numeric_limits<double>::max();
    double maxZ = -std::numeric_limits<double>::max();
    for (const FrontSlotLocalSample &sample : samples) {
        minU = std::min(minU, sample.u);
        maxU = std::max(maxU, sample.u);
        minV = std::min(minV, sample.v);
        maxV = std::max(maxV, sample.v);
        minZ = std::min(minZ, sample.z);
        maxZ = std::max(maxZ, sample.z);
    }

    const double spanU = maxU - minU;
    const double spanV = maxV - minV;
    if (spanU <= 1.0e-6 || spanV <= 1.0e-6) {
        return false;
    }

    const bool sideDerivedFrontSlot =
        source.kind == FeatureKind::Slot &&
        (source.region == FaceRegion::Side || std::abs(source.axis.z()) < 0.5f);
    const double angleRad = contour.angle * std::acos(-1.0) / 180.0;
    const QVector3D lengthDir(float(std::cos(angleRad)), float(std::sin(angleRad)), 0.0f);
    const QVector3D widthDir(float(-std::sin(angleRad)), float(std::cos(angleRad)), 0.0f);
    const bool lengthAlongU = spanU >= spanV;
    const double midU = (minU + maxU) * 0.5;
    const double midV = (minV + maxV) * 0.5;
    if (sideDerivedFrontSlot) {
        const double projectedAngle = lengthAlongU ? contour.angle : contour.angle + 90.0;
        const double projectedAngleRad = projectedAngle * std::acos(-1.0) / 180.0;
        const QVector3D projectedLengthDir(float(std::cos(projectedAngleRad)),
                                           float(std::sin(projectedAngleRad)),
                                           0.0f);
        const double centerAlongProjectedLength = lengthAlongU ? midU : midV;
        QVector3D center = contour.center + projectedLengthDir * float(centerAlongProjectedLength);
        center.setZ(float(maxZ));
        contour.center = center;
        contour.length = lengthAlongU ? spanU : spanV;
        if (!lengthAlongU) {
            contour.angle = std::fmod(contour.angle + 90.0, 360.0);
            if (contour.angle < 0.0) {
                contour.angle += 360.0;
            }
        }
        if (source.width > 0.0) {
            contour.width = source.width;
        }
    }
    contour.depth = std::min(contour.depth, std::max(0.0, maxZ - minZ));

    if (lengthAlongU) {
        contour.slopeStartLength = std::max(0.0, bottomMinU - minU);
        contour.slopeEndLength = std::max(0.0, maxU - bottomMaxU);
        contour.slopeMinWidth = std::max(0.0, bottomMinV - minV);
        contour.slopeMaxWidth = std::max(0.0, maxV - bottomMaxV);
    } else {
        contour.slopeStartLength = std::max(0.0, bottomMinV - minV);
        contour.slopeEndLength = std::max(0.0, maxV - bottomMaxV);
        contour.slopeMinWidth = std::max(0.0, bottomMinU - minU);
        contour.slopeMaxWidth = std::max(0.0, maxU - bottomMaxU);
    }
    if (sideDerivedFrontSlot) {
        contour.slopeStartLength = 0.0;
        contour.slopeEndLength = 0.0;
        const double bottomWidthSpan = lengthAlongU ? bottomMaxV - bottomMinV : bottomMaxU - bottomMinU;
        const double missingWidth = std::max(0.0, contour.width - bottomWidthSpan);
        if (missingWidth > std::max(0.02, contour.width * 0.05)) {
            contour.slopeMinWidth = 0.0;
            contour.slopeMaxWidth = missingWidth;
        }
    }

    qDebug().noquote()
        << QStringLiteral("[slot-debug] front-local-slot-applied center=(%1,%2,%3) L=%4 W=%5 A=%6 U[%7,%8] V[%9,%10] bottomU[%11,%12] bottomV[%13,%14] note=%15")
              .arg(contour.center.x(), 0, 'f', 3)
              .arg(contour.center.y(), 0, 'f', 3)
              .arg(contour.center.z(), 0, 'f', 3)
              .arg(contour.length, 0, 'f', 3)
              .arg(contour.width, 0, 'f', 3)
              .arg(contour.angle, 0, 'f', 3)
              .arg(minU, 0, 'f', 3)
              .arg(maxU, 0, 'f', 3)
              .arg(minV, 0, 'f', 3)
              .arg(maxV, 0, 'f', 3)
              .arg(bottomMinU, 0, 'f', 3)
              .arg(bottomMaxU, 0, 'f', 3)
              .arg(bottomMinV, 0, 'f', 3)
              .arg(bottomMaxV, 0, 'f', 3)
              .arg(sideDerivedFrontSlot ? QStringLiteral("front-projection-preserve-depth-width")
                                         : QStringLiteral("preserve-recognized-geometry"));
    return true;
}

static bool refineSlotSlopesFromMesh(const MachiningFeature &source,
                                     const MeshData &mesh,
                                     ContourFeature &contour)
{
    if (source.kind != FeatureKind::Slot ||
        source.faceIndices.isEmpty() ||
        mesh.isEmpty() ||
        contour.length <= 0.0 ||
        contour.width <= 0.0 ||
        contour.depth <= 0.0) {
        return false;
    }

    struct SlotSample {
        double u;
        double v;
        double z;
        double normalZ;
    };

    double projectionAngleDeg = contour.angle;
    if (!std::isfinite(projectionAngleDeg)) {
        projectionAngleDeg = source.angle;
    }
    const double angleRad = projectionAngleDeg * std::acos(-1.0) / 180.0;
    const QVector3D lengthDir(float(std::cos(angleRad)), float(std::sin(angleRad)), 0.0f);
    const QVector3D widthDir(float(-std::sin(angleRad)), float(std::cos(angleRad)), 0.0f);
    QVector<SlotSample> samples;
    double minZ =  std::numeric_limits<double>::max();
    double maxZ = -std::numeric_limits<double>::max();
    if (contour.region == FaceRegion::Front) {
        const QVector<FrontSlotLocalSample> localSamples =
            collectFrontSlotLocalSamples(source, mesh, contour);
        for (const FrontSlotLocalSample &sample : localSamples) {
            samples.append({sample.u, sample.v, sample.z, sample.normalZ});
            minZ = std::min(minZ, sample.z);
            maxZ = std::max(maxZ, sample.z);
        }
    } else {
        QSet<int> faceSet;
        for (int faceIndex : source.faceIndices) {
            if (faceIndex > 0) {
                faceSet.insert(faceIndex);
            }
        }
        if (faceSet.isEmpty()) {
            return false;
        }
        for (const Triangle &tri : mesh.triangles) {
            if (!faceSet.contains(tri.faceIndex)) {
                continue;
            }
            const QVector3D points[3] = {tri.v0, tri.v1, tri.v2};
            for (const QVector3D &point : points) {
                const QVector3D rel = point - contour.center;
                const double u = QVector3D::dotProduct(rel, lengthDir);
                const double v = QVector3D::dotProduct(rel, widthDir);
                const double z = double(point.z());
                samples.append({u, v, z, double(tri.normal.z())});
                minZ = std::min(minZ, z);
                maxZ = std::max(maxZ, z);
            }
        }
    }

    if (samples.size() < 3 || maxZ <= minZ) {
        return false;
    }

    const double bottomZ = minZ;
    const double zTol = std::max(0.02, (maxZ - minZ) * 0.08);
    auto collectBottomRange = [&](bool rejectVerticalSide,
                                  double &bottomMinU,
                                  double &bottomMaxU,
                                  double &bottomMinV,
                                  double &bottomMaxV,
                                  int &bottomPointCount) {
        bottomMinU =  std::numeric_limits<double>::max();
        bottomMaxU = -std::numeric_limits<double>::max();
        bottomMinV =  std::numeric_limits<double>::max();
        bottomMaxV = -std::numeric_limits<double>::max();
        bottomPointCount = 0;

        for (const SlotSample &sample : samples) {
            if (sample.z > bottomZ + zTol) {
                continue;
            }
            if (rejectVerticalSide && std::abs(sample.normalZ) < 0.15) {
                continue;
            }
            bottomMinU = std::min(bottomMinU, sample.u);
            bottomMaxU = std::max(bottomMaxU, sample.u);
            bottomMinV = std::min(bottomMinV, sample.v);
            bottomMaxV = std::max(bottomMaxV, sample.v);
            ++bottomPointCount;
        }
    };

    double bottomMinU = 0.0;
    double bottomMaxU = 0.0;
    double bottomMinV = 0.0;
    double bottomMaxV = 0.0;
    int bottomPointCount = 0;
    collectBottomRange(true, bottomMinU, bottomMaxU, bottomMinV, bottomMaxV, bottomPointCount);
    bool usedNonVerticalBottom = bottomPointCount >= 3;
    if (!usedNonVerticalBottom) {
        collectBottomRange(false, bottomMinU, bottomMaxU, bottomMinV, bottomMaxV, bottomPointCount);
    }

    const double fullMinU = -contour.length * 0.5;
    const double fullMaxU =  contour.length * 0.5;
    const double fullMinV = -contour.width * 0.5;
    const double fullMaxV =  contour.width * 0.5;
    const double bottomSpanU = bottomMaxU - bottomMinU;
    const double bottomSpanV = bottomMaxV - bottomMinV;
    const double minBottomSpanU = std::max(0.02, contour.length * 0.05);
    const double minBottomSpanV = std::max(0.02, contour.width * 0.05);
    const double minSlope = 0.02;
    const bool sideDerivedFrontSlot =
        contour.region == FaceRegion::Front &&
        source.kind == FeatureKind::Slot &&
        (source.region == FaceRegion::Side || std::abs(source.axis.z()) < 0.5f);
    const double boundsTolU = std::max(0.05, contour.length * 0.10);
    const double boundsTolV = std::max(0.05, contour.width * 0.10);
    if (bottomPointCount < 3 ||
        bottomMinU < fullMinU - boundsTolU ||
        bottomMaxU > fullMaxU + boundsTolU ||
        bottomMinV < fullMinV - boundsTolV ||
        bottomMaxV > fullMaxV + boundsTolV) {
        qDebug().noquote()
            << QStringLiteral("[slot-debug] slope-candidate rejected reason=outside-slot-bounds bottomU[%1,%2] bottomV[%3,%4] fullU[%5,%6] fullV[%7,%8]")
                  .arg(bottomMinU, 0, 'f', 3)
                  .arg(bottomMaxU, 0, 'f', 3)
                  .arg(bottomMinV, 0, 'f', 3)
                  .arg(bottomMaxV, 0, 'f', 3)
                  .arg(fullMinU, 0, 'f', 3)
                  .arg(fullMaxU, 0, 'f', 3)
                  .arg(fullMinV, 0, 'f', 3)
                  .arg(fullMaxV, 0, 'f', 3);
        return false;
    }
    bool applied = false;
    if (bottomPointCount >= 3 && bottomSpanU > minBottomSpanU) {
        const double startSlope = std::max(0.0, bottomMinU - fullMinU);
        const double endSlope = std::max(0.0, fullMaxU - bottomMaxU);
        if (startSlope > minSlope || endSlope > minSlope) {
            contour.slopeStartLength = startSlope;
            contour.slopeEndLength = endSlope;
            applied = true;
        }
    }

    if (!sideDerivedFrontSlot && bottomPointCount >= 3 && bottomSpanV > minBottomSpanV) {
        const double minWidthSlope = std::max(0.0, bottomMinV - fullMinV);
        const double maxWidthSlope = std::max(0.0, fullMaxV - bottomMaxV);
        if (minWidthSlope > minSlope || maxWidthSlope > minSlope) {
            contour.slopeMinWidth = minWidthSlope;
            contour.slopeMaxWidth = maxWidthSlope;
            applied = true;
        }
    }

    qDebug().noquote()
        << QStringLiteral("[slot-debug] slope-candidate bottomZ=%1 tol=%2 count=%3 U[%4,%5] V[%6,%7] slopes=(%8,%9,%10,%11) applied=%12")
              .arg(bottomZ, 0, 'f', 3)
              .arg(zTol, 0, 'f', 3)
              .arg(bottomPointCount)
              .arg(bottomMinU, 0, 'f', 3)
              .arg(bottomMaxU, 0, 'f', 3)
              .arg(bottomMinV, 0, 'f', 3)
              .arg(bottomMaxV, 0, 'f', 3)
              .arg(contour.slopeStartLength, 0, 'f', 3)
              .arg(contour.slopeEndLength, 0, 'f', 3)
              .arg(contour.slopeMinWidth, 0, 'f', 3)
              .arg(contour.slopeMaxWidth, 0, 'f', 3)
              .arg(applied ? QStringLiteral("yes") : QStringLiteral("no"));
    qDebug().noquote()
        << QStringLiteral("[slot-debug] slope-bottom-source=%1")
              .arg(usedNonVerticalBottom ? QStringLiteral("non-vertical")
                                         : QStringLiteral("all-faces"));

    return applied;
}

static QVector<FrontSlotLocalSample> collectFrontSlotLocalSamples(const MachiningFeature &source,
                                                                  const MeshData &mesh,
                                                                  const ContourFeature &contour,
                                                                  double *bottomMinUOut,
                                                                  double *bottomMaxUOut,
                                                                  double *bottomMinVOut,
                                                                  double *bottomMaxVOut)
{
    QVector<FrontSlotLocalSample> result;
    if (source.kind != FeatureKind::Slot ||
        contour.region != FaceRegion::Front ||
        source.faceIndices.isEmpty() ||
        mesh.isEmpty()) {
        return result;
    }

    QSet<int> faceSet;
    for (int faceIndex : source.faceIndices) {
        if (faceIndex > 0) {
            faceSet.insert(faceIndex);
        }
    }
    if (faceSet.isEmpty()) {
        return result;
    }

    const double angleRad = contour.angle * std::acos(-1.0) / 180.0;
    const QVector3D lengthDir(float(std::cos(angleRad)), float(std::sin(angleRad)), 0.0f);
    const QVector3D widthDir(float(-std::sin(angleRad)), float(std::cos(angleRad)), 0.0f);

    QVector<FrontSlotLocalSample> allSamples;
    double minZ = std::numeric_limits<double>::max();
    double maxZ = -std::numeric_limits<double>::max();
    for (const Triangle &tri : mesh.triangles) {
        if (!faceSet.contains(tri.faceIndex)) {
            continue;
        }
        const QVector3D points[3] = {tri.v0, tri.v1, tri.v2};
        for (const QVector3D &point : points) {
            const QVector3D rel = point - contour.center;
            FrontSlotLocalSample sample;
            sample.point = point;
            sample.u = QVector3D::dotProduct(rel, lengthDir);
            sample.v = QVector3D::dotProduct(rel, widthDir);
            sample.z = double(point.z());
            sample.normalZ = double(tri.normal.z());
            allSamples.append(sample);
            minZ = std::min(minZ, sample.z);
            maxZ = std::max(maxZ, sample.z);
        }
    }
    if (allSamples.size() < 3 || maxZ <= minZ) {
        return result;
    }

    const double bottomTol = std::max(0.02, (maxZ - minZ) * 0.12);
    double bottomMinU = std::numeric_limits<double>::max();
    double bottomMaxU = -std::numeric_limits<double>::max();
    double bottomMinV = std::numeric_limits<double>::max();
    double bottomMaxV = -std::numeric_limits<double>::max();
    int bottomCount = 0;
    for (const FrontSlotLocalSample &sample : allSamples) {
        if (sample.z > minZ + bottomTol) {
            continue;
        }
        bottomMinU = std::min(bottomMinU, sample.u);
        bottomMaxU = std::max(bottomMaxU, sample.u);
        bottomMinV = std::min(bottomMinV, sample.v);
        bottomMaxV = std::max(bottomMaxV, sample.v);
        ++bottomCount;
    }
    if (bottomCount < 3) {
        return result;
    }

    const double padU = std::max(0.2, contour.length * 0.2);
    const double padV = std::max(0.15, contour.width * 0.35);
    const double topTol = std::max(0.02, (maxZ - minZ) * 0.08);
    for (const FrontSlotLocalSample &sample : allSamples) {
        const bool nearBottomWindow =
            sample.u >= bottomMinU - padU && sample.u <= bottomMaxU + padU &&
            sample.v >= bottomMinV - padV && sample.v <= bottomMaxV + padV;
        const bool nearTopOpening =
            sample.z >= maxZ - topTol &&
            sample.u >= bottomMinU - padU && sample.u <= bottomMaxU + padU &&
            sample.v >= bottomMinV - padV && sample.v <= bottomMaxV + padV;
        if (nearBottomWindow || nearTopOpening) {
            result.append(sample);
        }
    }

    if (bottomMinUOut) *bottomMinUOut = bottomMinU;
    if (bottomMaxUOut) *bottomMaxUOut = bottomMaxU;
    if (bottomMinVOut) *bottomMinVOut = bottomMinV;
    if (bottomMaxVOut) *bottomMaxVOut = bottomMaxV;
    return result;
}

static bool applyFrontReachableSlotMachiningGeometry(const MachiningFeature &source,
                                                     const MeshData &mesh,
                                                     ContourFeature &contour)
{
    if (source.kind != FeatureKind::Slot ||
        contour.region != FaceRegion::Front ||
        std::abs(source.axis.z()) >= 0.5f ||
        source.faceIndices.isEmpty() ||
        mesh.isEmpty() ||
        contour.length <= 0.0 ||
        contour.width <= 0.0) {
        return false;
    }

    struct SamplePoint {
        double x;
        double y;
        double z;
        double u;
        double v;
    };
    const double projectionAngleDeg = source.angle;
    const double projectionAngleRad = projectionAngleDeg * std::acos(-1.0) / 180.0;
    const QVector3D lengthDir(float(std::cos(projectionAngleRad)), float(std::sin(projectionAngleRad)), 0.0f);
    const QVector3D widthDir(float(-std::sin(projectionAngleRad)), float(std::cos(projectionAngleRad)), 0.0f);
    QVector<SamplePoint> samples;
    double minZ =  std::numeric_limits<double>::max();
    double maxZ = -std::numeric_limits<double>::max();
    const QVector<FrontSlotLocalSample> localSamples =
        collectFrontSlotLocalSamples(source, mesh, contour);
    for (const FrontSlotLocalSample &sample : localSamples) {
        samples.append({double(sample.point.x()), double(sample.point.y()), sample.z, sample.u, sample.v});
        minZ = std::min(minZ, sample.z);
        maxZ = std::max(maxZ, sample.z);
    }

    if (samples.size() < 3 || maxZ <= minZ) {
        return false;
    }

    const double topZ = maxZ;
    const double topTol = std::max(0.02, (maxZ - minZ) * 0.08);
    double bottomMinU =  std::numeric_limits<double>::max();
    double bottomMaxU = -std::numeric_limits<double>::max();
    double bottomMinV =  std::numeric_limits<double>::max();
    double bottomMaxV = -std::numeric_limits<double>::max();
    const double bottomTol = std::max(0.02, (maxZ - minZ) * 0.12);
    int bottomCount = 0;
    for (const SamplePoint &sample : samples) {
        if (sample.z > minZ + bottomTol) {
            continue;
        }
        bottomMinU = std::min(bottomMinU, sample.u);
        bottomMaxU = std::max(bottomMaxU, sample.u);
        bottomMinV = std::min(bottomMinV, sample.v);
        bottomMaxV = std::max(bottomMaxV, sample.v);
        ++bottomCount;
    }
    const double expectedLength = contour.length;
    const double expectedWidth = contour.width;
    const double spanTol = std::max(0.2, std::min(expectedLength, expectedWidth) * 0.35);
    bool useUAsLength = true;
    if (bottomCount >= 3) {
        const double bottomSpanU = bottomMaxU - bottomMinU;
        const double bottomSpanV = bottomMaxV - bottomMinV;
        const bool uLooksLength = std::abs(bottomSpanU - expectedLength) <= std::abs(bottomSpanU - expectedWidth) + spanTol;
        const bool vLooksLength = std::abs(bottomSpanV - expectedLength) <= std::abs(bottomSpanV - expectedWidth) + spanTol;
        if (!uLooksLength && vLooksLength) {
            useUAsLength = false;
        } else if (uLooksLength != vLooksLength) {
            useUAsLength = uLooksLength;
        } else {
            useUAsLength = bottomSpanU >= bottomSpanV;
        }
    }

    QVector3D localSeedCenter = contour.center;
    if (bottomCount >= 3) {
        const double seedU = useUAsLength ? (bottomMinU + bottomMaxU) * 0.5
                                          : (bottomMinV + bottomMaxV) * 0.5;
        const double seedV = useUAsLength ? (bottomMinV + bottomMaxV) * 0.5
                                          : (bottomMinU + bottomMaxU) * 0.5;
        localSeedCenter = QVector3D(float(source.center.x()) + lengthDir.x() * float(seedU) + widthDir.x() * float(seedV),
                                    float(source.center.y()) + lengthDir.y() * float(seedU) + widthDir.y() * float(seedV),
                                    contour.center.z());
    }
    const double localHalfLength = std::max(expectedLength * 0.55, 0.9);
    const double localHalfWidth = std::max(expectedWidth * 0.75, 0.45);
    const QVector3D seedRel(localSeedCenter.x() - source.center.x(),
                            localSeedCenter.y() - source.center.y(),
                            0.0f);
    const double seedAlongLength = QVector3D::dotProduct(seedRel, lengthDir);
    const double seedAlongWidth = QVector3D::dotProduct(seedRel, widthDir);
    const double bottomMinAlongLength = useUAsLength ? bottomMinU : bottomMinV;
    const double bottomMaxAlongLength = useUAsLength ? bottomMaxU : bottomMaxV;
    const double bottomMinAlongWidth = useUAsLength ? bottomMinV : bottomMinU;
    const double bottomMaxAlongWidth = useUAsLength ? bottomMaxV : bottomMaxU;
    const double allowedMinAlongLength = bottomMinAlongLength - std::max(0.25, expectedLength * 0.35);
    const double allowedMaxAlongLength = bottomMaxAlongLength + std::max(0.25, expectedLength * 0.35);
    const double allowedMinAlongWidth = bottomMinAlongWidth - std::max(0.20, expectedWidth * 0.35);
    const double allowedMaxAlongWidth = bottomMaxAlongWidth + std::max(0.20, expectedWidth * 0.35);
    double minX =  std::numeric_limits<double>::max();
    double maxX = -std::numeric_limits<double>::max();
    double minY =  std::numeric_limits<double>::max();
    double maxY = -std::numeric_limits<double>::max();
    double minU =  std::numeric_limits<double>::max();
    double maxU = -std::numeric_limits<double>::max();
    double minV =  std::numeric_limits<double>::max();
    double maxV = -std::numeric_limits<double>::max();
    int topPointCount = 0;
    int localPointCount = 0;
    int topZPointCount = 0;
    int topRangePointCount = 0;
    double topMinAlongLength =  std::numeric_limits<double>::max();
    double topMaxAlongLength = -std::numeric_limits<double>::max();
    double topMinAlongWidth =  std::numeric_limits<double>::max();
    double topMaxAlongWidth = -std::numeric_limits<double>::max();

    for (const SamplePoint &sample : samples) {
        if (sample.z < topZ - topTol) {
            continue;
        }
        ++topZPointCount;
        const double sampleAlongLength = useUAsLength ? sample.u : sample.v;
        const double sampleAlongWidth = useUAsLength ? sample.v : sample.u;
        topMinAlongLength = std::min(topMinAlongLength, sampleAlongLength);
        topMaxAlongLength = std::max(topMaxAlongLength, sampleAlongLength);
        topMinAlongWidth = std::min(topMinAlongWidth, sampleAlongWidth);
        topMaxAlongWidth = std::max(topMaxAlongWidth, sampleAlongWidth);
        const bool inSeedWindow = std::abs(sampleAlongLength - seedAlongLength) <= localHalfLength &&
                                  std::abs(sampleAlongWidth - seedAlongWidth) <= localHalfWidth;
        const bool inBottomWindow = sampleAlongLength >= allowedMinAlongLength &&
                                    sampleAlongLength <= allowedMaxAlongLength &&
                                    sampleAlongWidth >= allowedMinAlongWidth &&
                                    sampleAlongWidth <= allowedMaxAlongWidth;
        if (!inSeedWindow && !inBottomWindow) {
            continue;
        }
        ++topRangePointCount;
        minX = std::min(minX, sample.x);
        maxX = std::max(maxX, sample.x);
        minY = std::min(minY, sample.y);
        maxY = std::max(maxY, sample.y);
        minU = std::min(minU, sample.u);
        maxU = std::max(maxU, sample.u);
        minV = std::min(minV, sample.v);
        maxV = std::max(maxV, sample.v);
        ++topPointCount;
        ++localPointCount;
    }

    if (topPointCount < 3 || !(maxU > minU && maxV > minV)) {
        qDebug().noquote()
            << QStringLiteral("[slot-debug] front-geometry-candidate rejected reason=top-window-empty topZ=%1 tol=%2 topCount=%3 localCount=%4 topZCount=%5 topRangeCount=%6 halfL=%7 halfW=%8 seed=(%9,%10,%11) axis=%12 topL[%13,%14] topW[%15,%16] allowL[%17,%18] allowW[%19,%20] bottomU[%21,%22] bottomV[%23,%24] bottomCount=%25")
                  .arg(topZ, 0, 'f', 3)
                  .arg(topTol, 0, 'f', 3)
                  .arg(topPointCount)
                  .arg(localPointCount)
                  .arg(topZPointCount)
                  .arg(topRangePointCount)
                  .arg(localHalfLength, 0, 'f', 3)
                  .arg(localHalfWidth, 0, 'f', 3)
                  .arg(localSeedCenter.x(), 0, 'f', 3)
                  .arg(localSeedCenter.y(), 0, 'f', 3)
                  .arg(localSeedCenter.z(), 0, 'f', 3)
                  .arg(useUAsLength ? QStringLiteral("U-as-length") : QStringLiteral("V-as-length"))
                  .arg(topMinAlongLength, 0, 'f', 3)
                  .arg(topMaxAlongLength, 0, 'f', 3)
                  .arg(topMinAlongWidth, 0, 'f', 3)
                  .arg(topMaxAlongWidth, 0, 'f', 3)
                  .arg(allowedMinAlongLength, 0, 'f', 3)
                  .arg(allowedMaxAlongLength, 0, 'f', 3)
                  .arg(allowedMinAlongWidth, 0, 'f', 3)
                  .arg(allowedMaxAlongWidth, 0, 'f', 3)
                  .arg(bottomMinU, 0, 'f', 3)
                  .arg(bottomMaxU, 0, 'f', 3)
                  .arg(bottomMinV, 0, 'f', 3)
                  .arg(bottomMaxV, 0, 'f', 3)
                  .arg(bottomCount);
        return false;
    }

    const double spanX = maxX - minX;
    const double spanY = maxY - minY;
    const double spanU = maxU - minU;
    const double spanV = maxV - minV;
    const double lengthTol = std::max(0.4, expectedLength * 1.2);
    const double widthTol = std::max(0.4, expectedWidth * 1.2);

    const double spanAlongLength = useUAsLength ? spanU : spanV;
    const double spanAlongWidth = useUAsLength ? spanV : spanU;
    if (!(std::abs(spanAlongLength - expectedLength) <= lengthTol &&
          std::abs(spanAlongWidth - expectedWidth) <= widthTol)) {
        qDebug().noquote()
            << QStringLiteral("[slot-debug] front-geometry-candidate rejected X[%1,%2] spanX=%3 Y[%4,%5] spanY=%6 U[%7,%8] spanU=%9 V[%10,%11] spanV=%12 axis=%13 spanL=%14 spanW=%15 expectedL=%16 expectedW=%17 topZ=%18 topCount=%19 halfL=%20 halfW=%21 seed=(%22,%23,%24)")
                  .arg(minX, 0, 'f', 3)
                  .arg(maxX, 0, 'f', 3)
                  .arg(spanX, 0, 'f', 3)
                  .arg(minY, 0, 'f', 3)
                  .arg(maxY, 0, 'f', 3)
                  .arg(spanY, 0, 'f', 3)
                  .arg(minU, 0, 'f', 3)
                  .arg(maxU, 0, 'f', 3)
                  .arg(spanU, 0, 'f', 3)
                  .arg(minV, 0, 'f', 3)
                  .arg(maxV, 0, 'f', 3)
                  .arg(spanV, 0, 'f', 3)
                  .arg(useUAsLength ? QStringLiteral("U-as-length") : QStringLiteral("V-as-length"))
                  .arg(spanAlongLength, 0, 'f', 3)
                  .arg(spanAlongWidth, 0, 'f', 3)
                  .arg(expectedLength, 0, 'f', 3)
                  .arg(expectedWidth, 0, 'f', 3)
                  .arg(topZ, 0, 'f', 3)
                  .arg(topPointCount)
                  .arg(localHalfLength, 0, 'f', 3)
                  .arg(localHalfWidth, 0, 'f', 3)
                  .arg(localSeedCenter.x(), 0, 'f', 3)
                  .arg(localSeedCenter.y(), 0, 'f', 3)
                  .arg(localSeedCenter.z(), 0, 'f', 3);
        return false;
    }

    const QVector3D oldCenter = contour.center;
    const double centerAlongLength = useUAsLength ? (minU + maxU) * 0.5
                                                  : (minV + maxV) * 0.5;
    const double centerAlongWidth = useUAsLength ? (minV + maxV) * 0.5
                                                 : (minU + maxU) * 0.5;
    const bool worldXLooksLength = std::abs(spanX - expectedLength) <= std::abs(spanX - expectedWidth);
    const bool worldYLooksLength = std::abs(spanY - expectedLength) <= std::abs(spanY - expectedWidth);
    if (worldXLooksLength != worldYLooksLength) {
        contour.angle = worldXLooksLength ? 0.0 : 90.0;
    } else if (!useUAsLength) {
        contour.angle = std::fmod(contour.angle + 90.0, 360.0);
        if (contour.angle < 0.0) {
            contour.angle += 360.0;
        }
    }
    contour.center = QVector3D(float(source.center.x()) + lengthDir.x() * float(centerAlongLength) + widthDir.x() * float(centerAlongWidth),
                               float(source.center.y()) + lengthDir.y() * float(centerAlongLength) + widthDir.y() * float(centerAlongWidth),
                               contour.center.z());

    qDebug().noquote()
        << QStringLiteral("[slot-debug] front-geometry-applied oldCenter=(%1,%2,%3) newCenter=(%4,%5,%6) angle=%7 axis=%8 localAxis=%9 edgeW=%10 centerW=%11 centerL=%12 X[%13,%14] Y[%15,%16] U[%17,%18] V[%19,%20] topZ=%21 topCount=%22 halfL=%23 halfW=%24 seed=(%25,%26,%27) bottomU[%28,%29] bottomV[%30,%31] bottomCount=%32 note=%33")
              .arg(oldCenter.x(), 0, 'f', 3)
              .arg(oldCenter.y(), 0, 'f', 3)
              .arg(oldCenter.z(), 0, 'f', 3)
              .arg(contour.center.x(), 0, 'f', 3)
              .arg(contour.center.y(), 0, 'f', 3)
              .arg(contour.center.z(), 0, 'f', 3)
              .arg(contour.angle, 0, 'f', 3)
              .arg(useUAsLength ? QStringLiteral("preserve-recognized-center")
                                : QStringLiteral("rewrite-center-angle"))
              .arg(useUAsLength ? QStringLiteral("U-as-length") : QStringLiteral("V-as-length"))
              .arg(useUAsLength ? ((minV + maxV) * 0.5) : ((minU + maxU) * 0.5), 0, 'f', 3)
              .arg(centerAlongWidth, 0, 'f', 3)
              .arg(centerAlongLength, 0, 'f', 3)
              .arg(minX, 0, 'f', 3)
              .arg(maxX, 0, 'f', 3)
              .arg(minY, 0, 'f', 3)
              .arg(maxY, 0, 'f', 3)
              .arg(minU, 0, 'f', 3)
              .arg(maxU, 0, 'f', 3)
              .arg(minV, 0, 'f', 3)
              .arg(maxV, 0, 'f', 3)
              .arg(topZ, 0, 'f', 3)
              .arg(topPointCount)
              .arg(localHalfLength, 0, 'f', 3)
              .arg(localHalfWidth, 0, 'f', 3)
              .arg(localSeedCenter.x(), 0, 'f', 3)
              .arg(localSeedCenter.y(), 0, 'f', 3)
              .arg(localSeedCenter.z(), 0, 'f', 3)
              .arg(bottomMinU, 0, 'f', 3)
              .arg(bottomMaxU, 0, 'f', 3)
              .arg(bottomMinV, 0, 'f', 3)
              .arg(bottomMaxV, 0, 'f', 3)
              .arg(bottomCount)
              .arg(useUAsLength ? QStringLiteral("front-edge-only-center-refined")
                                : QStringLiteral("front-edge-only-axis-swapped"));
    return true;
}

static bool isFrontEdgeOnlySlotGeometry(const MachiningFeature &source,
                                        const MeshData &mesh,
                                        const ContourFeature &contour)
{
    if (source.kind != FeatureKind::Slot ||
        contour.region != FaceRegion::Front ||
        std::abs(source.axis.z()) >= 0.5f ||
        source.faceIndices.isEmpty() ||
        mesh.isEmpty()) {
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

    const double angleRad = source.angle * std::acos(-1.0) / 180.0;
    const QVector3D lengthDir(float(std::cos(angleRad)), float(std::sin(angleRad)), 0.0f);
    const QVector3D widthDir(float(-std::sin(angleRad)), float(std::cos(angleRad)), 0.0f);
    double minZ =  std::numeric_limits<double>::max();
    double maxZ = -std::numeric_limits<double>::max();

    struct Sample {
        double u;
        double v;
        double z;
    };
    QVector<Sample> samples;

    for (const Triangle &tri : mesh.triangles) {
        if (!faceSet.contains(tri.faceIndex)) {
            continue;
        }
        const QVector3D points[3] = {tri.v0, tri.v1, tri.v2};
        for (const QVector3D &point : points) {
            const QVector3D rel(point.x() - source.center.x(),
                                point.y() - source.center.y(),
                                0.0f);
            const double u = QVector3D::dotProduct(rel, lengthDir);
            const double v = QVector3D::dotProduct(rel, widthDir);
            const double z = double(point.z());
            samples.append({u, v, z});
            minZ = std::min(minZ, z);
            maxZ = std::max(maxZ, z);
        }
    }

    if (samples.size() < 3 || maxZ <= minZ) {
        return false;
    }

    const double topZ = maxZ;
    const double topTol = std::max(0.02, (maxZ - minZ) * 0.08);
    double minTopU =  std::numeric_limits<double>::max();
    double maxTopU = -std::numeric_limits<double>::max();
    double minTopV =  std::numeric_limits<double>::max();
    double maxTopV = -std::numeric_limits<double>::max();
    int topCount = 0;

    for (const Sample &sample : samples) {
        if (sample.z < topZ - topTol) {
            continue;
        }
        minTopU = std::min(minTopU, sample.u);
        maxTopU = std::max(maxTopU, sample.u);
        minTopV = std::min(minTopV, sample.v);
        maxTopV = std::max(maxTopV, sample.v);
        ++topCount;
    }

    if (topCount < 3) {
        return false;
    }

    const double topSpanU = maxTopU - minTopU;
    const double topSpanV = maxTopV - minTopV;
    const double minWidthSpan = std::max(0.1, contour.width * 0.35);
    const double lengthThreshold = std::max(contour.length * 0.8, contour.width * 1.5);
    return ((topSpanU <= minWidthSpan && topSpanV >= lengthThreshold) ||
            (topSpanV <= minWidthSpan && topSpanU >= lengthThreshold));
}

static bool refineFrontSlotCenterFromMesh(const MachiningFeature &source,
                                          const MeshData &mesh,
                                          ContourFeature &contour)
{
    Q_UNUSED(source);
    Q_UNUSED(mesh);
    Q_UNUSED(contour);
    qDebug().noquote()
        << QStringLiteral("[slot-debug] front-center-candidate skipped reason=disabled-unstable-projection");
    return false;

/*
    if (source.kind != FeatureKind::Slot ||
        contour.region != FaceRegion::Front ||
        std::abs(source.axis.z()) >= 0.5f ||
        source.faceIndices.isEmpty() ||
        mesh.isEmpty() ||
        contour.length <= 0.0 ||
        contour.width <= 0.0) {
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

    struct SlotSample {
        double u;
        double v;
        double z;
        double normalZ;
    };

    double projectionAngleDeg = source.angle;
    if (!std::isfinite(projectionAngleDeg)) {
        projectionAngleDeg = contour.angle;
    }
    const double angleRad = projectionAngleDeg * std::acos(-1.0) / 180.0;
    const QVector3D lengthDir(float(std::cos(angleRad)), float(std::sin(angleRad)), 0.0f);
    const QVector3D widthDir(float(-std::sin(angleRad)), float(std::cos(angleRad)), 0.0f);
    QVector<SlotSample> samples;
    double minZ =  std::numeric_limits<double>::max();
    double maxZ = -std::numeric_limits<double>::max();

    for (const Triangle &tri : mesh.triangles) {
        if (!faceSet.contains(tri.faceIndex)) {
            continue;
        }
        const QVector3D points[3] = {tri.v0, tri.v1, tri.v2};
        for (const QVector3D &point : points) {
            const QVector3D rel = point - contour.center;
            samples.append({QVector3D::dotProduct(rel, lengthDir),
                            QVector3D::dotProduct(rel, widthDir),
                            double(point.z()),
                            double(tri.normal.z())});
            minZ = std::min(minZ, double(point.z()));
            maxZ = std::max(maxZ, double(point.z()));
        }
    }

    if (samples.size() < 3 || maxZ <= minZ) {
        return false;
    }

    const double bottomZ = minZ;
    const double zTol = std::max(0.02, (maxZ - minZ) * 0.08);
    double bottomMinU =  std::numeric_limits<double>::max();
    double bottomMaxU = -std::numeric_limits<double>::max();
    double bottomMinV =  std::numeric_limits<double>::max();
    double bottomMaxV = -std::numeric_limits<double>::max();
    int bottomPointCount = 0;

    for (const SlotSample &sample : samples) {
        if (sample.z > bottomZ + zTol || std::abs(sample.normalZ) < 0.15) {
            continue;
        }
        bottomMinU = std::min(bottomMinU, sample.u);
        bottomMaxU = std::max(bottomMaxU, sample.u);
        bottomMinV = std::min(bottomMinV, sample.v);
        bottomMaxV = std::max(bottomMaxV, sample.v);
        ++bottomPointCount;
    }

    if (bottomPointCount < 3) {
        return false;
    }

    const double bottomSpanU = bottomMaxU - bottomMinU;
    const double bottomSpanV = bottomMaxV - bottomMinV;
    if (bottomSpanV < contour.length * 0.6) {
        qDebug().noquote()
            << QStringLiteral("[slot-debug] front-center-candidate rejected bottomU[%1,%2] spanU=%3 expectedW=%4 bottomV[%5,%6] spanV=%7 expectedL=%8 reason=length-span")
                  .arg(bottomMinU, 0, 'f', 3)
                  .arg(bottomMaxU, 0, 'f', 3)
                  .arg(bottomSpanU, 0, 'f', 3)
                  .arg(contour.width, 0, 'f', 3)
                  .arg(bottomMinV, 0, 'f', 3)
                  .arg(bottomMaxV, 0, 'f', 3)
                  .arg(bottomSpanV, 0, 'f', 3)
                  .arg(contour.length, 0, 'f', 3);
        return false;
    }

    const double targetCenterU = (bottomMinU + bottomMaxU) * 0.5;
    const double targetCenterV = (bottomMinV + bottomMaxV) * 0.5;
    const bool useVForLength = bottomSpanV >= bottomSpanU;
    const QVector3D delta = useVForLength
        ? (lengthDir * float(targetCenterV))
        : (lengthDir * float(targetCenterU));
    const QVector3D oldCenter = contour.center;
    contour.center += delta;

    qDebug().noquote()
        << QStringLiteral("[slot-debug] front-center-applied oldCenter=(%1,%2,%3) newCenter=(%4,%5,%6) bottomU[%7,%8] bottomV[%9,%10] axis=%11")
              .arg(oldCenter.x(), 0, 'f', 3)
              .arg(oldCenter.y(), 0, 'f', 3)
              .arg(oldCenter.z(), 0, 'f', 3)
              .arg(contour.center.x(), 0, 'f', 3)
              .arg(contour.center.y(), 0, 'f', 3)
              .arg(contour.center.z(), 0, 'f', 3)
              .arg(bottomMinU, 0, 'f', 3)
              .arg(bottomMaxU, 0, 'f', 3)
              .arg(bottomMinV, 0, 'f', 3)
              .arg(bottomMaxV, 0, 'f', 3)
              .arg(QStringLiteral("%1@%2")
                       .arg(useVForLength ? QStringLiteral("V-as-length")
                                          : QStringLiteral("U-as-length"))
                       .arg(projectionAngleDeg, 0, 'f', 3));
    return true;
*/
}

static ContourFeature toContourFeature(const MachiningFeature &feature, const MeshData *mesh = nullptr)
{
    ContourFeature contour;
    contour.subType = feature.subType;
    contour.center = feature.center;
    contour.center.setZ(float(machiningTopZ(feature)));
    contour.radius = feature.radius;
    contour.depth = feature.depth;
    contour.width = feature.width;
    contour.length = feature.length;
    contour.angle = feature.angle;
    contour.axis = feature.axis;
    contour.region = feature.region;
    if (feature.subType == QStringLiteral("open_slot")) {
        contour.openSide = 1.0;
    }
    if (mesh) {
        if (feature.region != FaceRegion::Side &&
            isFrontReachableSlot(*mesh, feature)) {
            contour.region = FaceRegion::Front;
        }
        if ((feature.region == FaceRegion::Side || std::abs(feature.axis.z()) < 0.5f) &&
            (feature.subType == QStringLiteral("straight_slot") ||
             feature.subType == QStringLiteral("arc_slot"))) {
            ContourFeature adapted = adaptSideTaggedSlotForBlindFrontMilling(
                contour,
                QStringLiteral("mill_tapered_slot"));
            if (refineSideTaggedSlotProjection(feature, *mesh, adapted)) {
                contour.center = adapted.center;
                contour.length = adapted.length;
                contour.width = adapted.width;
            }
        }
        if (!refineFrontStraightSlotFromLocalSamples(feature, *mesh, contour)) {
            refineSlotContourFromMesh(feature, *mesh, contour);
        }
        if (!(contour.region == FaceRegion::Front &&
              (feature.subType == QStringLiteral("straight_slot") ||
               feature.subType == QStringLiteral("arc_slot")))) {
            applyFrontReachableSlotMachiningGeometry(feature, *mesh, contour);
        }
        refineFrontSlotCenterFromMesh(feature, *mesh, contour);
        if (feature.kind == FeatureKind::Slot) {
            refineSlotSlopesFromMesh(feature, *mesh, contour);
            if (isFrontEdgeOnlySlotGeometry(feature, *mesh, contour)) {
                qDebug().noquote()
                    << QStringLiteral("[slot-debug] front-edge-only-slope-check center=(%1,%2,%3) L=%4 W=%5 startU=%6 endU=%7 minV=%8 maxV=%9")
                          .arg(contour.center.x(), 0, 'f', 3)
                          .arg(contour.center.y(), 0, 'f', 3)
                          .arg(contour.center.z(), 0, 'f', 3)
                          .arg(contour.length, 0, 'f', 3)
                          .arg(contour.width, 0, 'f', 3)
                          .arg(contour.slopeStartLength, 0, 'f', 3)
                          .arg(contour.slopeEndLength, 0, 'f', 3)
                          .arg(contour.slopeMinWidth, 0, 'f', 3)
                          .arg(contour.slopeMaxWidth, 0, 'f', 3);
            }
        }
        const double detectedOpenSide = detectOpenSlotSide(*mesh, feature);
        if (std::abs(detectedOpenSide) > 1.0e-6) {
            contour.openSide = detectedOpenSide;
            contour.subType = QStringLiteral("open_slot");
        }
    }
    return contour;
}

static ContourFeature applyContourOverrides(ContourFeature feature, const StrategyParams &params)
{
    if (params.values.contains(QStringLiteral("depth"))) {
        feature.depth = params.get(QStringLiteral("depth"), feature.depth);
    }
    if (params.values.contains(QStringLiteral("slotLength"))) {
        feature.length = params.get(QStringLiteral("slotLength"),
                                    feature.length > 0.0 ? feature.length : feature.radius * 2.0);
    }
    if (params.values.contains(QStringLiteral("slotWidth"))) {
        feature.width = params.get(QStringLiteral("slotWidth"),
                                   feature.width > 0.0 ? feature.width : feature.radius);
    }
    if (params.values.contains(QStringLiteral("angle"))) {
        feature.angle = params.get(QStringLiteral("angle"), feature.angle);
    }
    return feature;
}

static ContourFeature applySlotOverridesFromParams(ContourFeature feature,
                                                   const MachiningFeature &baseFeature,
                                                   const StrategyParams &params)
{
    const bool sideSlotFrontMilling = baseFeature.kind == FeatureKind::Slot &&
                                      (baseFeature.region == FaceRegion::Side ||
                                       std::abs(baseFeature.axis.z()) < 0.5f);
    const double frontDepthLimit = sideSlotFrontDepthLimit(baseFeature);
    if (params.values.contains(QStringLiteral("depth"))) {
        const double value = params.get(QStringLiteral("depth"), baseFeature.depth);
        if (sideSlotFrontMilling && frontDepthLimit > 0.0) {
            feature.depth = std::min(value, frontDepthLimit);
        } else if (!nearlyEqual(value, baseFeature.depth, 1.0e-3)) {
            feature.depth = value;
        }
    } else if (sideSlotFrontMilling && frontDepthLimit > 0.0) {
        feature.depth = frontDepthLimit;
    }
    if (params.values.contains(QStringLiteral("slotLength"))) {
        const double value = params.get(QStringLiteral("slotLength"),
                                        baseFeature.length > 0.0 ? baseFeature.length
                                                                 : baseFeature.radius * 2.0);
        const double baseValue = baseFeature.length > 0.0 ? baseFeature.length
                                                          : baseFeature.radius * 2.0;
        if (!sideSlotFrontMilling || !nearlyEqual(value, baseValue, 1.0e-3)) {
            feature.length = value;
        }
    }
    if (params.values.contains(QStringLiteral("slotWidth"))) {
        const double value = params.get(QStringLiteral("slotWidth"),
                                        baseFeature.width > 0.0 ? baseFeature.width
                                                                : baseFeature.radius);
        feature.width = value;
    }
    if (params.values.contains(QStringLiteral("angle"))) {
        const double value = params.get(QStringLiteral("angle"), baseFeature.angle);
        if (!sideSlotFrontMilling || !nearlyEqual(value, baseFeature.angle, 1.0e-3)) {
            feature.angle = value;
        }
    }
    return feature;
}

static StrategyParams slotMachiningParamsWithoutGeometry(StrategyParams params)
{
    params.values.remove(QStringLiteral("depth"));
    params.values.remove(QStringLiteral("slotLength"));
    params.values.remove(QStringLiteral("slotWidth"));
    params.values.remove(QStringLiteral("angle"));
    return params;
}

static void logSlotContourFeature(const char *label,
                                  const ContourFeature &feature,
                                  const StrategyParams &params)
{
    qDebug().noquote()
        << QStringLiteral("[slot-debug] %1 subtype=%2 region=%3 center=(%4,%5,%6) L=%7 W=%8 D=%9 A=%10 openSide=%11 paramL=%12 paramW=%13 paramD=%14 paramA=%15")
              .arg(QString::fromLatin1(label))
              .arg(feature.subType)
              .arg(faceRegionName(feature.region))
              .arg(feature.center.x(), 0, 'f', 3)
              .arg(feature.center.y(), 0, 'f', 3)
              .arg(feature.center.z(), 0, 'f', 3)
              .arg(feature.length, 0, 'f', 3)
              .arg(feature.width, 0, 'f', 3)
              .arg(feature.depth, 0, 'f', 3)
              .arg(feature.angle, 0, 'f', 3)
              .arg(feature.openSide, 0, 'f', 3)
              .arg(params.get(QStringLiteral("slotLength"), -1.0), 0, 'f', 3)
              .arg(params.get(QStringLiteral("slotWidth"), -1.0), 0, 'f', 3)
              .arg(params.get(QStringLiteral("depth"), -1.0), 0, 'f', 3)
              .arg(params.get(QStringLiteral("angle"), -1000.0), 0, 'f', 3);
}

static int findNearestHoleFeature(const QVector<MachiningFeature> &features,
                                  const QVector3D &point,
                                  FaceRegion region)
{
    int bestIndex = -1;
    float bestDistance = std::numeric_limits<float>::max();

    for (int i = 0; i < features.size(); ++i) {
        const MachiningFeature &feature = features[i];
        if (!isHoleFeature(feature)) {
            continue;
        }
        if (region != FaceRegion::Unknown &&
            feature.region != FaceRegion::Unknown &&
            feature.region != region) {
            continue;
        }

        const QVector3D axis = feature.axis.normalized();
        QVector3D delta = point - feature.center;
        if (axis.lengthSquared() > 1.0e-8f) {
            delta -= axis * QVector3D::dotProduct(delta, axis);
        } else {
            delta.setZ(0.0f);
        }
        const float distance = delta.length();
        const float tolerance = std::max(float(feature.radius * 3.0), 3.0f);
        if (distance <= tolerance && distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    return bestIndex;
}

static QString holeSetupRestrictionMessage(const HoleFeature &feature, bool zh)
{
    if (feature.faceIndices.isEmpty() || feature.radius <= 0.0) {
        return zh ? QStringLiteral("所选孔缺少有效几何，无法建立工序。")
                  : QStringLiteral("The selected hole is missing valid geometry, so an operation cannot be created.");
    }

    if (feature.region == FaceRegion::Side) {
        return zh ? QStringLiteral("当前钻孔流程只支持正面 Z 向单 Setup；侧面孔需要重新装夹并设置对应正面。")
                  : QStringLiteral("The current drilling workflow only supports a single front-face Z setup. Side holes need fixture rotation and a matching Setup.");
    }

    if (feature.region == FaceRegion::Back) {
        return zh ? QStringLiteral("反面 Setup 坐标变换尚未实现，不能在当前正面 Z 向 Setup 中建立反面孔工序。")
                  : QStringLiteral("Back-face Setup transformation is not implemented. Back-face hole operations cannot be created in the current front-face Z Setup.");
    }

    return QString();
}

static bool isSlotMillingStrategy(const QString &strategyId)
{
    return strategyId == QStringLiteral("mill_slot") ||
           strategyId == QStringLiteral("mill_blind_slot") ||
           strategyId == QStringLiteral("mill_tapered_slot");
}

static QString slotSetupRestrictionMessage(const ContourFeature &feature,
                                           const QString &strategyId,
                                           const ToolEntry &tool,
                                           const StrategyParams &params,
                                           bool zh)
{
    if (!isSlotMillingStrategy(strategyId)) {
        return QString();
    }

    if (strategyId == QStringLiteral("mill_slot") &&
        feature.subType != QStringLiteral("open_slot")) {
        return zh ? QStringLiteral("有底面的槽不能使用开口槽铣，请改用盲槽铣。")
                  : QStringLiteral("Bottomed slots cannot use open-slot milling. Use blind-slot milling instead.");
    }

    if (feature.region == FaceRegion::Front &&
        std::abs(feature.axis.z()) < 0.5f &&
        feature.subType == QStringLiteral("straight_slot") &&
        strategyId == QStringLiteral("mill_blind_slot")) {
        return zh ? QStringLiteral("当前槽为正面可达的斜底槽候选，普通盲槽铣会误用平底槽模型；请改用“斜底槽铣”。")
                  : QStringLiteral("This slot looks like a front-reachable tapered slot. Use tapered-slot milling instead of blind-slot milling.");
    }

    if (feature.region == FaceRegion::Side &&
        strategyId == QStringLiteral("mill_slot")) {
        return zh ? QStringLiteral("当前槽为侧面槽，当前槽铣流程只支持正面 Z 装夹；请先旋转装夹或使用专用侧面槽流程。")
                  : QStringLiteral("The selected slot is on a side face. The current slot milling workflow only supports a front-face Z setup.");
    }

    if (feature.region == FaceRegion::Back) {
        return zh ? QStringLiteral("当前槽为反面槽，反面装夹坐标变换尚未实现，不能用当前正面 Z 流程生成。")
                  : QStringLiteral("The selected slot is on the back face. Back-face setup transformation is not implemented yet.");
    }

    const double slotLength = params.get(QStringLiteral("slotLength"),
                                         feature.length > 0.0 ? feature.length : feature.radius * 2.0);
    const double slotWidth  = params.get(QStringLiteral("slotWidth"),
                                         feature.width > 0.0 ? feature.width : feature.radius);
    if (slotLength <= 0.0 || slotWidth <= 0.0) {
        return zh ? QStringLiteral("槽参数无效：槽长和槽宽都必须大于 0。")
                  : QStringLiteral("Invalid slot parameters: slot length and slot width must both be greater than 0.");
    }
    if (tool.diameter <= 0.0) {
        return zh ? QStringLiteral("槽铣未选择有效立铣刀。")
                  : QStringLiteral("No valid end mill is selected for slot milling.");
    }
    if (tool.diameter >= slotWidth) {
        return zh ? QStringLiteral("槽铣刀具直径必须小于槽宽。")
                  : QStringLiteral("The slot milling tool diameter must be smaller than the slot width.");
    }
    if (feature.depth <= 0.0) {
        return zh ? QStringLiteral("槽加工深度必须大于 0。")
                  : QStringLiteral("Slot machining depth must be greater than 0.");
    }
    return QString();
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_viewport(new ViewportWidget(this))
    , m_simViewport(new ViewportWidget(this))
    , m_gcodeEditor(new GCodeEditor(this))
    , m_featurePanel(new FeatureListPanel(this))
    , m_strategyPanel(new StrategyPanel(this))
    , m_toolPanel(new ToolLibraryPanel(this))
    , m_operationPanel(new OperationListPanel(this))
    , m_bottomBar(new BottomBar(this))
    , m_simCtrl(new SimulationController(this))
{
    setMinimumSize(1200, 750);
    resize(1440, 900);
    setObjectName(QStringLiteral("mainWindow"));

    createMenus();
    createToolBar();
    createPages();
    createStatusBar();
    connectSignals();
    retranslateUi();
    setStyleSheet(QStringLiteral(
        "QMainWindow#mainWindow, QWidget#centralShell, QWidget#contentPage { background: #f3f6fb; }"
        "QWidget#systemHeader { background: #111827; border: 0; }"
        "QLabel#productTitle { color: #ffffff; font-size: 17px; font-weight: 700; padding: 0; }"
        "QLabel#workspaceTitle { color: #aeb9c9; font-size: 13px; padding-left: 10px; }"
        "QLabel#cq8ConnectionBadge { color: #ffffff; background: #b42318; border: 1px solid #d92d20; border-radius: 6px; padding: 5px 9px; font-weight: 600; }"
        "QLabel#machineModeBadge, QLabel#activeTaskBadge { color: #d9e2ef; background: #253247; border: 1px solid #3b4a62; border-radius: 6px; padding: 5px 9px; }"
        "QLabel#safetyStateBadge { color: #1f2937; background: #f6c453; border: 1px solid #d9a514; border-radius: 6px; padding: 5px 9px; font-weight: 600; }"
        "QToolBar { background: #ffffff; border: 0; border-bottom: 1px solid #d7deea; spacing: 6px; padding: 6px 10px; }"
        "QToolBar QToolButton { background: #eef3ff; border: 1px solid #d7e2f5; border-radius: 6px; padding: 6px 10px; }"
        "QToolBar QToolButton:hover { background: #e3ecff; }"
        "QToolBar QComboBox { min-height: 32px; border: 1px solid #cfd7e6; border-radius: 6px; padding: 4px 8px; background: #ffffff; }"
        "QToolBar QLabel#toolbarFieldLabel { color: #52627a; font-size: 12px; font-weight: 600; padding-left: 6px; }"
        "QListWidget#pageNav { background: #101828; color: #cbd5e1; border: 0; padding: 12px 8px; }"
        "QListWidget#pageNav::item { border-radius: 8px; padding: 12px 14px; margin: 2px 4px; }"
        "QListWidget#pageNav::item:selected { background: #2f6fec; color: #ffffff; font-weight: 600; }"
        "QListWidget#pageNav::item:hover:!selected { background: #1b2434; }"
        "QToolButton[featureFilter=\"true\"] { min-height: 30px; padding: 3px 7px; background: #f7f9fc; color: #52627a; border: 1px solid #dbe3ef; border-radius: 5px; font-weight: 500; }"
        "QToolButton[featureFilter=\"true\"]:hover { background: #edf3ff; border-color: #a8bde4; }"
        "QToolButton[featureFilter=\"true\"]:checked { background: #2f6fec; color: #ffffff; border-color: #2f6fec; font-weight: 600; }"
        "QToolButton[featureFilter=\"true\"]:focus { border-color: #174fb8; }"
        "QGroupBox { background: #ffffff; border: 1px solid #dbe3ef; border-radius: 8px; margin-top: 14px; font-weight: 600; color: #172033; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }"
        "QLabel#pageHintLabel { color: #607089; padding: 4px 2px 0 2px; }"
        "QListWidget#programList, QPlainTextEdit, QTextEdit { background: #ffffff; border: 1px solid #d6deea; border-radius: 8px; }"
        "QListWidget#programList::item { padding: 8px 10px; border-radius: 6px; }"
        "QListWidget#programList::item:selected { background: #e6efff; color: #172033; }"
        "QWidget#programValidationStrip { background: #ffffff; border: 1px solid #dbe3ef; border-radius: 8px; }"
        "QWidget#designWorkflowStrip { background: #ffffff; border: 1px solid #dbe3ef; border-radius: 8px; }"
        "QToolButton[workflowStage=\"true\"] { min-height: 36px; padding: 0 12px; text-align: left; background: #f6f8fc; color: #41516a; border: 1px solid #dbe3ef; border-radius: 6px; font-weight: 600; }"
        "QToolButton[workflowStage=\"true\"]:hover { background: #edf3ff; border-color: #9ab8f2; }"
        "QToolButton[workflowState=\"ready\"] { background: #edf8f1; color: #17663a; border-color: #a7d7b7; }"
        "QToolButton[workflowState=\"active\"] { background: #fff7e5; color: #815600; border-color: #e6c36b; }"
        "QToolButton[workflowStage=\"true\"]:disabled { background: #f5f6f8; color: #929bab; border-color: #e0e4ea; }"
        "QLabel[reviewBadge=\"true\"] { color: #52627a; background: #f7f9fc; border: 1px solid #dbe3ef; border-radius: 6px; padding: 7px 10px; font-size: 13px; font-weight: 600; }"
        "QLabel[reviewState=\"ready\"] { color: #176b3a; background: #eaf8ef; border-color: #a8d9b9; }"
        "QLabel[reviewState=\"warning\"] { color: #7a4b00; background: #fff5d8; border-color: #e8c66b; }"
        "QLabel[reviewState=\"blocked\"] { color: #9f2419; background: #fff0ee; border-color: #e5aaa4; }"
        "QLabel#programEmptyState { color: #68778d; background: #f7f9fc; border: 1px dashed #c6d0df; border-radius: 6px; padding: 12px; }"
        "QLabel#panelContextHint { color: #68778d; font-size: 12px; padding: 0 2px 4px 2px; }"
        "QSplitter::handle { background: #e4eaf3; }"
        "QToolButton#machiningActionButton { min-height: 36px; border-radius: 6px; background: #eef3ff; border: 1px solid #d7e2f5; padding: 6px 10px; }"
        "QToolButton#machiningActionButton:hover { background: #e3ecff; }"
        "QToolButton#machiningSecondaryActionButton { min-height: 36px; border-radius: 6px; background: #ffffff; color: #27364d; border: 1px solid #c6d0df; padding: 6px 10px; font-weight: 500; }"
        "QToolButton#machiningSecondaryActionButton:hover { background: #f3f6fb; border-color: #9fb0c8; }"
        "QToolButton#machiningPrimaryActionButton { min-height: 36px; border-radius: 6px; background: #2f6fec; color: #ffffff; border: 1px solid #2f6fec; padding: 6px 10px; font-weight: 600; }"
        "QToolButton#machiningPrimaryActionButton:hover { background: #245fd0; }"
        "QToolButton#machineControlButton { min-height: 42px; border-radius: 6px; background: #ffffff; color: #27364d; border: 1px solid #c6d0df; padding: 7px 12px; font-weight: 600; }"
        "QToolButton#machineControlButton:disabled { background: #edf1f6; border-color: #d7deea; color: #8d9bad; }"
        "QLabel#machineFieldName { color: #68778d; font-size: 13px; }"
        "QLabel[machineFieldValue=\"true\"] { color: #172033; background: #f7f9fc; border: 1px solid #dbe3ef; border-radius: 5px; padding: 7px 9px; font-size: 13px; font-weight: 600; }"
        "QLabel#machineAxisValue { color: #10233f; background: #f7f9fc; border: 1px solid #dbe3ef; border-radius: 5px; padding: 9px 10px; font-family: Consolas; font-size: 17px; font-weight: 700; }"
        "QLabel#machineRunHint { color: #52627a; font-size: 13px; line-height: 1.35; }"
        "QLabel#machineEmptyState { color: #68778d; background: #f7f9fc; border: 1px dashed #c6d0df; border-radius: 6px; padding: 16px; }"
        "QToolButton#machiningPrimaryActionButton:disabled, QToolButton#machiningSecondaryActionButton:disabled, QToolButton#machiningActionButton:disabled { background: #edf1f6; border-color: #d7deea; color: #9aa7b8; }"));

    // Auto-load tool library from app directory (persisted from last session)
    const QString toolsPath = QCoreApplication::applicationDirPath() + QStringLiteral("/tools.json");
    ToolLibrary::instance().loadFromFile(toolsPath);
    m_toolPanel->refresh();
}

bool MainWindow::isChineseUi() const
{
    return Settings::instance().language() == QStringLiteral("zh_CN");
}

QString MainWindow::currentWorkOffset() const
{
    if (!m_wcsCombo) {
        return QStringLiteral("G54");
    }
    const QString value = m_wcsCombo->currentData().toString().trimmed().toUpper();
    return value.isEmpty() ? QStringLiteral("G54") : value;
}

static QString toolCompatibilityIssueText(const ToolCompatibilityReport &report)
{
    QStringList lines;
    for (const ToolCompatibilityIssue &issue : report.issues) {
        lines.append(QStringLiteral("• %1").arg(issue.message));
    }
    return lines.join(QLatin1Char('\n'));
}

static bool confirmToolCompatibility(QWidget *parent,
                                     const ToolCompatibilityReport &report,
                                     bool chinese)
{
    if (report.hasBlockingIssues()) {
        QMessageBox::critical(
            parent,
            chinese ? QStringLiteral("刀具与工序不适配")
                    : QStringLiteral("Tool and Operation Incompatible"),
            (chinese ? QStringLiteral("以下问题必须先修正，不能继续创建工序：\n\n")
                     : QStringLiteral("The following issues must be corrected before creating the operation:\n\n"))
                + toolCompatibilityIssueText(report));
        return false;
    }
    if (!report.hasWarnings()) {
        return true;
    }
    const QMessageBox::StandardButton answer = QMessageBox::warning(
        parent,
        chinese ? QStringLiteral("确认刀具适配风险")
                : QStringLiteral("Acknowledge Tool Compatibility Risk"),
        (chinese ? QStringLiteral("当前刀具可以继续，但存在以下差异：\n\n")
                 : QStringLiteral("The selected tool can continue, but the following differences require review:\n\n"))
            + toolCompatibilityIssueText(report)
            + (chinese ? QStringLiteral("\n\n是否已核对后续工艺并继续？")
                       : QStringLiteral("\n\nHave you reviewed the follow-up process and want to continue?")),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    return answer == QMessageBox::Yes;
}

bool MainWindow::validateSetupForProposals(
    const QList<OperationProposal> &proposals)
{
    const bool zh = isChineseUi();
    ProjectManager *project = AppController::instance().projectManager();
    if (m_activeRegion == FaceRegion::Unknown || !project
        || !project->setupOrigin().confirmed
        || !project->stockDefinition().confirmed) {
        QMessageBox::warning(
            this,
            zh ? QStringLiteral("需要先确认 Setup")
               : QStringLiteral("Setup Confirmation Required"),
            zh ? QStringLiteral("请先确认加工正面、原点、G54-G59 和矩形毛坯六向余量，然后再确认工艺方案。")
               : QStringLiteral("Confirm the front face, origin, G54-G59, and all six rectangular-stock allowances before confirming an operation proposal."));
        return false;
    }

    int blockedCount = 0;
    for (const OperationProposal &proposal : proposals) {
        if (SetupOrientation::requiresActiveRegionConfirmation(
                m_activeRegion, proposal.featureRegion())) {
            ++blockedCount;
        }
    }
    if (blockedCount == 0) {
        return true;
    }

    QMessageBox::warning(
        this,
        zh ? QStringLiteral("工艺方案被 Setup 阻止")
           : QStringLiteral("Operation Proposal Blocked by Setup"),
        zh ? QStringLiteral("%1 个待确认工序的特征不属于当前 Setup，或加工面尚未识别。\n\n"
                            "第一阶段三轴单 Setup 不允许覆盖该限制。请重新选择特征或设置正确的正面。")
                 .arg(blockedCount)
           : QStringLiteral("%1 proposed operation(s) are outside the active Setup or have an unknown machining face.\n\n"
                            "The first-stage three-axis single-Setup workflow does not allow an override. Select compatible features or set the correct front face.")
                 .arg(blockedCount));
    return false;
}

void MainWindow::setDesignWorkflowStage(QToolButton *stage,
                                        const QString &text,
                                        const QString &state,
                                        bool enabled)
{
    if (!stage) {
        return;
    }
    stage->setText(text);
    stage->setEnabled(enabled);
    stage->setProperty("workflowState", state);
    stage->style()->unpolish(stage);
    stage->style()->polish(stage);
    stage->update();
}

void MainWindow::updateDesignWorkflowSummary()
{
    ProjectManager *project = AppController::instance().projectManager();
    const bool zh = isChineseUi();
    const bool modelReady = project && !project->mesh().isEmpty();
    const bool directionReady = modelReady && m_activeRegion != FaceRegion::Unknown;
    const bool originReady = project && project->setupOrigin().confirmed;
    const bool stockReady = project && project->stockDefinition().confirmed;
    const bool setupReady = directionReady && originReady && stockReady;
    const int featureCount = project ? project->features().size() : 0;
    const int operationCount = m_operationPanel ? m_operationPanel->operations().size() : 0;
    const int programCount = project ? project->programs().size() : 0;

    setDesignWorkflowStage(
        m_designModelStage,
        modelReady
            ? (zh ? QStringLiteral("模型 · 已加载") : QStringLiteral("Model · Loaded"))
            : (zh ? QStringLiteral("模型 · 导入 STEP") : QStringLiteral("Model · Import STEP")),
        modelReady ? QStringLiteral("ready") : QStringLiteral("active"),
        true);
    setDesignWorkflowStage(
        m_designSetupStage,
        setupReady
            ? (zh ? QStringLiteral("Setup · 已确认") : QStringLiteral("Setup · Confirmed"))
            : !directionReady
                ? (zh ? QStringLiteral("Setup · 设置正面") : QStringLiteral("Setup · Set front face"))
                : !originReady
                    ? (zh ? QStringLiteral("Setup · 设置原点") : QStringLiteral("Setup · Set origin"))
                    : (zh ? QStringLiteral("Setup · 设置毛坯") : QStringLiteral("Setup · Set stock")),
        setupReady ? QStringLiteral("ready") : QStringLiteral("active"),
        modelReady);
    setDesignWorkflowStage(
        m_designFeatureStage,
        featureCount > 0
            ? (zh ? QStringLiteral("特征 · %1 个已识别").arg(featureCount)
                  : QStringLiteral("Features · %1 recognized").arg(featureCount))
            : (zh ? QStringLiteral("特征 · 等待识别") : QStringLiteral("Features · Waiting")),
        featureCount > 0 ? QStringLiteral("ready") : QStringLiteral("active"),
        modelReady);
    setDesignWorkflowStage(
        m_designOperationStage,
        operationCount > 0
            ? (zh ? QStringLiteral("工序 · %1 道已确认").arg(operationCount)
                  : QStringLiteral("Operations · %1 confirmed").arg(operationCount))
            : (zh ? QStringLiteral("工序 · 等待确认") : QStringLiteral("Operations · Waiting")),
        operationCount > 0 ? QStringLiteral("ready") : QStringLiteral("active"),
        featureCount > 0);
    setDesignWorkflowStage(
        m_designProgramStage,
        programCount > 0
            ? (zh ? QStringLiteral("程序 · %1 个快照").arg(programCount)
                  : QStringLiteral("Programs · %1 snapshot(s)").arg(programCount))
            : (zh ? QStringLiteral("程序 · 等待生成") : QStringLiteral("Programs · Waiting")),
        programCount > 0 ? QStringLiteral("ready") : QStringLiteral("active"),
        operationCount > 0);

    if (m_designModelStage) {
        m_designModelStage->setToolTip(
            zh ? QStringLiteral("导入 STEP 模型并建立项目几何基线。")
               : QStringLiteral("Import a STEP model and establish the project geometry baseline."));
    }
    if (m_designSetupStage) {
        m_designSetupStage->setToolTip(
            !modelReady
                ? (zh ? QStringLiteral("请先导入模型。") : QStringLiteral("Import a model first."))
                : !directionReady
                    ? (zh ? QStringLiteral("先人工选择并确认当前加工正面。")
                          : QStringLiteral("Select and confirm the active machining front face first."))
                    : !originReady
                        ? (zh ? QStringLiteral("确认九点定位或自定义原点、偏移和 G54-G59。")
                              : QStringLiteral("Confirm a nine-point or custom origin, offsets, and G54-G59."))
                        : (zh ? QStringLiteral("核对零件尺寸并人工确认矩形毛坯六向余量。")
                              : QStringLiteral("Review part dimensions and confirm all six rectangular-stock allowances.")));
    }
    if (m_designFeatureStage) {
        m_designFeatureStage->setToolTip(
            !modelReady
                ? (zh ? QStringLiteral("请先导入模型。") : QStringLiteral("Import a model first."))
                : (zh ? QStringLiteral("查看已识别特征并人工选择加工对象。")
                      : QStringLiteral("Review recognized features and manually choose machining targets.")));
    }
    if (m_designOperationStage) {
        m_designOperationStage->setToolTip(
            featureCount == 0
                ? (zh ? QStringLiteral("当前没有可确认的特征。") : QStringLiteral("No features are available for confirmation."))
                : (zh ? QStringLiteral("查看、排序和编辑已确认工序。")
                      : QStringLiteral("Review, reorder, and edit confirmed operations.")));
    }
    if (m_designProgramStage) {
        m_designProgramStage->setToolTip(
            operationCount == 0
                ? (zh ? QStringLiteral("请先人工确认至少一道工序。")
                      : QStringLiteral("Confirm at least one operation first."))
                : (zh ? QStringLiteral("进入程序验证，检查最终 CQ8 程序。")
                      : QStringLiteral("Open Program Validation and inspect the final CQ8 program.")));
    }
}

void MainWindow::updateProgramActionAvailability()
{
    const bool hasProgram = m_gcodeEditor &&
        !m_gcodeEditor->toPlainText().trimmed().isEmpty();
    bool outputReady = hasProgram;

    if (outputReady) {
        outputReady = GCodeSafetyValidator::validate(
            m_gcodeEditor->toPlainText()).ok;
    }

    ProjectManager *project = AppController::instance().projectManager();
    if (outputReady && project) {
        outputReady = !project->setupFingerprint().isEmpty();
    }
    if (outputReady && project && !m_currentProgramId.trimmed().isEmpty()) {
        const ProgramEntry program = project->programById(m_currentProgramId);
        if (!program.id.isEmpty()) {
            const ProgramSnapshotStatus status = ProgramSnapshotStatus::evaluate(
                m_operationPanel ? m_operationPanel->operations()
                                 : QList<MachiningOperation>(),
                program.sourceOperationIds,
                program.sourceOperationFingerprint,
                project->setupFingerprint(),
                program.setupFingerprint);
            outputReady = status.okForOutput();
        }
    }

    if (m_actExportGCode) {
        m_actExportGCode->setEnabled(outputReady);
    }
    if (m_actSendToMachine) {
        m_actSendToMachine->setEnabled(outputReady);
    }
    if (m_actSimPlay) {
        m_actSimPlay->setEnabled(hasProgram);
    }
    if (m_actSimPause) {
        m_actSimPause->setEnabled(hasProgram);
    }
    if (m_actSimStop) {
        m_actSimStop->setEnabled(hasProgram);
    }

    updateProgramReviewSummary();
}

void MainWindow::setProgramReviewBadge(QLabel *badge,
                                       const QString &text,
                                       const QString &state)
{
    if (!badge) {
        return;
    }
    badge->setText(text);
    badge->setProperty("reviewState", state);
    badge->style()->unpolish(badge);
    badge->style()->polish(badge);
    badge->update();
}

void MainWindow::updateProgramReviewSummary()
{
    const bool zh = isChineseUi();
    const QString gcode = m_gcodeEditor ? m_gcodeEditor->toPlainText() : QString();
    const bool hasProgram = !gcode.trimmed().isEmpty();
    bool snapshotReady = hasProgram;
    bool hasSnapshotMetadata = false;

    ProjectManager *project = AppController::instance().projectManager();
    if (hasProgram && project && !m_currentProgramId.trimmed().isEmpty()) {
        const ProgramEntry program = project->programById(m_currentProgramId);
        if (!program.id.isEmpty()) {
            hasSnapshotMetadata = !program.sourceOperationIds.isEmpty()
                || !program.sourceOperationFingerprint.trimmed().isEmpty();
            if (hasSnapshotMetadata) {
                const ProgramSnapshotStatus status = ProgramSnapshotStatus::evaluate(
                    m_operationPanel ? m_operationPanel->operations()
                                     : QList<MachiningOperation>(),
                    program.sourceOperationIds,
                    program.sourceOperationFingerprint,
                    project->setupFingerprint(),
                    program.setupFingerprint);
                snapshotReady = status.okForOutput();
            }
        }
    }

    if (!hasProgram) {
        setProgramReviewBadge(m_programSnapshotBadge,
            zh ? QStringLiteral("程序快照 · 未生成") : QStringLiteral("Snapshot · Not generated"),
            QStringLiteral("neutral"));
    } else if (!snapshotReady) {
        setProgramReviewBadge(m_programSnapshotBadge,
            zh ? QStringLiteral("程序快照 · 已过期") : QStringLiteral("Snapshot · Stale"),
            QStringLiteral("blocked"));
    } else if (hasSnapshotMetadata) {
        setProgramReviewBadge(m_programSnapshotBadge,
            zh ? QStringLiteral("程序快照 · 当前") : QStringLiteral("Snapshot · Current"),
            QStringLiteral("ready"));
    } else {
        setProgramReviewBadge(m_programSnapshotBadge,
            zh ? QStringLiteral("程序快照 · 手工程序") : QStringLiteral("Snapshot · Manual program"),
            QStringLiteral("warning"));
    }

    const GCodeSafetyReport safety = hasProgram
        ? GCodeSafetyValidator::validate(gcode)
        : GCodeSafetyReport();
    if (!hasProgram) {
        setProgramReviewBadge(m_programSafetyBadge,
            zh ? QStringLiteral("安全校验 · 等待程序") : QStringLiteral("Safety · Waiting"),
            QStringLiteral("neutral"));
    } else if (safety.ok) {
        setProgramReviewBadge(m_programSafetyBadge,
            zh ? QStringLiteral("安全校验 · 通过") : QStringLiteral("Safety · Passed"),
            QStringLiteral("ready"));
    } else {
        setProgramReviewBadge(m_programSafetyBadge,
            zh ? QStringLiteral("安全校验 · 阻断") : QStringLiteral("Safety · Blocked"),
            QStringLiteral("blocked"));
    }

    if (!hasProgram || m_simulationReviewState == SimulationReviewState::Unavailable) {
        setProgramReviewBadge(m_simulationStateBadge,
            zh ? QStringLiteral("仿真 · 等待程序") : QStringLiteral("Simulation · Waiting"),
            QStringLiteral("neutral"));
    } else if (m_simulationReviewState == SimulationReviewState::Running) {
        setProgramReviewBadge(m_simulationStateBadge,
            zh ? QStringLiteral("仿真 · 运行中") : QStringLiteral("Simulation · Running"),
            QStringLiteral("warning"));
    } else if (m_simulationReviewState == SimulationReviewState::Paused) {
        setProgramReviewBadge(m_simulationStateBadge,
            zh ? QStringLiteral("仿真 · 已暂停") : QStringLiteral("Simulation · Paused"),
            QStringLiteral("warning"));
    } else if (m_simulationReviewState == SimulationReviewState::Completed) {
        setProgramReviewBadge(m_simulationStateBadge,
            zh ? QStringLiteral("仿真 · 已完成") : QStringLiteral("Simulation · Completed"),
            QStringLiteral("ready"));
    } else if (m_simulationReviewState == SimulationReviewState::Stopped) {
        setProgramReviewBadge(m_simulationStateBadge,
            zh ? QStringLiteral("仿真 · 已停止") : QStringLiteral("Simulation · Stopped"),
            QStringLiteral("warning"));
    } else {
        setProgramReviewBadge(m_simulationStateBadge,
            zh ? QStringLiteral("仿真 · 待运行") : QStringLiteral("Simulation · Ready"),
            QStringLiteral("warning"));
    }

    const bool outputReady = hasProgram && snapshotReady && safety.ok;
    setProgramReviewBadge(m_outputReadinessBadge,
        !hasProgram
            ? (zh ? QStringLiteral("CQ8 输出 · 等待程序") : QStringLiteral("CQ8 Output · Waiting"))
            : outputReady
                ? (zh ? QStringLiteral("CQ8 输出 · 已就绪") : QStringLiteral("CQ8 Output · Ready"))
                : (zh ? QStringLiteral("CQ8 输出 · 已阻断") : QStringLiteral("CQ8 Output · Blocked")),
        !hasProgram ? QStringLiteral("neutral")
                    : outputReady ? QStringLiteral("ready") : QStringLiteral("blocked"));
}

void MainWindow::syncProgramList()
{
    if (!m_programList) {
        return;
    }
    ProjectManager *project = AppController::instance().projectManager();
    if (!project) {
        return;
    }

    QSignalBlocker blocker(m_programList);
    m_updatingProgramList = true;
    m_programList->clear();

    const QList<ProgramEntry> programs = project->programs();
    if (m_programEmptyLabel) {
        m_programEmptyLabel->setVisible(programs.isEmpty());
    }
    m_programList->setVisible(!programs.isEmpty());
    const QString preferredProgramId = !m_currentProgramId.trimmed().isEmpty()
            ? m_currentProgramId
            : project->currentProgramId();
    if (programs.isEmpty()) {
        m_currentProgramId.clear();
        project->setCurrentProgramId(QString());
    }
    int selectedRow = -1;
    for (int i = 0; i < programs.size(); ++i) {
        const ProgramEntry &program = programs[i];
        ProgramSnapshotStatus snapshotStatus;
        if (m_operationPanel) {
            snapshotStatus = ProgramSnapshotStatus::evaluate(m_operationPanel->operations(),
                                                             program.sourceOperationIds,
                                                             program.sourceOperationFingerprint,
                                                             project->setupFingerprint(),
                                                             program.setupFingerprint);
        }

        QString label = program.name.trimmed();
        if (label.isEmpty()) {
            label = tr("Program %1").arg(i + 1);
        }
        if (!program.sourceOperationIds.isEmpty()) {
            label = QStringLiteral("%1 (%2)").arg(label).arg(program.sourceOperationIds.size());
        }
        label += snapshotStatus.listSuffix();
        auto *item = new QListWidgetItem(label, m_programList);
        item->setData(Qt::UserRole, program.id);
        QString tooltip = programSourceTooltip(program);
        if (!snapshotStatus.okForOutput() && !snapshotStatus.message.isEmpty()) {
            if (!tooltip.isEmpty()) {
                tooltip += QLatin1Char('\n');
            }
            tooltip += snapshotStatus.message;
            item->setForeground(Qt::darkYellow);
        }
        item->setToolTip(tooltip);
        if (program.id == preferredProgramId) {
            selectedRow = i;
        }
    }

    if (selectedRow < 0 && m_programList->count() > 0) {
        selectedRow = m_programList->count() - 1;
    }
    if (selectedRow >= 0) {
        m_programList->setCurrentRow(selectedRow);
    }
    m_updatingProgramList = false;
    updateProgramReviewSummary();
}

QString MainWindow::programSourceTooltip(const ProgramEntry &program) const
{
    QStringList lines;
    if (!program.sourceSummary.trimmed().isEmpty()) {
        lines << program.sourceSummary.trimmed();
    }
    if (!program.postProcessorId.trimmed().isEmpty()) {
        lines << tr("Post: %1").arg(program.postProcessorId);
    }
    if (program.expandedLineCount > program.lineCount && program.lineCount > 0) {
        lines << tr("CQ8 macro compression: %1 / %2 lines")
                     .arg(program.lineCount)
                     .arg(program.expandedLineCount);
    }
    if (!program.sourceOperationIds.isEmpty()) {
        lines << tr("Operations: %1").arg(program.sourceOperationIds.size());
        if (m_operationPanel) {
            const QList<MachiningOperation> &operations = m_operationPanel->operations();
            int appended = 0;
            for (const QString &operationId : program.sourceOperationIds) {
                for (const MachiningOperation &op : operations) {
                    if (op.id == operationId) {
                        lines << QStringLiteral(" - %1").arg(op.featureRef.isEmpty() ? op.strategyId : op.featureRef);
                        ++appended;
                        break;
                    }
                }
                if (appended >= 4) {
                    break;
                }
            }
            if (program.sourceOperationIds.size() > appended) {
                lines << tr(" - %1 more").arg(program.sourceOperationIds.size() - appended);
            }
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void MainWindow::syncCurrentProgramSnapshot()
{
    if (m_currentProgramId.trimmed().isEmpty() || !m_gcodeEditor) {
        return;
    }

    ProjectManager *project = AppController::instance().projectManager();
    if (!project) {
        return;
    }

    ProgramEntry program = project->programById(m_currentProgramId);
    if (program.id.isEmpty()) {
        return;
    }

    const QString currentText = m_gcodeEditor->toPlainText();
    if (program.gcodeText == currentText) {
        project->setCurrentProgramId(program.id);
        return;
    }

    program.gcodeText = currentText;
    program.mainProgramFileName.clear();
    program.packageFiles.clear();
    program.parametricPrograms.clear();
    program.macroText.clear();
    program.expandedGcodeText.clear();
    if (m_macroLibraryEditor) {
        m_macroLibraryEditor->clear();
    }
    program.postProcessorId = Settings::instance().postProcessorId();
    project->upsertProgram(program);
    project->setCurrentProgramId(program.id);
}

QString MainWindow::findProgramIdForOperation(const QString &operationId) const
{
    if (operationId.trimmed().isEmpty()) {
        return QString();
    }

    ProjectManager *project = AppController::instance().projectManager();
    if (!project) {
        return QString();
    }

    const QList<ProgramEntry> programs = project->programs();
    for (int i = programs.size() - 1; i >= 0; --i) {
        if (programs[i].sourceOperationIds.contains(operationId)) {
            return programs[i].id;
        }
    }
    return QString();
}

int MainWindow::findOperationLine(const QString &gcode,
                                  const QString &operationId,
                                  int operationNumber) const
{
    if (gcode.isEmpty()) {
        return -1;
    }

    const QStringList lines = gcode.split(QLatin1Char('\n'));
    const QString operationIdMarker = QStringLiteral("[op:%1]").arg(operationId);
    const QString numberMarker = QStringLiteral("Operation %1").arg(operationNumber);
    for (int line = 0; line < lines.size(); ++line) {
        if (!operationId.isEmpty() && lines.at(line).contains(operationIdMarker)) {
            return line;
        }
        if (operationNumber > 0 && lines.at(line).contains(numberMarker)) {
            return line;
        }
    }
    return lines.isEmpty() ? -1 : 0;
}

void MainWindow::loadProgramById(const QString &programId, bool syncSelection)
{
    if (programId.trimmed().isEmpty()) {
        return;
    }
    ProjectManager *project = AppController::instance().projectManager();
    if (!project) {
        return;
    }

    if (programId != m_currentProgramId) {
        syncCurrentProgramSnapshot();
    }

    const ProgramEntry program = project->programById(programId);
    if (program.id.isEmpty()) {
        return;
    }

    m_currentProgramId = program.id;
    project->setCurrentProgramId(program.id);
    m_gcodeEditor->setGCode(program.gcodeText);
    if (m_macroLibraryEditor) {
        m_macroLibraryEditor->setPlainText(program.macroText);
    }
    m_simCtrl->loadGCode(program.expandedGcodeText.isEmpty()
                              ? program.gcodeText
                              : program.expandedGcodeText);
    if (m_pageNav) {
        m_pageNav->setCurrentRow(1);
    }

    if (syncSelection && m_programList) {
        QSignalBlocker blocker(m_programList);
        for (int i = 0; i < m_programList->count(); ++i) {
            QListWidgetItem *item = m_programList->item(i);
            if (item && item->data(Qt::UserRole).toString() == programId) {
                m_programList->setCurrentRow(i);
                break;
            }
        }
    }

    if (!program.sourceOperationIds.isEmpty() && m_operationPanel) {
        QString targetOperationId = m_operationPanel->currentOperationId();
        if (!program.sourceOperationIds.contains(targetOperationId)) {
            targetOperationId = program.sourceOperationIds.first();
        }
        m_operationPanel->selectOperationById(targetOperationId);
    }
}

QString MainWindow::appendProgramSnapshot(const QString &baseName,
                                          const QString &gcode,
                                          const QString &sourceSummary,
                                         const QStringList &sourceOperationIds)
{
    ProjectManager *project = AppController::instance().projectManager();
    if (!project) {
        return QString();
    }

    ProgramEntry program;
    const int nextIndex = project->programs().size() + 1;
    program.name = QStringLiteral("%1 %2").arg(baseName).arg(nextIndex);
    program.postProcessorId = Settings::instance().postProcessorId();
    program.sourceOperationIds = sourceOperationIds;
    if (m_operationPanel) {
        program.sourceOperationFingerprint =
            ProgramSnapshotFingerprint::calculate(m_operationPanel->operations(), sourceOperationIds);
    }
    program.setupFingerprint = project->setupFingerprint();
    program.sourceSummary = sourceSummary;
    program.gcodeText = gcode;
    const QString programId = project->upsertProgram(program);
    loadProgramById(programId);
    return programId;
}

bool MainWindow::validateCurrentGCodeForOutput(const QString &actionName)
{
    if (!m_gcodeEditor) {
        return false;
    }

    const QString gcode = m_gcodeEditor->toPlainText();
    ProjectManager *project = AppController::instance().projectManager();
    if (!project || project->setupFingerprint().isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Setup is incomplete"),
            tr("Cannot %1 until the front face, origin, work offset, and stock allowances are confirmed.")
                .arg(actionName));
        return false;
    }
    if (project && !m_currentProgramId.trimmed().isEmpty()) {
        const ProgramEntry program = project->programById(m_currentProgramId);
        if (!program.id.isEmpty()
            && !program.sourceOperationIds.isEmpty()
            && !program.sourceOperationFingerprint.isEmpty()
            && m_operationPanel) {
            const ProgramSnapshotStatus status =
                ProgramSnapshotStatus::evaluate(m_operationPanel->operations(),
                                                program.sourceOperationIds,
                                                program.sourceOperationFingerprint,
                                                project->setupFingerprint(),
                                                program.setupFingerprint);
            if (!status.okForOutput()) {
                QMessageBox::warning(this,
                                     tr("G-code snapshot is stale"),
                                     tr("Cannot %1 because the current G-code snapshot is no longer current.\n\n%2\n\nRegenerate the program before export or machine send.")
                                         .arg(actionName, status.message));
                return false;
            }
        }
    }

    const GCodeSafetyReport report = GCodeSafetyValidator::validate(gcode);
    if (report.ok) {
        return true;
    }

    QMessageBox::warning(this,
                         tr("G-code safety check failed"),
                         tr("Cannot %1 because the current G-code failed safety checks:\n\n%2")
                             .arg(actionName, report.messages.join(QLatin1Char('\n'))));
    return false;
}

void MainWindow::createMenus()
{
    m_actImportStep = new QAction(this);
    m_actOpenProject = new QAction(this);
    m_actSaveProject = new QAction(this);
    m_actExportGCode = new QAction(this);
    m_actExit = new QAction(this);
    m_actResetCamera = new QAction(this);
    m_actLangZh = new QAction(this);
    m_actLangEn = new QAction(this);
    m_actAbout = new QAction(this);
    m_actSendToMachine = new QAction(this);
    m_actSimPlay  = new QAction(this);
    m_actSimPause = new QAction(this);
    m_actSimStop  = new QAction(this);
    m_actSetFrontFace = new QAction(this);
    m_actSetFrontFace->setCheckable(true);
    m_actSetupOrigin = new QAction(this);
    m_actOriginFromHole = new QAction(this);
    m_actStockDefinition = new QAction(this);

    m_actImportStep->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    m_actOpenProject->setShortcut(QKeySequence::Open);
    m_actSaveProject->setShortcut(QKeySequence::Save);
    m_actResetCamera->setShortcut(QKeySequence(QStringLiteral("F5")));

    m_actLangZh->setCheckable(true);
    m_actLangEn->setCheckable(true);

    m_fileMenu = menuBar()->addMenu(QString());
    m_fileMenu->addAction(m_actImportStep);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_actOpenProject);
    m_fileMenu->addAction(m_actSaveProject);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_actExportGCode);
    m_fileMenu->addAction(m_actSendToMachine);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_actExit);

    m_viewMenu = menuBar()->addMenu(QString());
    m_viewMenu->addAction(m_actResetCamera);

    m_langMenu = menuBar()->addMenu(QString());
    m_langMenu->addAction(m_actLangZh);
    m_langMenu->addAction(m_actLangEn);

    m_helpMenu = menuBar()->addMenu(QString());
    m_helpMenu->addAction(m_actAbout);
}

void MainWindow::createToolBar()
{
    m_mainToolBar = addToolBar(QString());
    m_mainToolBar->setObjectName(QStringLiteral("mainToolBar"));
    m_mainToolBar->setMovable(false);
    m_mainToolBar->addAction(m_actImportStep);
    m_mainToolBar->addAction(m_actSaveProject);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_actResetCamera);
    m_mainToolBar->addAction(m_actSetFrontFace);
    m_mainToolBar->addAction(m_actSetupOrigin);
    m_mainToolBar->addAction(m_actOriginFromHole);
    m_mainToolBar->addAction(m_actStockDefinition);
    m_mainToolBar->addSeparator();

    auto *postLabel = new QLabel(this);
    postLabel->setObjectName(QStringLiteral("toolbarFieldLabel"));
    postLabel->setProperty("field", QStringLiteral("post"));
    m_mainToolBar->addWidget(postLabel);

    m_ppCombo = new QComboBox(this);
    m_ppCombo->setMinimumWidth(170);
    const QStringList ids = PostProcessorRegistry::instance().availableIds();
    const QString currentId = Settings::instance().postProcessorId();
    for (const QString &ppId : ids) {
        PostProcessorBase *pp = PostProcessorRegistry::instance().get(ppId);
        m_ppCombo->addItem(pp ? pp->displayName() : ppId, ppId);
    }
    const int idx = m_ppCombo->findData(currentId);
    if (idx >= 0)
        m_ppCombo->setCurrentIndex(idx);
    connect(m_ppCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        const QString id = m_ppCombo->itemData(index).toString();
        Settings::instance().setPostProcessorId(id);
    });
    m_mainToolBar->addWidget(m_ppCombo);

    auto *wcsLabel = new QLabel(this);
    wcsLabel->setObjectName(QStringLiteral("toolbarFieldLabel"));
    wcsLabel->setProperty("field", QStringLiteral("wcs"));
    m_mainToolBar->addWidget(wcsLabel);

    m_wcsCombo = new QComboBox(this);
    m_wcsCombo->setMinimumWidth(90);
    for (int code = 54; code <= 59; ++code) {
        const QString wcs = QStringLiteral("G%1").arg(code);
        m_wcsCombo->addItem(wcs, wcs);
    }
    m_wcsCombo->setToolTip(QStringLiteral("Work coordinate system"));
    m_mainToolBar->addWidget(m_wcsCombo);
    connect(m_wcsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        ProjectManager *project = AppController::instance().projectManager();
        if (!project) return;
        project->setWorkOffset(currentWorkOffset());
        updateProgramActionAvailability();
        updateDesignWorkflowSummary();
    });
    m_actMachineProfile = m_mainToolBar->addAction(tr("Machine Profile"));
}

void MainWindow::createPages()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralShell"));
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *systemHeader = new QWidget(central);
    systemHeader->setObjectName(QStringLiteral("systemHeader"));
    systemHeader->setMinimumHeight(52);
    systemHeader->setMaximumHeight(52);
    auto *headerLayout = new QHBoxLayout(systemHeader);
    headerLayout->setContentsMargins(18, 8, 14, 8);
    headerLayout->setSpacing(8);

    m_productTitleLabel = new QLabel(systemHeader);
    m_productTitleLabel->setObjectName(QStringLiteral("productTitle"));
    m_workspaceTitleLabel = new QLabel(systemHeader);
    m_workspaceTitleLabel->setObjectName(QStringLiteral("workspaceTitle"));
    m_cq8ConnectionBadge = new QLabel(systemHeader);
    m_cq8ConnectionBadge->setObjectName(QStringLiteral("cq8ConnectionBadge"));
    m_machineModeBadge = new QLabel(systemHeader);
    m_machineModeBadge->setObjectName(QStringLiteral("machineModeBadge"));
    m_activeTaskBadge = new QLabel(systemHeader);
    m_activeTaskBadge->setObjectName(QStringLiteral("activeTaskBadge"));
    m_safetyStateBadge = new QLabel(systemHeader);
    m_safetyStateBadge->setObjectName(QStringLiteral("safetyStateBadge"));

    headerLayout->addWidget(m_productTitleLabel);
    headerLayout->addWidget(m_workspaceTitleLabel);
    headerLayout->addStretch(1);
    headerLayout->addWidget(m_cq8ConnectionBadge);
    headerLayout->addWidget(m_machineModeBadge);
    headerLayout->addWidget(m_activeTaskBadge);
    headerLayout->addWidget(m_safetyStateBadge);

    auto *bodyShell = new QWidget(central);
    bodyShell->setObjectName(QStringLiteral("bodyShell"));
    auto *bodyLayout = new QHBoxLayout(bodyShell);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    m_pageNav = new QListWidget(bodyShell);
    m_pageNav->setObjectName(QStringLiteral("pageNav"));
    m_pageNav->setFixedWidth(168);
    m_pageNav->setFrameShape(QFrame::NoFrame);
    m_pageNav->setSpacing(6);
    m_pageNav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pageNav->addItem(QString());
    m_pageNav->addItem(QString());
    m_pageNav->addItem(QString());

    m_pageStack = new QStackedWidget(bodyShell);

    m_designPage = new QWidget(m_pageStack);
    m_designPage->setObjectName(QStringLiteral("contentPage"));
    auto *designLayout = new QVBoxLayout(m_designPage);
    designLayout->setContentsMargins(8, 8, 8, 8);
    designLayout->setSpacing(8);

    m_designWorkflowStrip = new QWidget(m_designPage);
    m_designWorkflowStrip->setObjectName(QStringLiteral("designWorkflowStrip"));
    auto *workflowLayout = new QHBoxLayout(m_designWorkflowStrip);
    workflowLayout->setContentsMargins(10, 8, 10, 8);
    workflowLayout->setSpacing(8);
    m_designModelStage = new QToolButton(m_designWorkflowStrip);
    m_designModelStage->setObjectName(QStringLiteral("designModelStage"));
    m_designSetupStage = new QToolButton(m_designWorkflowStrip);
    m_designSetupStage->setObjectName(QStringLiteral("designSetupStage"));
    m_designFeatureStage = new QToolButton(m_designWorkflowStrip);
    m_designFeatureStage->setObjectName(QStringLiteral("designFeatureStage"));
    m_designOperationStage = new QToolButton(m_designWorkflowStrip);
    m_designOperationStage->setObjectName(QStringLiteral("designOperationStage"));
    m_designProgramStage = new QToolButton(m_designWorkflowStrip);
    m_designProgramStage->setObjectName(QStringLiteral("designProgramStage"));
    const QList<QToolButton*> workflowStages = {
        m_designModelStage, m_designSetupStage, m_designFeatureStage,
        m_designOperationStage, m_designProgramStage
    };
    for (QToolButton *stage : workflowStages) {
        stage->setProperty("workflowStage", true);
        stage->setToolButtonStyle(Qt::ToolButtonTextOnly);
        stage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        workflowLayout->addWidget(stage, 1);
    }
    designLayout->addWidget(m_designWorkflowStrip);

    auto *designLeftSplitter = new QSplitter(Qt::Vertical, m_designPage);
    designLeftSplitter->setHandleWidth(6);
    designLeftSplitter->addWidget(m_featurePanel);
    designLeftSplitter->addWidget(m_operationPanel);
    designLeftSplitter->setStretchFactor(0, 3);
    designLeftSplitter->setStretchFactor(1, 2);

    auto *designRightSplitter = new QSplitter(Qt::Vertical, m_designPage);
    designRightSplitter->setHandleWidth(6);
    designRightSplitter->addWidget(m_strategyPanel);
    designRightSplitter->addWidget(m_toolPanel);
    designRightSplitter->setStretchFactor(0, 3);
    designRightSplitter->setStretchFactor(1, 2);

    auto *designCenterSplitter = new QSplitter(Qt::Horizontal, m_designPage);
    designCenterSplitter->setHandleWidth(6);
    designCenterSplitter->addWidget(designLeftSplitter);
    designCenterSplitter->addWidget(m_viewport);
    designCenterSplitter->addWidget(designRightSplitter);
    designCenterSplitter->setStretchFactor(0, 1);
    designCenterSplitter->setStretchFactor(1, 3);
    designCenterSplitter->setStretchFactor(2, 1);
    designLayout->addWidget(designCenterSplitter, 1);

    connect(m_designModelStage, &QToolButton::clicked,
            m_actImportStep, &QAction::trigger);
    connect(m_designSetupStage, &QToolButton::clicked, this, [this]() {
        if (m_activeRegion == FaceRegion::Unknown) {
            m_actSetFrontFace->trigger();
        } else if (!AppController::instance().projectManager()->setupOrigin().confirmed) {
            onEditSetupOrigin();
        } else {
            onEditStockDefinition();
        }
    });
    connect(m_designFeatureStage, &QToolButton::clicked, this, [this]() {
        if (m_featureDock) m_featureDock->raise();
        if (m_featurePanel) m_featurePanel->setFocus();
    });
    connect(m_designOperationStage, &QToolButton::clicked, this, [this]() {
        if (m_operationDock) m_operationDock->raise();
        if (m_operationPanel) m_operationPanel->setFocus();
    });
    connect(m_designProgramStage, &QToolButton::clicked, this, [this]() {
        if (m_pageNav) m_pageNav->setCurrentRow(1);
    });

    m_machiningPage = new QWidget(m_pageStack);
    m_machiningPage->setObjectName(QStringLiteral("contentPage"));
    auto *machiningLayout = new QVBoxLayout(m_machiningPage);
    machiningLayout->setContentsMargins(8, 8, 8, 8);
    machiningLayout->setSpacing(8);

    m_programValidationStrip = new QWidget(m_machiningPage);
    m_programValidationStrip->setObjectName(QStringLiteral("programValidationStrip"));
    auto *validationLayout = new QHBoxLayout(m_programValidationStrip);
    validationLayout->setContentsMargins(10, 8, 10, 8);
    validationLayout->setSpacing(8);
    m_programSnapshotBadge = new QLabel(m_programValidationStrip);
    m_programSnapshotBadge->setObjectName(QStringLiteral("programSnapshotBadge"));
    m_programSafetyBadge = new QLabel(m_programValidationStrip);
    m_programSafetyBadge->setObjectName(QStringLiteral("programSafetyBadge"));
    m_simulationStateBadge = new QLabel(m_programValidationStrip);
    m_simulationStateBadge->setObjectName(QStringLiteral("simulationStateBadge"));
    m_outputReadinessBadge = new QLabel(m_programValidationStrip);
    m_outputReadinessBadge->setObjectName(QStringLiteral("outputReadinessBadge"));
    const QList<QLabel*> reviewBadges = {
        m_programSnapshotBadge, m_programSafetyBadge,
        m_simulationStateBadge, m_outputReadinessBadge
    };
    for (QLabel *badge : reviewBadges) {
        badge->setProperty("reviewBadge", true);
        badge->setAlignment(Qt::AlignCenter);
        badge->setMinimumHeight(34);
        validationLayout->addWidget(badge, 1);
    }
    machiningLayout->addWidget(m_programValidationStrip);

    m_machiningActionsGroup = new QGroupBox(m_machiningPage);
    m_machiningActionsGroup->setObjectName(QStringLiteral("machiningActionsGroup"));
    auto *machiningActionsLayout = new QVBoxLayout(m_machiningActionsGroup);
    machiningActionsLayout->setContentsMargins(12, 16, 12, 12);
    machiningActionsLayout->setSpacing(8);
    auto *btnExport = new QToolButton(m_machiningActionsGroup);
    btnExport->setObjectName(QStringLiteral("machiningPrimaryActionButton"));
    btnExport->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btnExport->setDefaultAction(m_actExportGCode);
    auto *btnSend = new QToolButton(m_machiningActionsGroup);
    btnSend->setObjectName(QStringLiteral("machiningSecondaryActionButton"));
    btnSend->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btnSend->setDefaultAction(m_actSendToMachine);
    auto *btnPlay = new QToolButton(m_machiningActionsGroup);
    btnPlay->setObjectName(QStringLiteral("machiningActionButton"));
    btnPlay->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btnPlay->setDefaultAction(m_actSimPlay);
    auto *btnPause = new QToolButton(m_machiningActionsGroup);
    btnPause->setObjectName(QStringLiteral("machiningActionButton"));
    btnPause->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btnPause->setDefaultAction(m_actSimPause);
    auto *btnStop = new QToolButton(m_machiningActionsGroup);
    btnStop->setObjectName(QStringLiteral("machiningActionButton"));
    btnStop->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btnStop->setDefaultAction(m_actSimStop);
    m_programList = new QListWidget(m_machiningActionsGroup);
    m_programList->setObjectName(QStringLiteral("programList"));
    m_programList->setMinimumWidth(220);
    m_programEmptyLabel = new QLabel(m_machiningActionsGroup);
    m_programEmptyLabel->setObjectName(QStringLiteral("programEmptyState"));
    m_programEmptyLabel->setWordWrap(true);
    m_programEmptyLabel->setAlignment(Qt::AlignCenter);
    m_programEmptyLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_machiningHintLabel = new QLabel(m_machiningActionsGroup);
    m_machiningHintLabel->setObjectName(QStringLiteral("pageHintLabel"));
    m_machiningHintLabel->setWordWrap(true);
    machiningActionsLayout->addWidget(m_machiningHintLabel);
    machiningActionsLayout->addWidget(m_programEmptyLabel, 1);
    machiningActionsLayout->addWidget(m_programList, 1);
    machiningActionsLayout->addSpacing(12);
    machiningActionsLayout->addWidget(btnExport);
    machiningActionsLayout->addWidget(btnSend);

    m_simulationPanel = new QGroupBox(m_machiningPage);
    m_simulationPanel->setObjectName(QStringLiteral("simulationPanel"));
    auto *simulationLayout = new QVBoxLayout(m_simulationPanel);
    simulationLayout->setContentsMargins(10, 16, 10, 10);
    simulationLayout->setSpacing(8);
    auto *simulationHint = new QLabel(m_simulationPanel);
    simulationHint->setObjectName(QStringLiteral("panelContextHint"));
    simulationHint->setProperty("context", QStringLiteral("simulation"));
    simulationHint->setWordWrap(true);
    simulationLayout->addWidget(simulationHint);
    simulationLayout->addWidget(m_simViewport, 1);
    auto *simulationActions = new QHBoxLayout();
    simulationActions->setSpacing(8);
    simulationActions->addWidget(btnPlay);
    simulationActions->addWidget(btnPause);
    simulationActions->addWidget(btnStop);
    simulationActions->addStretch(1);
    simulationLayout->addLayout(simulationActions);

    m_finalProgramPanel = new QGroupBox(m_machiningPage);
    m_finalProgramPanel->setObjectName(QStringLiteral("finalProgramPanel"));
    auto *finalProgramLayout = new QVBoxLayout(m_finalProgramPanel);
    finalProgramLayout->setContentsMargins(10, 16, 10, 10);
    finalProgramLayout->setSpacing(8);
    auto *finalProgramHint = new QLabel(m_finalProgramPanel);
    finalProgramHint->setObjectName(QStringLiteral("panelContextHint"));
    finalProgramHint->setProperty("context", QStringLiteral("program"));
    finalProgramHint->setWordWrap(true);
    finalProgramLayout->addWidget(finalProgramHint);
    finalProgramLayout->addWidget(m_gcodeEditor, 1);
    auto *macroLibraryLabel = new QLabel(m_finalProgramPanel);
    macroLibraryLabel->setObjectName(QStringLiteral("macroLibraryLabel"));
    finalProgramLayout->addWidget(macroLibraryLabel);
    m_macroLibraryEditor = new QPlainTextEdit(m_finalProgramPanel);
    m_macroLibraryEditor->setObjectName(QStringLiteral("macroLibraryEditor"));
    m_macroLibraryEditor->setReadOnly(true);
    m_macroLibraryEditor->setMinimumHeight(100);
    finalProgramLayout->addWidget(m_macroLibraryEditor);

    auto *machiningSplitter = new QSplitter(Qt::Horizontal, m_machiningPage);
    machiningSplitter->setHandleWidth(6);
    machiningSplitter->addWidget(m_machiningActionsGroup);
    machiningSplitter->addWidget(m_simulationPanel);
    machiningSplitter->addWidget(m_finalProgramPanel);
    machiningSplitter->setStretchFactor(0, 1);
    machiningSplitter->setStretchFactor(1, 2);
    machiningSplitter->setStretchFactor(2, 3);

    machiningLayout->addWidget(machiningSplitter, 1);

    m_machineControlPage = new QWidget(m_pageStack);
    m_machineControlPage->setObjectName(QStringLiteral("contentPage"));
    auto *machineLayout = new QVBoxLayout(m_machineControlPage);
    machineLayout->setContentsMargins(12, 12, 12, 12);
    machineLayout->setSpacing(10);

    m_machineControlHintLabel = new QLabel(m_machineControlPage);
    m_machineControlHintLabel->setObjectName(QStringLiteral("pageHintLabel"));
    m_machineControlHintLabel->setWordWrap(true);
    machineLayout->addWidget(m_machineControlHintLabel);

    auto *machineTopSplitter = new QSplitter(Qt::Horizontal, m_machineControlPage);
    machineTopSplitter->setHandleWidth(6);
    m_machineStatusGroup = new QGroupBox(machineTopSplitter);
    m_machineStatusGroup->setObjectName(QStringLiteral("machineStatusGroup"));
    m_machineAxesGroup = new QGroupBox(machineTopSplitter);
    m_machineAxesGroup->setObjectName(QStringLiteral("machineAxesGroup"));
    m_machineRunGroup = new QGroupBox(machineTopSplitter);
    m_machineRunGroup->setObjectName(QStringLiteral("machineRunGroup"));
    auto *machineBottom = new QGroupBox(m_machineControlPage);
    m_machineLogGroup = machineBottom;
    m_machineLogGroup->setObjectName(QStringLiteral("machineLogGroup"));

    auto addMachineField = [](QGridLayout *layout,
                              QGroupBox *group,
                              int row,
                              const QString &nameObject,
                              const QString &valueObject) {
        auto *name = new QLabel(group);
        name->setObjectName(QStringLiteral("machineFieldName"));
        name->setProperty("field", nameObject);
        auto *value = new QLabel(QStringLiteral("--"), group);
        value->setObjectName(valueObject);
        value->setProperty("machineFieldValue", true);
        layout->addWidget(name, row, 0);
        layout->addWidget(value, row, 1);
    };

    auto *statusLayout = new QGridLayout(m_machineStatusGroup);
    statusLayout->setContentsMargins(14, 18, 14, 14);
    statusLayout->setHorizontalSpacing(10);
    statusLayout->setVerticalSpacing(9);
    statusLayout->setColumnStretch(1, 1);
    addMachineField(statusLayout, m_machineStatusGroup, 0,
                    QStringLiteral("connection"), QStringLiteral("machineConnectionValue"));
    addMachineField(statusLayout, m_machineStatusGroup, 1,
                    QStringLiteral("controller"), QStringLiteral("machineControllerValue"));
    addMachineField(statusLayout, m_machineStatusGroup, 2,
                    QStringLiteral("program"), QStringLiteral("machineProgramValue"));
    addMachineField(statusLayout, m_machineStatusGroup, 3,
                    QStringLiteral("buffer"), QStringLiteral("machineBufferValue"));

    auto *axesLayout = new QGridLayout(m_machineAxesGroup);
    axesLayout->setContentsMargins(14, 18, 14, 14);
    axesLayout->setHorizontalSpacing(10);
    axesLayout->setVerticalSpacing(9);
    axesLayout->setColumnStretch(1, 1);
    const QStringList axisNames = { QStringLiteral("X"), QStringLiteral("Y"),
                                    QStringLiteral("Z"), QStringLiteral("WCS") };
    for (int row = 0; row < axisNames.size(); ++row) {
        auto *name = new QLabel(axisNames.at(row), m_machineAxesGroup);
        name->setObjectName(QStringLiteral("machineFieldName"));
        auto *value = new QLabel(row < 3 ? QStringLiteral("0.000 mm")
                                         : QStringLiteral("G54"),
                                 m_machineAxesGroup);
        value->setObjectName(QStringLiteral("machineAxisValue"));
        value->setProperty("axis", axisNames.at(row));
        axesLayout->addWidget(name, row, 0);
        axesLayout->addWidget(value, row, 1);
    }

    auto *runLayout = new QVBoxLayout(m_machineRunGroup);
    runLayout->setContentsMargins(14, 18, 14, 14);
    runLayout->setSpacing(9);
    auto *runHint = new QLabel(m_machineRunGroup);
    runHint->setObjectName(QStringLiteral("machineRunHint"));
    runHint->setWordWrap(true);
    runLayout->addWidget(runHint);
    const QStringList runActions = {
        QStringLiteral("cycleStart"), QStringLiteral("feedHold"),
        QStringLiteral("controlledStop"), QStringLiteral("reset")
    };
    for (const QString &action : runActions) {
        auto *button = new QToolButton(m_machineRunGroup);
        button->setObjectName(QStringLiteral("machineControlButton"));
        button->setProperty("action", action);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setEnabled(false);
        runLayout->addWidget(button);
    }
    runLayout->addStretch(1);

    auto *logLayout = new QVBoxLayout(m_machineLogGroup);
    logLayout->setContentsMargins(14, 18, 14, 14);
    auto *emptyState = new QLabel(m_machineLogGroup);
    emptyState->setObjectName(QStringLiteral("machineEmptyState"));
    emptyState->setWordWrap(true);
    emptyState->setAlignment(Qt::AlignCenter);
    logLayout->addWidget(emptyState, 1);

    machineLayout->addWidget(machineTopSplitter, 2);
    machineLayout->addWidget(machineBottom, 1);

    m_pageStack->addWidget(m_designPage);
    m_pageStack->addWidget(m_machiningPage);
    m_pageStack->addWidget(m_machineControlPage);

    bodyLayout->addWidget(m_pageNav);
    bodyLayout->addWidget(m_pageStack, 1);
    rootLayout->addWidget(systemHeader);
    rootLayout->addWidget(bodyShell, 1);
    setCentralWidget(central);

    connect(m_pageNav, &QListWidget::currentRowChanged,
            m_pageStack, &QStackedWidget::setCurrentIndex);
    connect(m_pageNav, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_workspaceTitleLabel || !m_pageNav || row < 0 || row >= m_pageNav->count()) {
            return;
        }
        m_workspaceTitleLabel->setText(m_pageNav->item(row)->text());
    });
    m_pageNav->setCurrentRow(0);
}

void MainWindow::createStatusBar()
{
    statusBar()->addPermanentWidget(m_bottomBar, 1);
}

void MainWindow::connectSignals()
{
    connect(&ToolLibrary::instance(), &ToolLibrary::toolLibraryChanged, this, [this]() {
        const QString toolsPath = QCoreApplication::applicationDirPath() + QStringLiteral("/tools.json");
        ToolLibrary::instance().saveToFile(toolsPath);
    });

    connect(m_actImportStep, &QAction::triggered, this, &MainWindow::onImportStep);    connect(m_actOpenProject, &QAction::triggered, this, &MainWindow::onOpenProject);
    connect(m_actSaveProject, &QAction::triggered, this, &MainWindow::onSaveProject);
    connect(m_actExportGCode, &QAction::triggered, this, &MainWindow::onExportGCode);
    connect(m_actExit, &QAction::triggered, qApp, &QApplication::quit);
    connect(m_actResetCamera, &QAction::triggered, this, &MainWindow::onResetCamera);
    connect(m_actLangZh, &QAction::triggered, this, &MainWindow::onLanguageChinese);
    connect(m_actLangEn, &QAction::triggered, this, &MainWindow::onLanguageEnglish);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::onAbout);
    connect(m_actSendToMachine, &QAction::triggered, this, &MainWindow::onSendToMachine);

    // Simulation
    connect(m_actSimPlay,  &QAction::triggered, this, &MainWindow::onSimPlay);
    connect(m_actSimPause, &QAction::triggered, this, &MainWindow::onSimPause);
    connect(m_actSimStop,  &QAction::triggered, this, &MainWindow::onSimStop);

    connect(m_actSetFrontFace, &QAction::triggered, this, &MainWindow::onSetFrontFace);
    connect(m_actSetupOrigin, &QAction::triggered, this, &MainWindow::onEditSetupOrigin);
    connect(m_actOriginFromHole, &QAction::triggered,
            this, &MainWindow::onSetOriginFromSelectedHole);
    connect(m_actStockDefinition, &QAction::triggered, this, &MainWindow::onEditStockDefinition);
    connect(m_actMachineProfile, &QAction::triggered,
            this, &MainWindow::onEditMachineProfile);
    connect(this, &MainWindow::activeRegionChanged,
            m_featurePanel, &FeatureListPanel::onActiveRegionChanged);
    connect(this, &MainWindow::activeRegionChanged,
            m_strategyPanel, &StrategyPanel::setActiveRegion);
    connect(this, &MainWindow::activeRegionChanged,
            m_operationPanel, &OperationListPanel::setActiveRegion);
    connect(this, &MainWindow::activeRegionChanged, this,
            [this](FaceRegion) { updateDesignWorkflowSummary(); });

    connect(m_simCtrl, &SimulationController::toolPathReady,
            m_simViewport, &ViewportWidget::setToolPath);
    connect(m_simCtrl, &SimulationController::toolPositionChanged,
            m_simViewport, &ViewportWidget::setToolPosition);
    connect(m_simCtrl, &SimulationController::toolDiameterChanged,
            m_simViewport, &ViewportWidget::setToolDiameter);
    connect(m_simCtrl, &SimulationController::toolModelPathChanged,
            m_simViewport, &ViewportWidget::setToolModelPath);
    connect(m_simCtrl, &SimulationController::progressChanged,
            m_bottomBar, &BottomBar::setProgress);
    connect(m_simCtrl, &SimulationController::currentGCodeLineChanged,
            m_gcodeEditor, &GCodeEditor::setCurrentExecutionLine);
    connect(m_simCtrl, &SimulationController::collisionChecked, this,
            [this](bool hasCollision, const QString &message) {
        if (hasCollision) {
            m_bottomBar->setStatus(message);
        }
    });
    connect(m_gcodeEditor, &GCodeEditor::lineSelected, this, [this](int line) {
        if (m_simCtrl->isRunning()) {
            return;
        }
        const QString gcode = m_gcodeEditor->toPlainText();
        if (gcode.trimmed().isEmpty()) {
            return;
        }
        if (m_simCtrl->toolPath().isEmpty()) {
            m_simCtrl->loadGCode(gcode);
        }
        m_simCtrl->seekToGCodeLine(line);
    });
    connect(m_operationPanel, &OperationListPanel::currentOperationChanged,
            this, &MainWindow::jumpToGeneratedOperation);
    connect(m_operationPanel, &OperationListPanel::operationsEdited,
            this, [this](const QList<MachiningOperation> &operations) {
        ProjectManager *project = AppController::instance().projectManager();
        if (project) {
            project->setOperations(operations);
        }
        syncProgramList();
        updateProgramActionAvailability();
    });
    connect(m_bottomBar, &BottomBar::simulationSpeedChanged,
            m_simCtrl, &SimulationController::setSpeed);
    connect(m_simCtrl, &SimulationController::simulationFinished, this, [this]() {
        m_simulationReviewState = SimulationReviewState::Completed;
        updateProgramReviewSummary();
        m_bottomBar->setStatus(tr("Simulation finished."));
        m_bottomBar->showProgress(false);
    });

    AppController &ctrl = AppController::instance();
    connect(&ctrl, &AppController::stepImported, this, &MainWindow::onStepImported);
    connect(&ctrl, &AppController::gcodeReady, this, &MainWindow::onGCodeReady);
    connect(&ctrl, &AppController::statusMessage, this, &MainWindow::onStatusMessage);
    connect(&ctrl, &AppController::errorOccurred, this, &MainWindow::onErrorOccurred);
#ifdef CNEXT_ENABLE_OCC
    connect(&ctrl, &AppController::featuresReclassified, this, [this]() {
        ProjectManager *project = AppController::instance().projectManager();
        if (project) {
            m_featurePanel->setFeatures(project->features());
        }
    });
#endif

    ProjectManager *project = ctrl.projectManager();
    if (project) {
        connect(project, &ProjectManager::projectChanged, this, [this]() {
            ProjectManager *pm = AppController::instance().projectManager();
            if (!pm) {
                return;
            }
            m_viewport->setMesh(pm->mesh());
            m_simViewport->setMesh(pm->mesh());
            m_featurePanel->setFeatures(pm->features());
            updateDesignWorkflowSummary();
        });
        connect(project, &ProjectManager::operationsChanged, this, [this]() {
            ProjectManager *pm = AppController::instance().projectManager();
            if (pm) {
                m_operationPanel->setOperations(pm->operations());
            }
            updateDesignWorkflowSummary();
        });
        connect(project, &ProjectManager::programsChanged, this, [this]() {
            syncProgramList();
            updateDesignWorkflowSummary();
        });
    }

    if (m_programList) {
        connect(m_programList, &QListWidget::currentRowChanged, this, [this](int row) {
            if (m_updatingProgramList || row < 0) {
                return;
            }
            QListWidgetItem *item = m_programList->item(row);
            if (!item) {
                return;
            }
            loadProgramById(item->data(Qt::UserRole).toString(), false);
        });
    }

    connect(m_gcodeEditor, &QPlainTextEdit::textChanged, this, [this]() {
        m_simulationReviewState = m_gcodeEditor->toPlainText().trimmed().isEmpty()
            ? SimulationReviewState::Unavailable
            : SimulationReviewState::Ready;
        updateProgramActionAvailability();
    });
    updateProgramActionAvailability();
    updateDesignWorkflowSummary();

    connect(m_featurePanel, &FeatureListPanel::featureSelected, this, [this](int idx) {
        ProjectManager *project = AppController::instance().projectManager();
        if (!project) {
            return;
        }
        const auto &features = project->features();
        if (idx >= 0 && idx < features.size()) {
            showFeatureInStrategyPanel(m_strategyPanel, features[idx]);
            m_viewport->setHighlightedFaces(features[idx].faceIndices);
        } else {
            clearStrategyPanelSelection(m_strategyPanel);
            m_viewport->clearHighlight();
        }
    });

    connect(m_featurePanel, &FeatureListPanel::featureGroupSelected, this, [this](const QVector<int> &indices) {
        ProjectManager *project = AppController::instance().projectManager();
        if (!project) {
            return;
        }

        const auto &features = project->features();
        QSet<int> seenFaces;
        QVector<int> faces;
        for (int index : indices) {
            if (index < 0 || index >= features.size()) {
                continue;
            }
            for (int faceIndex : features[index].faceIndices) {
                if (!seenFaces.contains(faceIndex)) {
                    seenFaces.insert(faceIndex);
                    faces.append(faceIndex);
                }
            }
        }

        if (faces.isEmpty()) {
            m_viewport->clearHighlight();
            return;
        }
        m_viewport->setHighlightedFaces(faces);
    });
    connect(m_featurePanel, &FeatureListPanel::checkedFeaturesChanged, this, [this](const QVector<int> &indices) {
        ProjectManager *project = AppController::instance().projectManager();
        if (!project) {
            return;
        }

        const auto &features = project->features();
        QSet<int> seenFaces;
        QVector<int> faces;
        const int currentIndex = m_featurePanel->currentFeatureIndex();
        int preferredIndex = -1;
        int firstHoleIndex = -1;
        int firstSlotIndex = -1;

        const QVector<int> orderedIndices =
            prioritizeFeatureIndicesByActiveRegion(indices, features, m_activeRegion);
        for (int index : orderedIndices) {
            if (index < 0 || index >= features.size()) {
                continue;
            }
            if (preferredIndex < 0 &&
                index == currentIndex &&
                activeRegionOrder(features[index].region, m_activeRegion) == 0) {
                preferredIndex = index;
            }
            if (firstHoleIndex < 0 && isHoleFeature(features[index])) {
                firstHoleIndex = index;
            } else if (firstSlotIndex < 0 && isSlotFeature(features[index])) {
                firstSlotIndex = index;
            }
            for (int faceIndex : features[index].faceIndices) {
                if (!seenFaces.contains(faceIndex)) {
                    seenFaces.insert(faceIndex);
                    faces.append(faceIndex);
                }
            }
        }

        if (preferredIndex >= 0) {
            showFeatureInStrategyPanel(m_strategyPanel, features[preferredIndex]);
            m_featurePanel->selectFeature(preferredIndex);
        } else if (firstHoleIndex >= 0) {
            showFeatureInStrategyPanel(m_strategyPanel, features[firstHoleIndex]);
            m_featurePanel->selectFeature(firstHoleIndex);
        } else if (firstSlotIndex >= 0) {
            showFeatureInStrategyPanel(m_strategyPanel, features[firstSlotIndex]);
            m_featurePanel->selectFeature(firstSlotIndex);
        } else {
            clearStrategyPanelSelection(m_strategyPanel);
        }

        if (faces.isEmpty()) {
            m_viewport->clearHighlight();
        } else {
            m_viewport->setHighlightedFaces(faces);
        }
    });

    connect(m_viewport, &ViewportWidget::facePointPicked, this, [this](int faceIndex, const QVector3D &hitPoint) {
        if (m_settingFrontFace) {
            ProjectManager *project = AppController::instance().projectManager();
            if (project && faceIndex > 0) {
                const QVector3D normal = averageFaceNormal(project->mesh(), faceIndex);
                if (normal.lengthSquared() > 1.0e-8f) {
                    if (SetupOrientation::canConfirmFaceSelection(
                            m_pendingFrontFaceIndex,
                            m_pendingFrontFaceNormal,
                            faceIndex,
                            normal)) {
                        const QQuaternion rotation =
                            SetupOrientation::combinedRotationToFront(
                                project->setupRotation(), normal);
                        QString setupError;
#ifdef CNEXT_ENABLE_OCC
                        if (!AppController::instance().reloadStepWithSetupRotation(rotation, &setupError)) {
                            onErrorOccurred(setupError.isEmpty()
                                ? tr("Failed to re-recognize features for the selected setup.")
                                : setupError);
                            return;
                        }
#else
                        const MeshData rotatedMesh = rotateMesh(project->mesh(), rotation);
                        const QVector<MachiningFeature> rotatedFeatures =
                            rotateFeatures(project->features(), rotatedMesh, rotation);

                        project->setMesh(rotatedMesh);
                        project->setFeatures(rotatedFeatures);
                        project->setOperations({});
                        project->setPrograms({});
                        project->setSetupOrigin(SetupOrigin());
                        project->setStockDefinition(StockDefinition());
#endif
                        const MeshData &setupMesh = project->mesh();
                        const QVector<MachiningFeature> &setupFeatures = project->features();

                        m_viewport->setMesh(setupMesh);
                        m_viewport->clearHighlight();
                        m_simViewport->setMesh(setupMesh);
                        m_simViewport->clearHighlight();
                        m_simViewport->clearToolPath();
                        m_featurePanel->setFeatures(setupFeatures);
                        m_activeRegion = FaceRegion::Front;
                        emit activeRegionChanged(m_activeRegion);
                        m_operationPanel->setOperations(project->operations());
                        m_simCtrl->setMesh(setupMesh);
                        m_simCtrl->loadGCode(QString());
                        m_gcodeEditor->setGCode(QString());
                        clearStrategyPanelSelection(m_strategyPanel);

                        m_settingFrontFace = false;
                        m_pendingFrontFaceIndex = -1;
                        m_pendingFrontFaceNormal = QVector3D();
                        m_actSetFrontFace->setChecked(false);
                        m_bottomBar->setStatus(tr("Front-face setup updated."));
                    } else {
                        m_pendingFrontFaceIndex = faceIndex;
                        m_pendingFrontFaceNormal = normal;
                        m_viewport->setHighlightedFaces(QVector<int>{faceIndex});
                        const QString normalStr = QStringLiteral("(%1, %2, %3)")
                            .arg(double(normal.x()), 0, 'f', 2)
                            .arg(double(normal.y()), 0, 'f', 2)
                            .arg(double(normal.z()), 0, 'f', 2);
                        m_bottomBar->setStatus(
                            tr("Click the same face again to confirm setup. Normal: %1").arg(normalStr));

                    }
                    return;
                }
            }
            m_settingFrontFace = false;
            m_pendingFrontFaceIndex = -1;
            m_pendingFrontFaceNormal = QVector3D();
            m_actSetFrontFace->setChecked(false);
            m_bottomBar->setStatus(tr("Front-face selection canceled."));
            return;
        }

        ProjectManager *project = AppController::instance().projectManager();
        if (!project || faceIndex <= 0) {
            return;
        }

        const auto &features = project->features();
        for (int i = 0; i < features.size(); ++i) {
            if (isHoleFeature(features[i]) && features[i].faceIndices.contains(faceIndex)) {
                m_featurePanel->selectFeature(i);
                showFeatureInStrategyPanel(m_strategyPanel, features[i]);
                m_viewport->setHighlightedFaces(features[i].faceIndices);
                return;
            }
        }

        for (int i = 0; i < features.size(); ++i) {
            if (features[i].faceIndices.contains(faceIndex)) {
                m_featurePanel->selectFeature(i);
                showFeatureInStrategyPanel(m_strategyPanel, features[i]);
                m_viewport->setHighlightedFaces(features[i].faceIndices);
                return;
            }
        }

        const int holeIndex = findNearestHoleFeature(features, hitPoint, FaceRegion::Unknown);
        if (holeIndex >= 0) {
            m_featurePanel->selectFeature(holeIndex);
            showFeatureInStrategyPanel(m_strategyPanel, features[holeIndex]);
            m_viewport->setHighlightedFaces(features[holeIndex].faceIndices);
            return;
        }

        m_featurePanel->selectFeature(-1);
        m_viewport->setHighlightedFaces(QVector<int>{faceIndex});
    });

    connect(m_strategyPanel, &StrategyPanel::operationProposalConfirmed,
            this, [this](const OperationProposal &proposal) {
        if (proposal.kind != OperationProposalKind::Hole) {
            return;
        }
        const HoleFeature &feature = proposal.holeFeature;
        const QString &stratId = proposal.strategyId;
        const StrategyParams &params = proposal.params;
        const ToolEntry tool = ToolLibrary::instance().tool(proposal.toolId);
        StrategyBase *s = StrategyFactory::instance().strategy(stratId).get();
        if (!s) {
            onErrorOccurred(tr("Strategy not found."));
            return;
        }

        HoleFeature selectedFeature = feature;
        if (!isHoleFeature(selectedFeature)) {
            ProjectManager *project = AppController::instance().projectManager();
            if (project) {
                const auto &features = project->features();
                QVector<int> candidates = m_featurePanel->checkedFeatureIndices();
                const int currentIndex = m_featurePanel->currentFeatureIndex();
                if (currentIndex >= 0 && !candidates.contains(currentIndex)) {
                    candidates.prepend(currentIndex);
                }
                candidates = prioritizeFeatureIndicesByActiveRegion(candidates,
                                                                    features,
                                                                    m_activeRegion);

                for (int index : candidates) {
                    if (index >= 0 && index < features.size() && isHoleFeature(features[index])) {
                        selectedFeature = features[index];
                        m_strategyPanel->setFeature(selectedFeature);
                        break;
                    }
                }
            }
        }

        QVector<HoleFeature> selectedHoles;
        QSet<int> selectedHoleFaceKeys;
        auto appendSelectedHole = [&](const MachiningFeature &candidate) {
            if (!isHoleFeature(candidate)) {
                return;
            }

            const int key = candidate.faceIndices.value(0, -1);
            if (key >= 0 && selectedHoleFaceKeys.contains(key)) {
                return;
            }
            if (key >= 0) {
                selectedHoleFaceKeys.insert(key);
            }
            selectedHoles.append(candidate);
        };

        ProjectManager *batchProject = AppController::instance().projectManager();
        if (batchProject) {
            const auto &features = batchProject->features();
            const QVector<int> orderedIndices = prioritizeFeatureIndicesByActiveRegion(
                m_featurePanel->checkedFeatureIndices(),
                features,
                m_activeRegion);
            for (int index : orderedIndices) {
                if (index >= 0 && index < features.size()) {
                    appendSelectedHole(features[index]);
                }
            }

            if (selectedHoles.isEmpty()) {
                const int currentIndex = m_featurePanel->currentFeatureIndex();
                if (currentIndex >= 0 && currentIndex < features.size()) {
                    appendSelectedHole(features[currentIndex]);
                }
            }
        }
        if (selectedHoles.isEmpty()) {
            appendSelectedHole(selectedFeature);
        }
        if (selectedHoles.size() > 1)
            selectedHoles = sortHolesByActiveRegionThenNearest(selectedHoles, m_activeRegion);
        if (!selectedHoles.isEmpty()) {
            selectedFeature = selectedHoles.first();
            m_strategyPanel->setFeature(selectedFeature);
        }

        const bool zh = isChineseUi();
        const QString selectedHoleError = isHoleFeature(selectedFeature)
            ? holeSetupRestrictionMessage(selectedFeature, zh)
            : QString();
        if (!selectedHoleError.isEmpty()) {
            onErrorOccurred(selectedHoleError);
            return;
        }

        if (!isHoleFeature(selectedFeature)) {
            onErrorOccurred(tr("Selected feature is not a hole."));
            return;
        }
        if (selectedFeature.faceIndices.isEmpty() || selectedFeature.radius <= 0.0) {
            onErrorOccurred(tr("Invalid hole geometry."));
            return;
        }
        if (selectedFeature.region == FaceRegion::Side) {
            onErrorOccurred(tr("Side holes require a dedicated setup."));
            return;
        }
        const bool batchHoles = selectedHoles.size() > 1;
        if (batchHoles && stratId != QStringLiteral("hole_spot")) {
            if (!holeFeaturesShareGroup(selectedHoles)) {
                onErrorOccurred(zh
                    ? QStringLiteral("当前批量选择包含不同孔型或尺寸。除定点钻外，请一次只勾选一个孔分类组。")
                    : QStringLiteral("The batch contains different hole types or dimensions. Except for spot drilling, select one hole-classification group at a time."));
                return;
            }
        }
        if (batchHoles || stratId == QStringLiteral("hole_spot")) {
            for (const HoleFeature &hole : selectedHoles) {
                const QString holeError = holeSetupRestrictionMessage(hole, zh);
                if (!holeError.isEmpty()) {
                    onErrorOccurred(holeError);
                    return;
                }
            }
        }
        if (batchHoles || stratId == QStringLiteral("hole_spot")) {
            for (const HoleFeature &hole : selectedHoles) {
                if (hole.faceIndices.isEmpty() || hole.radius <= 0.0) {
                    onErrorOccurred(tr("Invalid hole geometry."));
                    return;
                }
                if (hole.region == FaceRegion::Side) {
                    onErrorOccurred(tr("Side holes require a dedicated setup."));
                    return;
                }
            }
        }
        if (tool.id <= 0) {
            onErrorOccurred(tr("No valid tool selected."));
            return;
        }
        const ToolCompatibilityReport toolReport = reviewToolCompatibility(
            stratId,
            tool,
            selectedFeature,
            zh,
            params.get(QStringLiteral("depth"), selectedFeature.depth));
        if (!confirmToolCompatibility(this, toolReport, zh)) {
            m_bottomBar->setStatus(
                zh ? QStringLiteral("已取消：请重新选择刀具或调整工艺。")
                   : QStringLiteral("Canceled: select another tool or revise the process."));
            return;
        }

        QList<OperationProposal> confirmedProposals;
        for (const HoleFeature &hole : selectedHoles) {
            OperationProposal oneProposal = proposal;
            oneProposal.holeFeature = hole;
            confirmedProposals.append(oneProposal);
        }
        if (!validateSetupForProposals(confirmedProposals)) {
            m_bottomBar->setStatus(
                zh ? QStringLiteral("已取消工序确认。")
                   : QStringLiteral("Operation confirmation canceled."));
            return;
        }

        QList<MachiningOperation> confirmedOperations;
        for (const OperationProposal &oneProposal : confirmedProposals) {
            const OperationConfirmationResult confirmation =
                confirmOperationProposal(
                    oneProposal, OperationConfirmationIntent::ExplicitUser);
            if (!confirmation.ok) {
                onErrorOccurred(confirmation.error);
                return;
            }
            confirmedOperations.append(confirmation.operation);
        }

        const ToolpathResult result = batchHoles
            ? s->generate(selectedHoles, tool, params)
            : s->generate(selectedFeature, tool, params);
        if (!result.ok || result.gcode.isEmpty()) {
            onErrorOccurred(result.errorMsg.isEmpty() ? tr("G-code generation failed.") : result.errorMsg);
            return;
        }

        const QStringList addedIds =
            m_operationPanel->addConfirmedOperations(confirmedOperations);
        if (!addedIds.isEmpty()) {
            m_operationPanel->selectOperationById(addedIds.constLast());
        }
        m_bottomBar->setStatus(
            zh ? QStringLiteral("已确认 %1 道孔工序；请从“已确认工序”生成程序。")
                     .arg(addedIds.size())
               : QStringLiteral("Confirmed %1 hole operation(s). Generate the program from Confirmed Operations.")
                     .arg(addedIds.size()));
    });

    connect(m_strategyPanel, &StrategyPanel::operationProposalConfirmed,
            this, [this](const OperationProposal &proposal) {
        if (proposal.kind != OperationProposalKind::Contour) {
            return;
        }
        const ContourFeature &feature = proposal.contourFeature;
        const QString &strategyId = proposal.strategyId;
        const StrategyParams &params = proposal.params;
        const ToolEntry tool = ToolLibrary::instance().tool(proposal.toolId);
        const bool zh = isChineseUi();
        QString resolvedStrategyId = strategyId;

        StrategyBase *s = StrategyFactory::instance().strategy(resolvedStrategyId).get();
        if (!s) {
            onErrorOccurred(tr("Strategy not found."));
            return;
        }
        if (tool.id <= 0 || tool.diameter <= 0.0) {
            onErrorOccurred(tr("Tool is invalid for milling."));
            return;
        }
        ProjectManager *project = AppController::instance().projectManager();
        ContourFeature requestedFeature = applyContourOverrides(feature, params);
        if (isSlotMillingStrategy(resolvedStrategyId)) {
            logSlotContourFeature("requested-after-ui-override", requestedFeature, params);
        }
        if (project) {
            const auto &features = project->features();
            const int currentIndex = m_featurePanel->currentFeatureIndex();
            int slotIndex = -1;
            if (currentIndex >= 0 && currentIndex < features.size() && isSlotFeature(features[currentIndex])) {
                slotIndex = currentIndex;
            } else if (isSlotMillingStrategy(resolvedStrategyId)) {
                double bestDistance = std::numeric_limits<double>::max();
                const double tolerance = std::max(1.0, requestedFeature.length * 0.25);
                for (int i = 0; i < features.size(); ++i) {
                    if (!isSlotFeature(features[i])) {
                        continue;
                    }
                    const double distance = double((features[i].center - requestedFeature.center).length());
                    if (distance <= tolerance && distance < bestDistance) {
                        bestDistance = distance;
                        slotIndex = i;
                    }
                }
            }
            if (slotIndex >= 0) {
                qDebug().noquote()
                    << QStringLiteral("[slot-debug] matched feature index=%1 subtype=%2 center=(%3,%4,%5) L=%6 W=%7 D=%8 A=%9")
                          .arg(slotIndex)
                          .arg(features[slotIndex].subType)
                          .arg(features[slotIndex].center.x(), 0, 'f', 3)
                          .arg(features[slotIndex].center.y(), 0, 'f', 3)
                          .arg(features[slotIndex].center.z(), 0, 'f', 3)
                          .arg(features[slotIndex].length, 0, 'f', 3)
                          .arg(features[slotIndex].width, 0, 'f', 3)
                          .arg(features[slotIndex].depth, 0, 'f', 3)
                          .arg(features[slotIndex].angle, 0, 'f', 3);
                requestedFeature = applySlotOverridesFromParams(
                    toContourFeature(features[slotIndex], &project->mesh()),
                    features[slotIndex],
                    params);
                logSlotContourFeature("requested-after-recognized-slot", requestedFeature, params);
            }
        }
        StrategyParams generationParams = isSlotMillingStrategy(resolvedStrategyId)
                                              ? slotMachiningParamsWithoutGeometry(params)
                                              : params;
        const ToolCompatibilityReport toolReport = reviewToolCompatibility(
            resolvedStrategyId,
            tool,
            requestedFeature,
            zh,
            generationParams.get(QStringLiteral("depth"), requestedFeature.depth));
        if (!confirmToolCompatibility(this, toolReport, zh)) {
            m_bottomBar->setStatus(
                zh ? QStringLiteral("已取消：请重新选择刀具或调整工艺。")
                   : QStringLiteral("Canceled: select another tool or revise the process."));
            return;
        }
        if (isSlotMillingStrategy(resolvedStrategyId)) {
            qDebug().noquote()
                << QStringLiteral("[slot-debug] strategy-selected id=%1 requestedId=%2 subtype=%3 region=%4 featureA=%5 paramA=%6 openSide=%7")
                      .arg(resolvedStrategyId)
                      .arg(strategyId)
                      .arg(requestedFeature.subType)
                      .arg(faceRegionName(requestedFeature.region))
                      .arg(requestedFeature.angle, 0, 'f', 3)
                      .arg(generationParams.get(QStringLiteral("angle"), -1000.0), 0, 'f', 3)
                      .arg(requestedFeature.openSide, 0, 'f', 3);
        }
        const QString slotError = slotSetupRestrictionMessage(requestedFeature,
                                                              resolvedStrategyId,
                                                              tool,
                                                              generationParams,
                                                              isChineseUi());
        if (!slotError.isEmpty()) {
            onErrorOccurred(slotError);
            return;
        }
        QVector<ContourFeature> contourBatch;
        if (isSlotMillingStrategy(resolvedStrategyId)) {
            if (project) {
                const auto &features = project->features();
                const QVector<int> checkedIndices = prioritizeFeatureIndicesByActiveRegion(
                    m_featurePanel->checkedFeatureIndices(),
                    features,
                    m_activeRegion);
                QVector<MachiningFeature> selectedSlots;
                for (int index : checkedIndices) {
                    if (index >= 0 && index < features.size() && isSlotFeature(features[index])) {
                        selectedSlots.append(features[index]);
                        ContourFeature batchFeature = applySlotOverridesFromParams(
                            toContourFeature(features[index], &project->mesh()),
                            features[index],
                            params);
                        contourBatch.append(batchFeature);
                    }
                }
                if (selectedSlots.size() > 1 && !contourFeaturesShareGroup(selectedSlots)) {
                    onErrorOccurred(
                        zh ? QStringLiteral("批量槽加工包含不同类型、尺寸、深度、角度或加工面。请一次只勾选一个槽分组。")
                           : QStringLiteral("The slot batch mixes types, dimensions, depths, angles, or machining regions. Select one slot group at a time."));
                    return;
                }
            }
        }
        if (contourBatch.isEmpty()) {
            contourBatch.append(requestedFeature);
        }
        if (isManualContourChoiceStrategy(resolvedStrategyId)) {
            ContourMachiningChoiceDialog choiceDialog(this);
            choiceDialog.setChineseUi(zh);
            choiceDialog.setContext(resolvedStrategyId,
                                    contourBatch.first(),
                                    generationParams);
            const ContourFeature previewSource = contourBatch.first();
            choiceDialog.setChoiceChangedCallback(
                [this, previewSource](const ContourMachiningChoice &choice) {
                    ContourFeature preview = previewSource;
                    preview.points = contourPreviewPoints(previewSource);
                    StrategyParams previewParams;
                    applyContourMachiningChoice(preview, previewParams, choice);
                    const int cutterSide =
                        choice.compensation == ContourCompensationChoice::RightG42 ? -1 : 1;
                    m_viewport->setContourChoicePreview(preview.points,
                                                        choice.closedContour,
                                                        cutterSide);
                });
            const int choiceResult = choiceDialog.exec();
            m_viewport->clearContourChoicePreview();
            if (choiceResult != QDialog::Accepted) {
                m_bottomBar->setStatus(
                    zh ? QStringLiteral("已取消轮廓加工确认。")
                       : QStringLiteral("Contour machining confirmation canceled."));
                return;
            }
            for (ContourFeature &oneFeature : contourBatch) {
                applyContourMachiningChoice(oneFeature,
                                            generationParams,
                                            choiceDialog.choice());
            }
        }
        if (isSlotMillingStrategy(resolvedStrategyId)) {
            for (int batchIndex = 0; batchIndex < contourBatch.size(); ++batchIndex) {
                logSlotContourFeature(batchIndex == 0 ? "batch-feature-0" : "batch-feature-n",
                                      contourBatch[batchIndex],
                                      generationParams);
            }
        }

        QList<OperationProposal> confirmedProposals;
        for (const ContourFeature &oneFeature : contourBatch) {
            OperationProposal oneProposal = proposal;
            oneProposal.strategyId = resolvedStrategyId;
            oneProposal.params = generationParams;
            oneProposal.contourFeature = oneFeature;
            confirmedProposals.append(oneProposal);
        }
        for (const ContourFeature &oneFeature : contourBatch) {
            const QString oneSlotError = slotSetupRestrictionMessage(oneFeature,
                                                                     resolvedStrategyId,
                                                                     tool,
                                                                     generationParams,
                                                                     isChineseUi());
            if (!oneSlotError.isEmpty()) {
                onErrorOccurred(oneSlotError);
                return;
            }
        }
        if (!validateSetupForProposals(confirmedProposals)) {
            m_bottomBar->setStatus(
                zh ? QStringLiteral("已取消工序确认。")
                   : QStringLiteral("Operation confirmation canceled."));
            return;
        }

        QList<MachiningOperation> confirmedOperations;
        for (const OperationProposal &oneProposal : confirmedProposals) {
            const OperationConfirmationResult confirmation =
                confirmOperationProposal(
                    oneProposal, OperationConfirmationIntent::ExplicitUser);
            if (!confirmation.ok) {
                onErrorOccurred(confirmation.error);
                return;
            }
            confirmedOperations.append(confirmation.operation);
        }

        for (const ContourFeature &oneFeature : contourBatch) {
            const ToolpathResult result = s->generate(oneFeature, tool, generationParams);
            if (!result.ok || result.gcode.isEmpty()) {
                onErrorOccurred(result.errorMsg.isEmpty() ? tr("Milling failed.") : result.errorMsg);
                return;
            }
        }

        const QStringList addedIds =
            m_operationPanel->addConfirmedOperations(confirmedOperations);
        if (!addedIds.isEmpty()) {
            m_operationPanel->selectOperationById(addedIds.constLast());
        }
        m_bottomBar->setStatus(
            zh ? QStringLiteral("已确认 %1 道铣削工序；请从“已确认工序”生成程序。")
                     .arg(addedIds.size())
               : QStringLiteral("Confirmed %1 milling operation(s). Generate the program from Confirmed Operations.")
                     .arg(addedIds.size()));
    });

    connect(m_operationPanel, &OperationListPanel::generateProgramRequested,
            this, [this](const QList<MachiningOperation> &operations) {
        const bool zh = isChineseUi();
        PostProcessorBase *pp = PostProcessorRegistry::instance().get(
            Settings::instance().postProcessorId());
        if (!pp) {
            pp = PostProcessorRegistry::instance().get(QStringLiteral("siemens"));
        }
        if (!pp) {
            onErrorOccurred(tr("No post-processor available."));
            return;
        }

        for (int opIndex = 0; opIndex < operations.size(); ++opIndex) {
            const MachiningOperation &op = operations[opIndex];
            if (op.opType == OperationType::Contour) {
                const StrategyParams generationParams = isSlotMillingStrategy(op.strategyId)
                                                            ? slotMachiningParamsWithoutGeometry(op.params)
                                                            : op.params;
                const QString slotError = slotSetupRestrictionMessage(op.contourFeature,
                                                                      op.strategyId,
                                                                      ToolLibrary::instance().tool(op.toolId),
                                                                      generationParams,
                                                                      zh);
                if (!slotError.isEmpty()) {
                    onErrorOccurred(QStringLiteral("Operation %1: ").arg(opIndex + 1) + slotError);
                    return;
                }
            } else if (op.opType == OperationType::Hole) {
                const QString holeError = holeSetupRestrictionMessage(op.holeFeature, zh);
                if (!holeError.isEmpty()) {
                    onErrorOccurred(QStringLiteral("Operation %1: ").arg(opIndex + 1) + holeError);
                    return;
                }
            }
        }
        ProgramGenerationService generationService(
            [](const QString &strategyId) {
                return StrategyFactory::instance().strategy(strategyId);
            },
            [](int toolId) {
                return ToolLibrary::instance().tool(toolId);
            });

        ProjectManager *projectManager = AppController::instance().projectManager();
        if (!projectManager) {
            onErrorOccurred(tr("No active project is available for the Program Snapshot."));
            return;
        }
        if (!projectManager->setupOrigin().confirmed) {
            onErrorOccurred(zh
                ? QStringLiteral("Setup 原点尚未确认，不能生成程序。请先确认原点、偏移和 G54-G59。")
                : QStringLiteral("The Setup origin is not confirmed. Confirm the origin, offsets, and G54-G59 before generating a program."));
            return;
        }
        if (!projectManager->stockDefinition().confirmed) {
            onErrorOccurred(zh
                ? QStringLiteral("矩形毛坯尚未确认，不能生成程序。请先核对零件尺寸和六向余量。")
                : QStringLiteral("The rectangular stock is not confirmed. Review the part dimensions and all six allowances before generating a program."));
            return;
        }

        PostProcessorOptions options;
        options.programNumber = QStringLiteral("O0001");
        options.addComments = true;
        options.workOffset = currentWorkOffset();
        options.safeStartBlocks = projectManager->machineProfile().safeStartBlocks;

        const int nextProgramIndex = projectManager->programs().size() + 1;
        ProgramGenerationSnapshotOptions snapshotOptions;
        snapshotOptions.name = QStringLiteral("%1 %2")
                                   .arg(tr("Batch Program"))
                                   .arg(nextProgramIndex);
        snapshotOptions.sourceSummary = QStringLiteral("batch");
        snapshotOptions.mainProgramName =
            QStringLiteral("CNEXT_BATCH_%1").arg(nextProgramIndex);
        snapshotOptions.machineProfile = projectManager->machineProfile();

        const ProgramGenerationResult generation =
            generationService.generate(operations, *pp, options, snapshotOptions);
        if (!generation.ok) {
            const QString details = generation.errors.join(QLatin1Char('\n'));
            onErrorOccurred(
                zh ? QStringLiteral("程序生成已中止，未创建程序快照：\n\n%1").arg(details)
                   : QStringLiteral("Program generation stopped. No Program Snapshot was created:\n\n%1")
                         .arg(details));
            return;
        }

        const QString programId = projectManager->upsertProgram(generation.snapshot);
        loadProgramById(programId);
        jumpToGeneratedOperation(m_operationPanel->currentOperationNumber());
        m_bottomBar->showProgress(true);
        m_bottomBar->setStatus(
            zh ? QStringLiteral("已从 %1 道确认工序生成程序并通过安全检查。")
                     .arg(operations.size())
               : QStringLiteral("Generated and safety-checked a program from %1 confirmed operation(s).")
                     .arg(operations.size()));
    });

    connect(m_operationPanel, &OperationListPanel::applyCurrentToolRequested,
            this, [this]() {
        const ToolEntry tool = m_strategyPanel->currentTool();
        if (tool.id <= 0) {
            onErrorOccurred(tr("No valid tool selected."));
            return;
        }
        if (m_operationPanel->applyToolToSelection(tool.id)) {
            m_bottomBar->setStatus(tr("Applied current tool."));
        }
    });
}

void MainWindow::onImportStep()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Import STEP..."),
        QString(),
        tr("STEP files (*.step *.stp *.STEP *.STP);;All files (* )"));
    if (path.isEmpty()) {
        return;
    }
    m_bottomBar->setStatus(tr("Importing STEP..."));
    AppController::instance().importStep(path);
}

void MainWindow::onSaveProject()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Save Project"),
        QString(),
        tr("CNEXT project (*.cnext)"));
    if (path.isEmpty()) {
        return;
    }
    ProjectManager *project = AppController::instance().projectManager();
    if (project) {
        project->setOperations(m_operationPanel->operations());
        project->setActiveRegion(m_activeRegion);
        project->setWorkOffset(currentWorkOffset());
        syncCurrentProgramSnapshot();
    }
    AppController::instance().saveProject(path);
}

void MainWindow::onOpenProject()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open Project"),
        QString(),
        tr("CNEXT project (*.cnext)"));
    if (!path.isEmpty()) {
        AppController &controller = AppController::instance();
        bool loaded = controller.loadProject(path);
        if (!loaded
            && (controller.lastProjectLoadIssue() == ProjectLoadIssue::SourceMissing
                || controller.lastProjectLoadIssue() == ProjectLoadIssue::SourceChanged)) {
            const QString replacementSource = QFileDialog::getOpenFileName(
                this,
                tr("Relink Source STEP"),
                QString(),
                tr("STEP files (*.step *.stp *.STEP *.STP);;All files (*)"));
            if (replacementSource.isEmpty()) {
                return;
            }
            loaded = controller.loadProject(path, replacementSource);
        }
        if (!loaded) {
            return;
        }
        ProjectManager *project = controller.projectManager();
        if (project) {
            m_viewport->setMesh(project->mesh());
            m_viewport->clearHighlight();
            m_simViewport->setMesh(project->mesh());
            m_simViewport->clearHighlight();
            m_featurePanel->setFeatures(project->features());
            m_operationPanel->setOperations(project->operations());
            m_simCtrl->setMesh(project->mesh());
            m_activeRegion = project->activeRegion();
            emit activeRegionChanged(m_activeRegion);
            if (m_wcsCombo) {
                const QSignalBlocker blocker(m_wcsCombo);
                const int wcsIndex = m_wcsCombo->findData(project->workOffset());
                m_wcsCombo->setCurrentIndex(wcsIndex >= 0 ? wcsIndex : 0);
            }
            syncProgramList();
            const QString programId = !project->currentProgramId().trimmed().isEmpty()
                    ? project->currentProgramId()
                    : (!project->programs().isEmpty() ? project->programs().last().id : QString());
            if (!programId.isEmpty()) {
                loadProgramById(programId);
            } else {
                m_currentProgramId.clear();
                m_gcodeEditor->setGCode(QString());
                m_simCtrl->loadGCode(QString());
            }
        }
    }
}

void MainWindow::onExportGCode()
{
    syncCurrentProgramSnapshot();
    if (!validateCurrentGCodeForOutput(tr("export"))) {
        return;
    }

    ProjectManager *project = AppController::instance().projectManager();
    const ProgramEntry program = project && !m_currentProgramId.trimmed().isEmpty()
                                     ? project->programById(m_currentProgramId)
                                     : ProgramEntry();
    if (!program.packageFiles.isEmpty()) {
        const QString directory = QFileDialog::getExistingDirectory(
            this,
            tr("Export Program Package..."));
        if (directory.isEmpty()) {
            return;
        }
        const ProgramPackageExportReport report = ProgramPackageExporter::exportFiles(
            directory,
            program.mainProgramFileName,
            program.packageFiles);
        if (!report.ok) {
            QMessageBox::warning(this,
                                 tr("Program Package export failed"),
                                 report.error);
            return;
        }
        m_bottomBar->setStatus(
            tr("Saved %1 Program Package files to %2")
                .arg(report.writtenFiles.size())
                .arg(directory));
        return;
    }

    const QString id = Settings::instance().postProcessorId();
    PostProcessorBase *pp = PostProcessorRegistry::instance().get(id);
    const QString ext = pp ? pp->fileExtension() : QStringLiteral(".nc");
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Export G-code..."),
        QString(),
        tr("G-code files (*%1);;All files (*)").arg(ext));
    if (path.isEmpty()) {
        return;
    }

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << m_gcodeEditor->toPlainText();
        m_bottomBar->setStatus(tr("Saved G-code to %1").arg(path));
    }
}

void MainWindow::onSendToMachine()
{
    syncCurrentProgramSnapshot();
    if (!validateCurrentGCodeForOutput(tr("send to machine"))) {
        return;
    }
    CncSendDialog dlg(m_gcodeEditor->toPlainText(), this);
    dlg.exec();
}

void MainWindow::onResetCamera()
{
    m_viewport->resetCamera();
    m_simViewport->resetCamera();
}

void MainWindow::onSimPlay()
{
    const QString gcode = m_gcodeEditor->toPlainText();
    if (m_simCtrl->toolPath().isEmpty() && !gcode.isEmpty()) {
        m_simCtrl->loadGCode(gcode);
        m_bottomBar->showProgress(true);
    }
    m_simCtrl->play();
    m_simulationReviewState = SimulationReviewState::Running;
    updateProgramReviewSummary();
    m_bottomBar->setStatus(tr("Simulation playing."));
}

void MainWindow::onSimPause()
{
    m_simCtrl->pause();
    m_simulationReviewState = SimulationReviewState::Paused;
    updateProgramReviewSummary();
    m_bottomBar->setStatus(tr("Simulation paused."));
}

void MainWindow::onSimStop()
{
    m_simCtrl->stop();
    m_simViewport->clearToolPath();
    m_simulationReviewState = m_gcodeEditor->toPlainText().trimmed().isEmpty()
        ? SimulationReviewState::Unavailable
        : SimulationReviewState::Stopped;
    updateProgramReviewSummary();
    m_bottomBar->showProgress(false);
    m_bottomBar->setStatus(tr("Simulation stopped."));
}

void MainWindow::onSetFrontFace(bool checked)
{
    m_settingFrontFace = checked;
    m_pendingFrontFaceIndex = -1;
    m_pendingFrontFaceNormal = QVector3D();
    if (m_actSetFrontFace->isChecked() != checked) {
        m_actSetFrontFace->setChecked(checked);
    }
    if (m_settingFrontFace) {
        m_bottomBar->setStatus(tr("Setting front face..."));
    } else {
        m_viewport->clearHighlight();
        m_bottomBar->setStatus(tr("Front face selection canceled."));
    }
}

void MainWindow::onEditSetupOrigin()
{
    ProjectManager *project = AppController::instance().projectManager();
    const bool zh = isChineseUi();
    if (!project || project->mesh().isEmpty()) {
        QMessageBox::warning(this,
                             zh ? QStringLiteral("无法设置原点")
                                : QStringLiteral("Cannot Set Origin"),
                             zh ? QStringLiteral("请先导入 STEP 模型。")
                                : QStringLiteral("Import a STEP model first."));
        return;
    }
    if (m_activeRegion == FaceRegion::Unknown) {
        QMessageBox::warning(this,
                             zh ? QStringLiteral("需要先设置正面")
                                : QStringLiteral("Front Face Required"),
                             zh ? QStringLiteral("请先人工选择并确认加工正面，再设置工件原点。")
                                : QStringLiteral("Select and confirm the machining front face before setting the work origin."));
        return;
    }

    SetupOriginDialog dialog(zh, this);
    dialog.setModelBounds(project->mesh().bbMin, project->mesh().bbMax);
    dialog.setOrigin(project->setupOrigin());
    dialog.setWorkOffset(project->workOffset());
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString previousFingerprint = project->setupFingerprint();
    const QString workOffset = dialog.workOffset();
    project->setWorkOffset(workOffset);
    project->setSetupOrigin(dialog.origin());
    if (m_wcsCombo) {
        const QSignalBlocker blocker(m_wcsCombo);
        const int index = m_wcsCombo->findData(workOffset);
        m_wcsCombo->setCurrentIndex(index >= 0 ? index : 0);
    }

    const QVector3D point = project->setupOrigin().resolvedPoint(
        project->mesh().bbMin, project->mesh().bbMax);
    const bool programsStale = !project->programs().isEmpty()
                               && previousFingerprint != project->setupFingerprint();
    updateDesignWorkflowSummary();
    syncProgramList();
    updateProgramActionAvailability();
    m_bottomBar->setStatus(
        (zh ? QStringLiteral("Setup 原点已确认：%1  X%2 Y%3 Z%4 mm%5")
            : QStringLiteral("Setup origin confirmed: %1  X%2 Y%3 Z%4 mm%5"))
            .arg(workOffset)
            .arg(double(point.x()), 0, 'f', 3)
            .arg(double(point.y()), 0, 'f', 3)
            .arg(double(point.z()), 0, 'f', 3)
            .arg(programsStale
                     ? (zh ? QStringLiteral("；已有程序已标记过期")
                           : QStringLiteral("; existing programs are now stale"))
                     : QString()));
}

void MainWindow::onSetOriginFromSelectedHole()
{
    ProjectManager *project = AppController::instance().projectManager();
    const bool zh = isChineseUi();
    const int featureIndex = m_featurePanel ? m_featurePanel->currentFeatureIndex() : -1;
    if (!project || featureIndex < 0 || featureIndex >= project->features().size()
        || !isHoleFeature(project->features()[featureIndex])) {
        QMessageBox::warning(
            this,
            zh ? QStringLiteral("未选择孔") : QStringLiteral("No Hole Selected"),
            zh ? QStringLiteral("请在识别特征列表中选中一个具体孔，而不是孔分组。")
               : QStringLiteral("Select one specific hole in the recognized-feature list, not a hole group."));
        return;
    }
    const MachiningFeature &hole = project->features()[featureIndex];
    if (m_activeRegion == FaceRegion::Unknown
        || SetupOrientation::requiresActiveRegionConfirmation(m_activeRegion, hole.region)) {
        QMessageBox::warning(
            this,
            zh ? QStringLiteral("孔不属于当前 Setup") : QStringLiteral("Hole Outside Current Setup"),
            zh ? QStringLiteral("所选孔必须属于已确认的当前加工正面。")
               : QStringLiteral("The selected hole must belong to the confirmed active machining face."));
        return;
    }

    const QVector3D currentOrigin = project->setupOrigin().confirmed
        ? project->setupOrigin().resolvedPoint(project->mesh().bbMin, project->mesh().bbMax)
        : QVector3D(0.0f, 0.0f, project->mesh().bbMax.z());
    const QVector3D holeOrigin(hole.center.x(), hole.center.y(), currentOrigin.z());
    const QString question = (zh
        ? QStringLiteral("将工件原点的 X/Y 设置到所选孔中心？\n\nX %1   Y %2   Z 保持 %3 mm\n\n确认后已有程序可能变为过期。")
        : QStringLiteral("Set the work-origin X/Y to the selected hole center?\n\nX %1   Y %2   Z remains %3 mm\n\nExisting programs may become stale after confirmation."))
        .arg(double(holeOrigin.x()), 0, 'f', 3)
        .arg(double(holeOrigin.y()), 0, 'f', 3)
        .arg(double(holeOrigin.z()), 0, 'f', 3);
    if (QMessageBox::question(
            this,
            zh ? QStringLiteral("确认孔心原点") : QStringLiteral("Confirm Hole-Center Origin"),
            question,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    const QString previousFingerprint = project->setupFingerprint();
    SetupOrigin origin;
    origin.anchor = SetupOriginAnchor::CustomPoint;
    origin.customPoint = holeOrigin;
    origin.confirmed = true;
    project->setSetupOrigin(origin);
    const bool programsStale = !project->programs().isEmpty()
                               && previousFingerprint != project->setupFingerprint();
    updateDesignWorkflowSummary();
    syncProgramList();
    updateProgramActionAvailability();
    m_bottomBar->setStatus(
        (zh ? QStringLiteral("已用孔 #%1 设置原点：X%2 Y%3，Z 保持 %4 mm%5")
            : QStringLiteral("Origin set from hole #%1: X%2 Y%3, Z remains %4 mm%5"))
            .arg(featureIndex + 1)
            .arg(double(holeOrigin.x()), 0, 'f', 3)
            .arg(double(holeOrigin.y()), 0, 'f', 3)
            .arg(double(holeOrigin.z()), 0, 'f', 3)
            .arg(programsStale
                     ? (zh ? QStringLiteral("；已有程序已标记过期")
                           : QStringLiteral("; existing programs are now stale"))
                     : QString()));
}

void MainWindow::onEditStockDefinition()
{
    ProjectManager *project = AppController::instance().projectManager();
    const bool zh = isChineseUi();
    if (!project || project->mesh().isEmpty()) {
        QMessageBox::warning(this,
                             zh ? QStringLiteral("无法设置毛坯")
                                : QStringLiteral("Cannot Set Stock"),
                             zh ? QStringLiteral("请先导入 STEP 模型。")
                                : QStringLiteral("Import a STEP model first."));
        return;
    }
    if (m_activeRegion == FaceRegion::Unknown || !project->setupOrigin().confirmed) {
        QMessageBox::warning(this,
                             zh ? QStringLiteral("Setup 尚未完成")
                                : QStringLiteral("Setup Incomplete"),
                             zh ? QStringLiteral("请先确认加工正面和工件原点，再设置毛坯。")
                                : QStringLiteral("Confirm the machining front face and work origin before setting stock."));
        return;
    }

    StockDefinitionDialog dialog(zh, this);
    dialog.setPartBounds(project->mesh().bbMin, project->mesh().bbMax);
    dialog.setStockDefinition(project->stockDefinition());
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString previousFingerprint = project->setupFingerprint();
    project->setStockDefinition(dialog.stockDefinition());
    const StockBounds bounds = project->stockDefinition().resolvedBounds(
        project->mesh().bbMin, project->mesh().bbMax);
    const QVector3D size = bounds.size();
    const bool programsStale = !project->programs().isEmpty()
                               && previousFingerprint != project->setupFingerprint();
    updateDesignWorkflowSummary();
    syncProgramList();
    updateProgramActionAvailability();
    m_bottomBar->setStatus(
        (zh ? QStringLiteral("矩形毛坯已确认：%1 × %2 × %3 mm%4")
            : QStringLiteral("Rectangular stock confirmed: %1 × %2 × %3 mm%4"))
            .arg(double(size.x()), 0, 'f', 3)
            .arg(double(size.y()), 0, 'f', 3)
            .arg(double(size.z()), 0, 'f', 3)
            .arg(programsStale
                     ? (zh ? QStringLiteral("；已有程序已标记过期")
                           : QStringLiteral("; existing programs are now stale"))
                     : QString()));
}

void MainWindow::onEditMachineProfile()
{
    ProjectManager *project = AppController::instance().projectManager();
    if (!project) {
        return;
    }

    MachineProfileDialog dialog(this);
    dialog.setProfile(project->machineProfile());
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    project->setMachineProfile(dialog.profile());
    m_bottomBar->setStatus(tr("Machine Profile updated."));
}

void MainWindow::onLanguageChinese()
{
    switchLanguage(QStringLiteral("zh_CN"));
}

void MainWindow::onLanguageEnglish()
{
    switchLanguage(QStringLiteral("en_US"));
}

void MainWindow::onAbout()
{
    QMessageBox::about(
        this,
        tr("About CNEXT-CAM"),
        tr("<b>CNEXT-CAM</b><br>"
           "CAM application for STEP import, feature recognition, toolpath generation and simulation.<br><br>"
           "Version 0.1.0<br>"
           "Qt %1").arg(QT_VERSION_STR));
}

void MainWindow::onStepImported(const QString &filePath)
{
    ProjectManager *project = AppController::instance().projectManager();
    if (!project) {
        onErrorOccurred(tr("Project manager is unavailable."));
        return;
    }

    m_viewport->setMesh(project->mesh());
    m_viewport->clearHighlight();
    m_simViewport->setMesh(project->mesh());
    m_simViewport->clearHighlight();
    m_simViewport->clearToolPath();
    m_simCtrl->setMesh(project->mesh());
    m_activeRegion = project->activeRegion();
    emit activeRegionChanged(m_activeRegion);
    m_featurePanel->setFeatures(project->features());
    m_operationPanel->setOperations(project->operations());
    m_currentProgramId.clear();
    project->setCurrentProgramId(QString());
    m_gcodeEditor->setGCode(QString());
    m_simCtrl->loadGCode(QString());
    syncProgramList();
    m_bottomBar->setStatus(tr("Imported STEP: %1").arg(filePath));
}

void MainWindow::jumpToGeneratedOperation(int operationNumber)
{
    if (operationNumber <= 0 || !m_gcodeEditor || !m_operationPanel) {
        return;
    }

    const QString operationId = m_operationPanel->currentOperationId();
    const QString targetProgramId = findProgramIdForOperation(operationId);
    if (!targetProgramId.isEmpty() && targetProgramId != m_currentProgramId) {
        loadProgramById(targetProgramId);
    }

    const QString gcode = m_gcodeEditor->toPlainText();
    const int line = findOperationLine(gcode, operationId, operationNumber);
    if (line < 0) {
        return;
    }

    if (m_simCtrl && !m_simCtrl->toolPath().isEmpty()) {
        m_simCtrl->seekToGCodeLine(line);
    }
    m_gcodeEditor->setCurrentExecutionLine(line);
}

void MainWindow::onGCodeReady(const QString &gcode)
{
    appendProgramSnapshot(tr("Program"), gcode, QStringLiteral("external"));
    m_bottomBar->showProgress(true);
    m_bottomBar->setStatus(tr("G-code loaded."));
}

void MainWindow::onStatusMessage(const QString &msg)
{
    m_bottomBar->setStatus(msg);
}

void MainWindow::onErrorOccurred(const QString &msg)
{
    m_bottomBar->setStatus(tr("Error: %1").arg(msg));
    QMessageBox::warning(this, tr("Error"), msg);
}

void MainWindow::switchLanguage(const QString &lang)
{
    Settings::instance().setLanguage(lang);
    Settings::instance().save();

    qApp->removeTranslator(&m_appTranslator);
    qApp->removeTranslator(&m_qtTranslator);

    if (m_qtTranslator.load(QStringLiteral("qt_") + lang,
                            QLibraryInfo::location(QLibraryInfo::TranslationsPath))) {
        qApp->installTranslator(&m_qtTranslator);
    }
    if (m_appTranslator.load(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations/") + lang)) ||
        m_appTranslator.load(QDir(QCoreApplication::applicationDirPath()).filePath(lang)) ||
        m_appTranslator.load(QStringLiteral(":/translations/") + lang)) {
        qApp->installTranslator(&m_appTranslator);
    }

    retranslateUi();
}

void MainWindow::retranslateUi()
{
    const bool zh = isChineseUi();
    setWindowTitle(zh ? QStringLiteral("CNEXT-CAM - 加工中心")
                      : QStringLiteral("CNEXT-CAM - CAM Center"));

    if (m_fileMenu) m_fileMenu->setTitle(zh ? QStringLiteral("文件") : QStringLiteral("File"));
    if (m_viewMenu) m_viewMenu->setTitle(zh ? QStringLiteral("视图") : QStringLiteral("View"));
    if (m_langMenu) m_langMenu->setTitle(zh ? QStringLiteral("语言") : QStringLiteral("Language"));
    if (m_helpMenu) m_helpMenu->setTitle(zh ? QStringLiteral("帮助") : QStringLiteral("Help"));
    if (m_mainToolBar) m_mainToolBar->setWindowTitle(zh ? QStringLiteral("主工具栏")
                                                        : QStringLiteral("Main Toolbar"));

    if (m_productTitleLabel) m_productTitleLabel->setText(QStringLiteral("CNEXT-CAM"));
    if (m_cq8ConnectionBadge) {
        m_cq8ConnectionBadge->setText(zh ? QStringLiteral("CQ8 未连接")
                                         : QStringLiteral("CQ8 Offline"));
    }
    if (m_machineModeBadge) {
        m_machineModeBadge->setText(zh ? QStringLiteral("模式：待机")
                                       : QStringLiteral("Mode: Standby"));
    }
    if (m_activeTaskBadge) {
        m_activeTaskBadge->setText(zh ? QStringLiteral("任务：未装载")
                                      : QStringLiteral("Task: None"));
    }
    if (m_safetyStateBadge) {
        m_safetyStateBadge->setText(zh ? QStringLiteral("安全：待配置")
                                       : QStringLiteral("Safety: Unconfigured"));
    }

    const QList<QLabel*> toolbarLabels = m_mainToolBar
        ? m_mainToolBar->findChildren<QLabel*>(QStringLiteral("toolbarFieldLabel"))
        : QList<QLabel*>();
    for (QLabel *label : toolbarLabels) {
        const QString field = label->property("field").toString();
        label->setText(field == QStringLiteral("post")
            ? (zh ? QStringLiteral("后处理") : QStringLiteral("Post"))
            : (zh ? QStringLiteral("坐标系") : QStringLiteral("WCS")));
    }

    if (m_actImportStep) m_actImportStep->setText(zh ? QStringLiteral("导入 STEP...")
                                                     : QStringLiteral("Import STEP..."));
    if (m_actOpenProject) m_actOpenProject->setText(zh ? QStringLiteral("打开项目...")
                                                       : QStringLiteral("Open Project..."));
    if (m_actSaveProject) m_actSaveProject->setText(zh ? QStringLiteral("保存项目")
                                                       : QStringLiteral("Save Project"));
    if (m_actExportGCode) m_actExportGCode->setText(zh ? QStringLiteral("导出程序...")
                                                       : QStringLiteral("Export Program..."));
    if (m_actSendToMachine) m_actSendToMachine->setText(zh ? QStringLiteral("发送到机床...")
                                                           : QStringLiteral("Send to Machine..."));
    if (m_actExit) m_actExit->setText(zh ? QStringLiteral("退出") : QStringLiteral("Exit"));
    if (m_actResetCamera) m_actResetCamera->setText(zh ? QStringLiteral("重置视角")
                                                       : QStringLiteral("Reset Camera"));
    if (m_actLangZh) {
        m_actLangZh->setText(zh ? QStringLiteral("中文") : QStringLiteral("Chinese"));
        m_actLangZh->setChecked(zh);
    }
    if (m_actLangEn) {
        m_actLangEn->setText(zh ? QStringLiteral("英文") : QStringLiteral("English"));
        m_actLangEn->setChecked(!zh);
    }
    if (m_actAbout) m_actAbout->setText(zh ? QStringLiteral("关于") : QStringLiteral("About"));

    if (m_actSimPlay)  m_actSimPlay->setText(zh ? QStringLiteral("播放") : QStringLiteral("Play"));
    if (m_actSimPause) m_actSimPause->setText(zh ? QStringLiteral("暂停") : QStringLiteral("Pause"));
    if (m_actSimStop)  m_actSimStop->setText(zh ? QStringLiteral("停止") : QStringLiteral("Stop"));
    if (m_actSetFrontFace) m_actSetFrontFace->setText(zh ? QStringLiteral("设置正面")
                                                         : QStringLiteral("Set Front Face"));
    if (m_actSetupOrigin) m_actSetupOrigin->setText(zh ? QStringLiteral("设置原点")
                                                       : QStringLiteral("Set Origin"));
    if (m_actOriginFromHole) m_actOriginFromHole->setText(
        zh ? QStringLiteral("孔心原点") : QStringLiteral("Hole-Center Origin"));
    if (m_actStockDefinition) m_actStockDefinition->setText(
        zh ? QStringLiteral("零件/毛坯") : QStringLiteral("Part / Stock"));
    if (m_wcsCombo) {
        m_wcsCombo->setToolTip(zh ? QStringLiteral("工件坐标系 G54-G59")
                                  : QStringLiteral("Work coordinate system G54-G59"));
    }
    if (m_pageNav) {
        if (m_pageNav->count() > 0) m_pageNav->item(0)->setText(zh ? QStringLiteral("工艺设计")
                                                                   : QStringLiteral("Process Design"));
        if (m_pageNav->count() > 1) m_pageNav->item(1)->setText(zh ? QStringLiteral("程序验证")
                                                                   : QStringLiteral("Program Validation"));
        if (m_pageNav->count() > 2) m_pageNav->item(2)->setText(zh ? QStringLiteral("机床运行")
                                                                   : QStringLiteral("Machine Operation"));
        if (m_workspaceTitleLabel && m_pageNav->currentRow() >= 0) {
            m_workspaceTitleLabel->setText(m_pageNav->currentItem()->text());
        }
    }
    if (m_machiningActionsGroup) {
        m_machiningActionsGroup->setTitle(zh ? QStringLiteral("程序快照")
                                             : QStringLiteral("Program Snapshots"));
    }
    if (m_machiningHintLabel) {
        m_machiningHintLabel->setText(
            zh ? QStringLiteral("选择一个不可变程序快照，并按上方状态依次完成校验。")
               : QStringLiteral("Select an immutable program snapshot and complete the checks shown above."));
    }
    if (m_programEmptyLabel) {
        m_programEmptyLabel->setText(
            zh ? QStringLiteral("还没有程序快照。\n请先在“工艺设计”中确认工序并生成程序。")
               : QStringLiteral("No program snapshot yet.\nConfirm operations in Process Design, then generate a program."));
    }
    if (m_simulationPanel) {
        m_simulationPanel->setTitle(zh ? QStringLiteral("最终代码仿真")
                                       : QStringLiteral("Final-code Simulation"));
    }
    if (m_finalProgramPanel) {
        m_finalProgramPanel->setTitle(zh ? QStringLiteral("最终 CQ8 程序")
                                         : QStringLiteral("Final CQ8 Program"));
        if (QLabel *label = m_finalProgramPanel->findChild<QLabel*>(
                QStringLiteral("macroLibraryLabel"))) {
            label->setText(zh ? QStringLiteral("CQ8 宏库（只读）")
                              : QStringLiteral("CQ8 Macro Library (read-only)"));
        }
    }
    if (m_macroLibraryEditor) {
        m_macroLibraryEditor->setPlaceholderText(
            zh ? QStringLiteral("当前程序没有关联的 CQ8 宏库。")
               : QStringLiteral("No CQ8 macro library is associated with this program."));
    }
    if (m_machiningPage) {
        const QList<QLabel*> contextHints =
            m_machiningPage->findChildren<QLabel*>(QStringLiteral("panelContextHint"));
        for (QLabel *hint : contextHints) {
            if (hint->property("context").toString() == QStringLiteral("simulation")) {
                hint->setText(zh ? QStringLiteral("仿真只解析当前最终程序；程序改变后需重新运行。")
                                 : QStringLiteral("Simulation parses the current final program; rerun it after any program change."));
            } else {
                hint->setText(zh ? QStringLiteral("这是导出和下发使用的最终控制器代码，编辑后会重新校验。")
                                 : QStringLiteral("This is the final controller code used for export and delivery; edits trigger validation again."));
            }
        }
    }
    if (m_machineStatusGroup) {
        m_machineStatusGroup->setTitle(zh ? QStringLiteral("连接与执行")
                                          : QStringLiteral("Connection & Execution"));
    }
    if (m_machineAxesGroup) {
        m_machineAxesGroup->setTitle(zh ? QStringLiteral("坐标区")
                                        : QStringLiteral("Axes"));
    }
    if (m_machineRunGroup) {
        m_machineRunGroup->setTitle(zh ? QStringLiteral("运行请求")
                                       : QStringLiteral("Run Requests"));
    }
    if (m_machineLogGroup) {
        m_machineLogGroup->setTitle(zh ? QStringLiteral("日志与报警区")
                                       : QStringLiteral("Logs and Alarms"));
    }
    if (m_machineControlHintLabel) {
        m_machineControlHintLabel->setText(
            zh ? QStringLiteral("这是独立 PC 软件的 CQ8 在线控制工作区。当前先完成状态、坐标、缓冲与运行请求界面；CQ8 通信和运动请求在接口验收前保持禁用。")
               : QStringLiteral("This is the standalone PC application's CQ8 workspace. Status, axes, buffers, and run requests are staged here; communication and motion requests remain disabled until interface acceptance."));
    }

    const QList<QLabel*> machineFieldLabels = m_machineControlPage
        ? m_machineControlPage->findChildren<QLabel*>(QStringLiteral("machineFieldName"))
        : QList<QLabel*>();
    for (QLabel *label : machineFieldLabels) {
        const QString field = label->property("field").toString();
        if (field == QStringLiteral("connection"))
            label->setText(zh ? QStringLiteral("CQ8 连接") : QStringLiteral("CQ8 connection"));
        else if (field == QStringLiteral("controller"))
            label->setText(zh ? QStringLiteral("控制器状态") : QStringLiteral("Controller state"));
        else if (field == QStringLiteral("program"))
            label->setText(zh ? QStringLiteral("执行程序") : QStringLiteral("Active program"));
        else if (field == QStringLiteral("buffer"))
            label->setText(zh ? QStringLiteral("缓冲状态") : QStringLiteral("Buffer state"));
    }
    auto setMachineValue = [this](const QString &objectName,
                                  const QString &zhText,
                                  const QString &enText) {
        if (!m_machineControlPage) return;
        if (QLabel *label = m_machineControlPage->findChild<QLabel*>(objectName)) {
            label->setText(isChineseUi() ? zhText : enText);
        }
    };
    setMachineValue(QStringLiteral("machineConnectionValue"),
                    QStringLiteral("未连接"), QStringLiteral("Offline"));
    setMachineValue(QStringLiteral("machineControllerValue"),
                    QStringLiteral("等待 CQ8 接口"), QStringLiteral("Awaiting CQ8 interface"));
    setMachineValue(QStringLiteral("machineProgramValue"),
                    QStringLiteral("未装载"), QStringLiteral("None"));
    setMachineValue(QStringLiteral("machineBufferValue"),
                    QStringLiteral("接收 -- / 轨迹 --"), QStringLiteral("RX -- / Path --"));
    if (m_machineControlPage) {
        if (QLabel *hint = m_machineControlPage->findChild<QLabel*>(QStringLiteral("machineRunHint"))) {
            hint->setText(zh ? QStringLiteral("运行按钮将在 CQ8 状态、实体面板和安全链完成联调后启用。所有 PC 指令均为请求，由 CQ8 最终裁决。")
                             : QStringLiteral("Run requests remain disabled until CQ8 state, the physical panel, and the safety chain are commissioned. CQ8 makes the final decision."));
            hint->setWordWrap(true);
        }
        const QList<QToolButton*> machineButtons =
            m_machineControlPage->findChildren<QToolButton*>(QStringLiteral("machineControlButton"));
        for (QToolButton *button : machineButtons) {
            const QString action = button->property("action").toString();
            if (action == QStringLiteral("cycleStart"))
                button->setText(zh ? QStringLiteral("循环启动（未启用）") : QStringLiteral("Cycle Start (Disabled)"));
            else if (action == QStringLiteral("feedHold"))
                button->setText(zh ? QStringLiteral("进给保持（未启用）") : QStringLiteral("Feed Hold (Disabled)"));
            else if (action == QStringLiteral("controlledStop"))
                button->setText(zh ? QStringLiteral("受控停止（未启用）") : QStringLiteral("Controlled Stop (Disabled)"));
            else if (action == QStringLiteral("reset"))
                button->setText(zh ? QStringLiteral("复位请求（未启用）") : QStringLiteral("Reset Request (Disabled)"));
        }
        if (QLabel *empty = m_machineControlPage->findChild<QLabel*>(QStringLiteral("machineEmptyState"))) {
            empty->setText(zh ? QStringLiteral("暂无 CQ8 事件或报警。\n连接建立后，这里按时间显示状态变化、报警原因和恢复条件。")
                              : QStringLiteral("No CQ8 events or alarms.\nAfter connection, state changes, alarm causes, and recovery conditions appear here."));
        }
    }

    if (m_featureDock) m_featureDock->setWindowTitle(zh ? QStringLiteral("特征列表")
                                                        : QStringLiteral("Features"));
    if (m_strategyDock) m_strategyDock->setWindowTitle(zh ? QStringLiteral("加工策略")
                                                          : QStringLiteral("Machining Strategy"));
    if (m_toolDock) m_toolDock->setWindowTitle(zh ? QStringLiteral("刀具库")
                                                  : QStringLiteral("Tool Library"));
    if (m_operationDock) m_operationDock->setWindowTitle(zh ? QStringLiteral("工序列表")
                                                            : QStringLiteral("Operation List"));

    if (m_featurePanel) m_featurePanel->retranslateUi();
    if (m_strategyPanel) m_strategyPanel->retranslateUi();
    if (m_toolPanel) m_toolPanel->retranslateUi();
    if (m_operationPanel) m_operationPanel->retranslateUi();
    updateDesignWorkflowSummary();
    updateProgramReviewSummary();
}
