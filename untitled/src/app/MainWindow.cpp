#include <QSaveFile>
#include "../workspaces/shared/ui/OperationMessageBox.h"
#include "../workspaces/process_design/ui/SketchEditorDialog.h"
#include "../workspaces/process_design/operations/MachiningInput.h"
#include "../workspaces/process_design/ui/OperationEditDialog.h"
#include "../workspaces/process_design/ui/OperationBatchEditDialog.h"
#include "../workspaces/program_verification/ui/OperationPathVisibility.h"
#include "MainWindow.h"
#include "../workspaces/process_design/selection/PocketOpening.h"
#include "../workspaces/process_design/selection/SlopedFloorOperation.h"
#include "../workspaces/process_design/ui/SlopedFloorPreview.h"
#include "../strategies/mill/ContourSideGeometry.h"
#include "../core/MachiningCoordinates.h"
#include "../core/SketchSelectionBinding.h"
#include "../core/SketchContextFactory.h"
#include "MillingBatchCommit.h"
#include "controllers/AppWorkspaceRouter.h"
#include "controllers/ProjectCommandController.h"

#include "../workspaces/shared/ui/BottomBar.h"
#include "../workspaces/machine_operation/ui/CncSendDialog.h"
#include "../workspaces/process_design/ui/ContourFeatureGrouping.h"
#include "../workspaces/process_design/ui/ContourMachiningChoiceDialog.h"
#include "../workspaces/process_design/ui/SelectionChainDialog.h"
#include "../workspaces/process_design/selection/CoplanarSelection.h"
#include <QUuid>
#include "../workspaces/process_design/selection/MachiningPlaneBoundaryFilter.h"
#include "../workspaces/process_design/ui/FeatureListPanel.h"
#include "../workspaces/program_verification/ui/GCodeEditor.h"
#include "../workspaces/process_design/ui/HoleFeatureGrouping.h"
#include "../workspaces/process_design/ui/HoleSelectionDialog.h"
#include "../workspaces/process_design/ui/AutoHoleDialog.h"
#include "../workspaces/process_design/ui/SmartChamferDialog.h"
#include "../workspaces/machine_operation/ui/MachineProfileDialog.h"
#include "../workspaces/process_design/ui/OperationListPanel.h"
#include "../workspaces/process_design/ui/SetupOriginDialog.h"
#include "../workspaces/process_design/ui/StockDefinitionDialog.h"
#include "../workspaces/process_design/ui/StrategyPanel.h"
#include "../workspaces/process_design/ui/ToolOperationCompatibility.h"
#include "../workspaces/process_design/ui/CircleMillDialog.h"
#include "../workspaces/process_design/ui/ParameterEditorDialog.h"
#include "../workspaces/process_design/ui/ToolLibraryPanel.h"
#include "../workspaces/shared/ui/ViewportWidget.h"
#include "AppController.h"
#include "../workspaces/process_design/selection/FeatureIdentity.h"
#include "../core/SetupOrientation.h"
#include "../core/Settings.h"
#include "../workspaces/program_verification/safety/GCodeSafetyValidator.h"
#include "../gcode/ProgramPackageExporter.h"
#include "../workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"
#include "../gcode/ProgramSnapshotStatus.h"
#include "../postprocessor/PostProcessorBase.h"
#include "../postprocessor/PostProcessorRegistry.h"
#include "../workspaces/program_verification/simulation/SimulationController.h"
#include "../workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../workspaces/process_design/ProcessDesignWorkspaceFacade.h"
#include "../workspaces/program_verification/ProgramVerificationWorkspaceFacade.h"
#include "../workspaces/machine_operation/MachineOperationWorkspaceFacade.h"
#include "../strategies/StrategyFactory.h"
#include "../strategies/mill/PlanarMachiningValidation.h"
#include "../strategies/hole/HoleStrategyUtils.h"
#include "../strategies/mill/SlotMachiningGeometry.h"
#include "../tool/ToolLibrary.h"

#include <QApplication>
#include <QPainter>
#include <QFrame>
#include <QHash>
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
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QCloseEvent>
#include <QPlainTextEdit>
#include <QDebug>
#include <QQuaternion>
#include <QSignalBlocker>
#include <QScopedValueRollback>
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
#include <memory>

namespace {
QIcon processCommandIcon(const QString &strategy)
{
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(QStringLiteral("#175ca6")), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (strategy.startsWith(QStringLiteral("hole_"))) {
        painter.drawLine(10, 3, 18, 3);
        painter.drawLine(11, 3, 11, 20);
        painter.drawLine(17, 3, 17, 20);
        painter.drawLine(11, 20, 14, 25);
        painter.drawLine(17, 20, 14, 25);
        for (int y = 8; y < 20; y += 4) painter.drawLine(11, y, 17, y - 3);
    } else if (strategy.contains(QStringLiteral("face"))) {
        painter.drawPolyline(QPolygonF(QVector<QPointF>{QPointF(3, 15), QPointF(10, 8), QPointF(25, 8), QPointF(18, 15), QPointF(3, 15)}));
        painter.drawPolyline(QPolygonF(QVector<QPointF>{QPointF(3, 19), QPointF(18, 19), QPointF(25, 12)}));
    } else {
        painter.drawRoundedRect(QRectF(4, 5, 20, 18), 3, 3);
        painter.drawRoundedRect(QRectF(8, 9, 12, 10), 2, 2);
    }
    return QIcon(pixmap);
}


static QString currentProgramToolFingerprint(const ProgramEntry &program,
                                             const QList<MachiningOperation> &operations)
{
    return ProgramSnapshotFingerprint::calculateTools(
        operations, program.sourceOperationIds,
        [](int id) { return ToolLibrary::instance().tool(id); });
}

static SketchVerificationContext programSketchContext(const ProjectManager &project, const ProgramEntry &program)
{
    QList<MachiningOperation> operations;
    for(const auto &id:program.sourceOperationIds)
        for(const auto &operation:project.operations()) if(operation.id==id) operations.append(operation);
    auto context=currentSketchContext(project,operations,[](int id){return ToolLibrary::instance().tool(id);});
    if(operations.size()!=program.sourceOperationIds.size()) context.error=QStringLiteral("程序来源工序已改变，请重新生成。");
    return context;
}

static QString programSketchContextFingerprint(const ProjectManager *project, const QString &programId)
{
    return project && project->sourceType()==ProjectSourceType::Sketch
        ? programSketchContext(*project,project->programById(programId)).fingerprint() : QString();
}

static bool generatedMotionEvidenceMissing(const ProgramEntry &program, const QString &text)
{
    return (!program.expandedGcodeText.isEmpty() || !program.packageFiles.isEmpty()) &&
        (program.gcodeText != text || !program.generationContentMatches());
}

// 函数说明：isHoleFeature负责当前模块的核心处理。
static bool isHoleFeature(const MachiningFeature &feature)
{
    return feature.kind == FeatureKind::Hole || feature.kind == FeatureKind::Thread;
}

// 函数说明：isSlotFeature负责当前模块的核心处理。
static bool isSlotFeature(const MachiningFeature &feature)
{
    return feature.kind == FeatureKind::Slot;
}

// 函数说明：showFeatureInStrategyPanel负责当前模块的核心处理。
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

// 函数说明：clearStrategyPanelSelection负责当前模块的核心处理。
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

// 函数说明：averageFaceNormal负责当前模块的核心处理。
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
// 函数说明：regionFromSetupAxis负责当前模块的核心处理。
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

// 函数说明：regionPriority负责当前模块的核心处理。
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

// 函数说明：rotateMesh负责当前模块的核心处理。
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

// 函数说明：dominantFaceRegionFromMesh负责当前模块的核心处理。
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

// 函数说明：planarSurfaceSubtype负责当前模块的核心处理。
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

// 函数说明：featureFaceZRange负责当前模块的核心处理。
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

// 函数说明：slotRegionForCurrentSetup负责当前模块的核心处理。
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

// 函数说明：reclassifyFeaturesForSetup负责当前模块的核心处理。
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

// 函数说明：rotateFeatures负责当前模块的核心处理。
static QVector<MachiningFeature> rotateFeatures(const QVector<MachiningFeature> &features,
                                                const MeshData &rotatedMesh,
                                                const QQuaternion &rotation)
{
    QVector<MachiningFeature> rotated = features;
    for (MachiningFeature &feature : rotated) {
        const double oldAngle = feature.angle;
        const double w = rotation.scalar(), x = rotation.x(), y = rotation.y(), z = rotation.z();
        const Coordinate3D p = feature.center;
        feature.center = Coordinate3D(
            (1-2*(y*y+z*z))*p.x()+2*(x*y-z*w)*p.y()+2*(x*z+y*w)*p.z(),
            2*(x*y+z*w)*p.x()+(1-2*(x*x+z*z))*p.y()+2*(y*z-x*w)*p.z(),
            2*(x*z-y*w)*p.x()+2*(y*z+x*w)*p.y()+(1-2*(x*x+y*y))*p.z());
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

// 函数说明：distanceToMeshBoundaryXY负责当前模块的核心处理。
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

// 函数说明：detectOpenSlotSide负责当前模块的核心处理。
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

    const QVector3D plusPoint = (feature.center + lengthDir * halfLength).toVector3D();
    const QVector3D minusPoint = (feature.center - lengthDir * halfLength).toVector3D();
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

// 函数说明：machiningTopZ负责当前模块的核心处理。
// 函数说明：isFrontReachableSlot负责当前模块的核心处理。
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

// 函数说明：activeRegionOrder负责当前模块的核心处理。
static int activeRegionOrder(FaceRegion featureRegion, FaceRegion activeRegion)
{
    if (activeRegion == FaceRegion::Unknown) {
        return 0;
    }
    return featureRegion == activeRegion ? 0 : 1;
}

// 函数说明：prioritizeFeatureIndicesByActiveRegion负责当前模块的核心处理。
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

// 函数说明：adaptSideTaggedSlotForBlindFrontMilling负责当前模块的核心处理。
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

// 函数说明：sideSlotFrontDepthLimit负责当前模块的核心处理。
static double sideSlotFrontDepthLimit(const MachiningFeature &feature)
{
    if (feature.kind == FeatureKind::Slot &&
        (feature.region == FaceRegion::Side || std::abs(feature.axis.z()) < 0.5f) &&
        feature.width > 0.0) {
        return std::min(feature.depth > 0.0 ? feature.depth : feature.width, feature.width);
    }
    return feature.depth;
}

// 函数说明：refineSideTaggedSlotProjection负责当前模块的核心处理。
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

// 函数说明：refineSlotContourFromMesh负责当前模块的核心处理。
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

// 函数说明：refineFrontStraightSlotFromLocalSamples负责当前模块的核心处理。
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
        Coordinate3D center = contour.center + Coordinate3D(projectedLengthDir) * centerAlongProjectedLength;
        center.setZ(maxZ);
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

// 函数说明：refineSlotSlopesFromMesh负责当前模块的核心处理。
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
                const Coordinate3D rel = Coordinate3D(point) - contour.center;
                const double u = Coordinate3D::dotProduct(rel, lengthDir);
                const double v = Coordinate3D::dotProduct(rel, widthDir);
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

// 函数说明：collectFrontSlotLocalSamples负责当前模块的核心处理。
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
            const Coordinate3D rel = Coordinate3D(point) - contour.center;
            FrontSlotLocalSample sample;
            sample.point = point;
            sample.u = Coordinate3D::dotProduct(rel, lengthDir);
            sample.v = Coordinate3D::dotProduct(rel, widthDir);
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

// 函数说明：applyFrontReachableSlotMachiningGeometry负责当前模块的核心处理。
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

    Coordinate3D localSeedCenter = contour.center;
    if (bottomCount >= 3) {
        const double seedU = useUAsLength ? (bottomMinU + bottomMaxU) * 0.5
                                          : (bottomMinV + bottomMaxV) * 0.5;
        const double seedV = useUAsLength ? (bottomMinV + bottomMaxV) * 0.5
                                          : (bottomMinU + bottomMaxU) * 0.5;
        localSeedCenter = Coordinate3D(source.center.x() + lengthDir.x() * seedU + widthDir.x() * seedV,
                                    source.center.y() + lengthDir.y() * seedU + widthDir.y() * seedV,
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

    const Coordinate3D oldCenter = contour.center;
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
    contour.center = Coordinate3D(source.center.x() + lengthDir.x() * centerAlongLength + widthDir.x() * centerAlongWidth,
                               source.center.y() + lengthDir.y() * centerAlongLength + widthDir.y() * centerAlongWidth,
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

// 函数说明：isFrontEdgeOnlySlotGeometry负责当前模块的核心处理。
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
            const double u = Coordinate3D::dotProduct(rel, lengthDir);
            const double v = Coordinate3D::dotProduct(rel, widthDir);
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

// 函数说明：refineFrontSlotCenterFromMesh负责当前模块的核心处理。
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
            const Coordinate3D rel = Coordinate3D(point) - contour.center;
            samples.append({Coordinate3D::dotProduct(rel, lengthDir),
                            Coordinate3D::dotProduct(rel, widthDir),
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
    const Coordinate3D oldCenter = contour.center;
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

static ContourFeature resolveRecognizedContour(const MachiningFeature &feature, const MeshData *mesh = nullptr)
{
    ContourFeature contour = machiningContour(feature);
    if (feature.kind != FeatureKind::Slot) return contour;
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

// 函数说明：applySlotOverridesFromParams负责当前模块的核心处理。
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

// 函数说明：slotMachiningParamsWithoutGeometry负责当前模块的核心处理。
static StrategyParams slotMachiningParamsWithoutGeometry(StrategyParams params)
{
    params.values.remove(QStringLiteral("depth"));
    params.values.remove(QStringLiteral("slotLength"));
    params.values.remove(QStringLiteral("slotWidth"));
    params.values.remove(QStringLiteral("angle"));
    return params;
}

// 函数说明：logSlotContourFeature负责当前模块的核心处理。
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

// 函数说明：findNearestHoleFeature负责当前模块的核心处理。
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
        Coordinate3D delta = Coordinate3D(point) - feature.center;
        if (axis.lengthSquared() > 1.0e-8f) {
            delta -= Coordinate3D(axis) * Coordinate3D::dotProduct(delta, axis);
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

// 函数说明：holeSetupRestrictionMessage负责当前模块的核心处理。
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

// 函数说明：isSlotMillingStrategy负责当前模块的核心处理。
static bool isSlotMillingStrategy(const QString &strategyId)
{
    return strategyId == QStringLiteral("mill_slot") ||
           strategyId == QStringLiteral("mill_blind_slot") ||
           strategyId == QStringLiteral("mill_tapered_slot");
}

// 函数说明：slotSetupRestrictionMessage负责当前模块的核心处理。
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
    // 函数说明：m_gcodeEditor负责当前模块的核心处理。
    , m_gcodeEditor(new GCodeEditor(this))
    // 函数说明：m_featurePanel负责当前模块的核心处理。
    , m_featurePanel(new FeatureListPanel(this))
    // 函数说明：m_strategyPanel负责当前模块的核心处理。
    , m_strategyPanel(new StrategyPanel(this))
    // 函数说明：m_toolPanel负责当前模块的核心处理。
    , m_toolPanel(new ToolLibraryPanel(this))
    // 函数说明：m_operationPanel负责当前模块的核心处理。
    , m_operationPanel(new OperationListPanel(this))
    // 函数说明：m_bottomBar负责当前模块的核心处理。
    , m_bottomBar(new BottomBar(this))
    // 函数说明：m_simCtrl负责当前模块的核心处理。
    , m_simCtrl(new SimulationController(this))
{
    m_projectCommandController = new ProjectCommandController(
        [](const QString &path) { return AppController::instance().saveProject(path); },
        [](const QString &path, const QString &replacement) {
            return AppController::instance().loadProject(path, replacement);
        }, this);
    setMinimumSize(1200, 750);
    resize(1440, 900);
    setObjectName(QStringLiteral("mainWindow"));

    createMenus();
    createToolBar();
    createPages();
    createStatusBar();
    connectSignals();
    retranslateUi();
    setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
    setStyleSheet(QStringLiteral(
        "QPushButton, QToolButton { background: #ffffff; color: #25364d; border: 1px solid #ccd7e6; border-radius: 4px; padding: 7px 10px; }"
        "QPushButton:hover, QToolButton:hover { background: #edf4ff; border-color: #7aaaff; }"
        "QPushButton:disabled, QToolButton:disabled { color: #8b96a7; background: #f3f5f8; border-color: #dfe5ed; }"
        "QComboBox, QDoubleSpinBox { background: white; color: #25364d; border: 1px solid #ccd7e6; border-radius: 4px; min-height: 28px; padding: 3px 6px; }"
        "QTabWidget::pane { border: 1px solid #dbe3ef; background: white; }"
        "QTabBar::tab { padding: 9px 14px; background: #f7f9fc; color: #25364d; }"
        "QTabBar::tab:selected { background: #eaf2ff; color: #0865dd; border-bottom: 2px solid #1976ee; }"
        "QHeaderView::section { background: #f6f8fb; color: #334563; padding: 6px; border: 0; border-bottom: 1px solid #dbe3ef; }"
        "QMainWindow#mainWindow, QWidget#centralShell, QWidget#contentPage { background: #f3f6fb; }"
        "QWidget#systemHeader { background: #ffffff; border: 0; border-bottom: 1px solid #dbe3ef; }"
        "QLabel#productTitle { color: #172b46; font-size: 17px; font-weight: 700; padding: 0; }"
        "QLabel#workspaceTitle { color: #aeb9c9; font-size: 13px; padding-left: 10px; }"
        "QLabel#cq8ConnectionBadge { color: #ffffff; background: #b42318; border: 1px solid #d92d20; border-radius: 6px; padding: 5px 9px; font-weight: 600; }"
        "QLabel#machineModeBadge, QLabel#activeTaskBadge { color: #d9e2ef; background: #253247; border: 1px solid #3b4a62; border-radius: 6px; padding: 5px 9px; }"
        "QLabel#safetyStateBadge { color: #1f2937; background: #f6c453; border: 1px solid #d9a514; border-radius: 6px; padding: 5px 9px; font-weight: 600; }"
        "QToolBar { background: #ffffff; border: 0; border-bottom: 1px solid #d7deea; spacing: 6px; padding: 6px 10px; }"
        "QToolBar QToolButton { background: #eef3ff; border: 1px solid #d7e2f5; border-radius: 6px; padding: 6px 10px; }"
        "QToolBar QToolButton:hover { background: #e3ecff; }"
        "QToolBar QComboBox { min-height: 32px; border: 1px solid #cfd7e6; border-radius: 6px; padding: 4px 8px; background: #ffffff; }"
        "QToolBar QLabel#toolbarFieldLabel { color: #52627a; font-size: 12px; font-weight: 600; padding-left: 6px; }"
        "QListWidget#pageNav { background: #ffffff; color: #25364d; border: 0; padding: 0; }"
        "QListWidget#pageNav::item { padding: 8px 14px; margin: 0 2px; }"
        "QListWidget#pageNav::item:selected { background: #eaf2ff; color: #0865dd; font-weight: 600; border-bottom: 2px solid #1976ee; }"
        "QListWidget#pageNav::item:hover:!selected { background: #f2f6fc; }"
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
        "QWidget#designCommandStrip { background: #ffffff; border: 1px solid #dbe3ef; border-radius: 8px; }"
        "QToolButton[designCommandButton=\"true\"] { min-height: 34px; padding: 0 10px; text-align: left; background: #f8faff; color: #27364d; border: 1px solid #dbe3ef; border-radius: 6px; font-weight: 600; }"
        "QToolButton[designCommandButton=\"true\"]:hover { background: #edf3ff; border-color: #9ab8f2; }"
        "QToolButton[designCommandButton=\"true\"]::menu-indicator { subcontrol-position: right center; right: 7px; }"
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
    if (QFileInfo::exists(toolsPath) && !ToolLibrary::instance().loadFromFile(toolsPath)) {
        const QString error = ToolLibrary::instance().lastError();
        QTimer::singleShot(0, this, [this, error]() {
            OperationMessageBox::warning(this, tr("Tool library could not be loaded"),
                tr("The current tool library was kept.\n\n%1").arg(error));
        });
    }
    m_toolPanel->refresh();
}

// 函数说明：isChineseUi负责当前模块的核心处理。
bool MainWindow::isChineseUi() const
{
    return Settings::instance().language() == QStringLiteral("zh_CN");
}

// 函数说明：currentWorkOffset负责当前模块的核心处理。
QString MainWindow::currentWorkOffset() const
{
    if (!m_wcsCombo) {
        return QStringLiteral("G54");
    }
    const QString value = m_wcsCombo->currentData().toString().trimmed().toUpper();
    return value.isEmpty() ? QStringLiteral("G54") : value;
}

// 函数说明：toolCompatibilityIssueText负责当前模块的核心处理。
static QString toolCompatibilityIssueText(const ToolCompatibilityReport &report)
{
    QStringList lines;
    for (const ToolCompatibilityIssue &issue : report.issues) {
        lines.append(QStringLiteral("• %1").arg(issue.message));
    }
    return lines.join(QLatin1Char('\n'));
}

// 函数说明：confirmToolCompatibility负责当前模块的核心处理。
static bool confirmToolCompatibility(QWidget *parent,
                                     const ToolCompatibilityReport &report,
                                     bool chinese)
{
    if (report.hasBlockingIssues()) {
        OperationMessageBox::critical(
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

// 函数说明：validateSetupForProposals负责当前模块的核心处理。
bool MainWindow::validateSetupForProposals(
    const QList<OperationProposal> &proposals)
{
    const bool zh = isChineseUi();
    ProjectManager *project = AppController::instance().projectManager();
    if (m_activeRegion == FaceRegion::Unknown || !project
        || !project->setupOrigin().confirmed
        || !project->stockDefinition().confirmed) {
        OperationMessageBox::warning(
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
            if (proposal.kind == OperationProposalKind::Contour &&
                proposal.contourFeature.region == FaceRegion::Side &&
                proposal.selectionEvidence.explicitUserSelection && project) {
                const SetupAccessResult access = evaluateSelectionAccess(
                    proposal.selectionEvidence,
                    project->sourceFileFingerprint(),
                    project->setupFingerprint(),
                    currentWorkOffset(),
                    machiningRequirements(proposal.strategyId).closedBoundary);
                if (access.ok()) {
                    continue;
                }
            }
            ++blockedCount;
        }
    }
    if (blockedCount == 0) {
        return true;
    }

    OperationMessageBox::warning(
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

// 函数说明：setDesignWorkflowStage负责当前模块的核心处理。
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

// 函数说明：updateDesignWorkflowSummary负责当前模块的核心处理。
void MainWindow::updateDesignWorkflowSummary()
{
    ProjectManager *project = AppController::instance().projectManager();
    const bool zh = isChineseUi();
    const bool modelReady = project && (!project->mesh().isEmpty() || (project->sourceType() == ProjectSourceType::Sketch && !project->sketchDocument().isEmpty()));
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

// 函数说明：updateProgramActionAvailability负责当前模块的核心处理。
void MainWindow::updateProgramActionAvailability()
{
    const bool hasProgram = m_gcodeEditor &&
        !m_gcodeEditor->toPlainText().trimmed().isEmpty();
    bool outputReady = hasProgram && m_externalGCodePath.isEmpty();

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
                program.setupFingerprint,
                currentProgramToolFingerprint(program, project->operations()), program.toolFingerprint, program.coordinatesVerified());
            outputReady = status.okForOutput() &&
                !generatedMotionEvidenceMissing(program, m_gcodeEditor->toPlainText());
        }
    }

    Coordinate3D checkedOrigin;
    const QString checkedText = currentInternalVerificationText(&checkedOrigin);
    if (outputReady && m_simCtrl->checkMatches(checkedText, checkedOrigin,programSketchContextFingerprint(project,m_currentProgramId)))
        outputReady = !m_simCtrl->checkResult().blocksOutput();
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

// 函数说明：setProgramReviewBadge负责当前模块的核心处理。
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

// 函数说明：updateProgramReviewSummary负责当前模块的核心处理。
void MainWindow::updateProgramReviewSummary()
{
    const bool zh = isChineseUi();
    const bool external = !m_externalGCodePath.isEmpty();
    const QString gcode = m_gcodeEditor ? m_gcodeEditor->toPlainText() : QString();
    const bool hasProgram = !gcode.trimmed().isEmpty();
    bool snapshotReady = hasProgram;
    bool hasSnapshotMetadata = false;
    bool missingMotionEvidence = false;

    ProjectManager *project = AppController::instance().projectManager();
    if (hasProgram && project && !m_currentProgramId.trimmed().isEmpty()) {
        const ProgramEntry program = project->programById(m_currentProgramId);
        if (!program.id.isEmpty()) {
            hasSnapshotMetadata = true;
            if (hasSnapshotMetadata) {
                const ProgramSnapshotStatus status = ProgramSnapshotStatus::evaluate(
                    m_operationPanel ? m_operationPanel->operations()
                                     : QList<MachiningOperation>(),
                    program.sourceOperationIds,
                    program.sourceOperationFingerprint,
                    project->setupFingerprint(),
                    program.setupFingerprint,
                    currentProgramToolFingerprint(program, project->operations()), program.toolFingerprint, program.coordinatesVerified());
                missingMotionEvidence = generatedMotionEvidenceMissing(program, gcode);
                snapshotReady = status.okForOutput() && !missingMotionEvidence;
            }
        }
    }

    if (external) {
        setProgramReviewBadge(m_programSnapshotBadge,
            zh ? QStringLiteral("外部程序 · 仅仿真") : QStringLiteral("External · Preview only"),
            QStringLiteral("warning"));
        m_programSnapshotBadge->setToolTip(m_externalGCodePath);
    } else if (!hasProgram) {
        setProgramReviewBadge(m_programSnapshotBadge,
            zh ? QStringLiteral("程序快照 · 未生成") : QStringLiteral("Snapshot · Not generated"),
            QStringLiteral("neutral"));
    } else if (missingMotionEvidence) {
        setProgramReviewBadge(m_programSnapshotBadge,
            zh ? QStringLiteral("程序快照 · 请重新生成") : QStringLiteral("Snapshot · Regenerate required"),
            QStringLiteral("blocked"));
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

    const GCodeSafetyReport safety = hasProgram && !external
        ? GCodeSafetyValidator::validate(gcode)
        : GCodeSafetyReport();
    if (external) {
        setProgramReviewBadge(m_programSafetyBadge,
            zh ? QStringLiteral("安全校验 · 未验证") : QStringLiteral("Safety · Unverified"),
            QStringLiteral("warning"));
    } else if (!hasProgram) {
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

    Coordinate3D checkedOrigin;
    const QString checkedText = currentInternalVerificationText(&checkedOrigin);
    const bool checkMatches = external || m_simCtrl->checkMatches(checkedText, checkedOrigin,programSketchContextFingerprint(project,m_currentProgramId));
    const auto check = checkMatches ? m_simCtrl->checkResult() : SimulationCheckResult();
    QString playback;
    switch (m_simulationReviewState) {
    case SimulationReviewState::Running: playback = zh ? QStringLiteral("播放中") : QStringLiteral("Playing"); break;
    case SimulationReviewState::Paused: playback = zh ? QStringLiteral("已暂停") : QStringLiteral("Paused"); break;
    case SimulationReviewState::Completed: playback = zh ? QStringLiteral("播放结束") : QStringLiteral("Playback ended"); break;
    case SimulationReviewState::Stopped: playback = zh ? QStringLiteral("已停止") : QStringLiteral("Stopped"); break;
    case SimulationReviewState::Ready: playback = zh ? QStringLiteral("待播放") : QStringLiteral("Ready to play"); break;
    default: playback = zh ? QStringLiteral("不可播放") : QStringLiteral("Unavailable"); break;
    }
    QString checkLabel, checkStyle = QStringLiteral("warning");
    switch (check.state) {
    case SimulationCheckState::Collision:
        checkLabel = zh ? QStringLiteral("碰撞警告") : QStringLiteral("Collision warning"); checkStyle = QStringLiteral("blocked"); break;
    case SimulationCheckState::InvalidMotion:
        checkLabel = zh ? QStringLiteral("运动解析失败") : QStringLiteral("Invalid motion"); checkStyle = QStringLiteral("blocked"); break;
    case SimulationCheckState::Incomplete:
        checkLabel = zh ? QStringLiteral("检查未完成") : QStringLiteral("Check incomplete"); break;
    case SimulationCheckState::Limited:
        checkLabel = zh ? QStringLiteral("初筛完成，有范围限制") : QStringLiteral("Screened with limitations"); break;
    case SimulationCheckState::Complete:
        checkLabel = zh ? QStringLiteral("几何初筛完成") : QStringLiteral("Geometric screening complete"); break;
    default:
        checkLabel = zh ? QStringLiteral("待检查") : QStringLiteral("Not checked"); checkStyle = QStringLiteral("neutral"); break;
    }
    if (checkMatches && m_simCtrl->hasUnverifiedRetract())
        checkLabel += zh ? QStringLiteral(" · G73短退刀未验证") : QStringLiteral(" · G73 short retract unverified");
    if (checkMatches && m_simCtrl->hasUnverifiedG76LateralShift())
        checkLabel += zh ? QStringLiteral(" · G76横向让刀未验证") : QStringLiteral(" · G76 lateral shift unverified");
    if (checkMatches && m_simCtrl->hasUnverifiedG86SpindleStopRetract())
        checkLabel += zh ? QStringLiteral(" · G86停主轴退刀未验证") : QStringLiteral(" · G86 spindle-stop retract unverified");
    if (checkMatches && m_simCtrl->hasUnverifiedSpindleSync())
        checkLabel += zh ? QStringLiteral(" · 攻牙同步未验证") : QStringLiteral(" · Tapping synchronization unverified");
    setProgramReviewBadge(m_simulationStateBadge, hasProgram
        ? (zh ? QStringLiteral("轨迹 · %1 | 初筛 · %2") : QStringLiteral("Trajectory · %1 | Screening · %2")).arg(playback, checkLabel)
        : (zh ? QStringLiteral("仿真 · 等待程序") : QStringLiteral("Simulation · Waiting")),
        hasProgram ? checkStyle : QStringLiteral("neutral"));
    QStringList details = check.limitations;
    if (!check.message.isEmpty() && check.message != details.join(QLatin1Char('\n'))) details.prepend(check.message);
    details.append(zh ? QStringLiteral("范围：模型表面与刀尖截面采样、超底初筛；未验证完整刀具/刀柄包络和实际毛坯去除。")
                      : QStringLiteral("Scope: model-surface/tool-tip footprint samples and bottom checks; full cutter/shank envelopes and material removal are not verified."));
    m_simulationStateBadge->setToolTip(details.join(QLatin1Char('\n')));

    const bool outputReady = !external && hasProgram && snapshotReady && safety.ok && !check.blocksOutput();

    setProgramReviewBadge(m_outputReadinessBadge,
        external
            ? (zh ? QStringLiteral("机床输出 · 外部仿真禁用") : QStringLiteral("Output · Preview only"))
            : !hasProgram
            ? (zh ? QStringLiteral("CQ8 输出 · 等待程序") : QStringLiteral("CQ8 Output · Waiting"))
            : outputReady
                ? (zh ? QStringLiteral("CQ8 输出 · 已就绪") : QStringLiteral("CQ8 Output · Ready"))
                : (zh ? QStringLiteral("CQ8 输出 · 已阻断") : QStringLiteral("CQ8 Output · Blocked")),
        !hasProgram ? QStringLiteral("neutral")
                    : outputReady ? QStringLiteral("ready") : QStringLiteral("blocked"));
    if (!external && m_programSnapshotBadge) m_programSnapshotBadge->setToolTip(missingMotionEvidence
        ? (zh ? QStringLiteral("最终代码、展开轨迹或程序包与生成记录不一致，请重新生成。")
              : QStringLiteral("Final code, expanded motion or package differs from its generation record. Regenerate the program."))
        : QString());
    if (m_finalProgramPanel) {
        m_finalProgramPanel->setTitle(external
            ? (zh ? QStringLiteral("导入的 G 代码") : QStringLiteral("Imported G-code"))
            : (zh ? QStringLiteral("最终 CQ8 程序") : QStringLiteral("Final CQ8 Program")));
        if (auto *hint = m_finalProgramPanel->findChild<QLabel*>(QStringLiteral("panelContextHint"))) {
            hint->setText(external
                ? (zh ? QStringLiteral("原文件内容（只读）；仅仿真所选程序段，行号对应原文件。重新导入可更新内容。")
                      : QStringLiteral("Original file (read-only). Only the selected program runs; line numbers refer to this file. Re-import to update."))
                : (zh ? QStringLiteral("这是导出和下发使用的最终控制器代码，编辑后会重新校验。")
                      : QStringLiteral("This is the final controller code used for export and delivery; edits trigger validation again.")));
        }
        if (auto *label = m_finalProgramPanel->findChild<QLabel*>(QStringLiteral("macroLibraryLabel")))
            label->setVisible(!external);
    }
    if (m_macroLibraryEditor) m_macroLibraryEditor->setVisible(!external);
    if (m_externalProgramCombo) m_externalProgramCombo->setVisible(external && !m_externalPrograms.isEmpty());
    if (m_machiningHintLabel) {
        m_machiningHintLabel->setText(external
            ? (zh ? QStringLiteral("%1\n独立程序段，不跨段连线。毫米、绝对 XYZ；初始坐标未知，仅绘制起终点 XYZ 均已知的运动；G28 后相关轴重新变为未知，不绘制回零运动；G43 H 补偿表未知，不应用偏置。孔循环为孔位/深度示意。毛坯、刀具和机床未验证；倍速仅控制动画。")
                        .arg(QFileInfo(m_externalGCodePath).fileName())
                  : QStringLiteral("%1\nIndependent programs; no joining between sections. Millimetres, absolute XYZ; initial coordinates are unknown. Only moves with known XYZ endpoints are drawn. G28 invalidates referenced axes; homing is not drawn. G43 H offsets are unknown and not applied. Cycles illustrate hole position/depth only. Stock, tools and machine unverified; speed controls animation.")
                        .arg(QFileInfo(m_externalGCodePath).fileName()))
            : (zh ? QStringLiteral("导入 G 代码直接仿真，或选择已生成的程序快照。")
                  : QStringLiteral("Import G-code for preview, or select a generated snapshot.")));
    }
    if (m_programEmptyLabel) m_programEmptyLabel->setVisible(!external && project && project->programs().isEmpty());
}

// 函数说明：syncProgramList负责当前模块的核心处理。
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
                                                             program.setupFingerprint,
                                                             currentProgramToolFingerprint(program, project->operations()),
                                                             program.toolFingerprint, program.coordinatesVerified());
        }

        if (generatedMotionEvidenceMissing(program, program.gcodeText)) {
            snapshotStatus.state = ProgramSnapshotState::Changed;
            snapshotStatus.message = tr("Generated motion evidence is missing; regenerate the program.");
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

    if (selectedRow < 0 && m_programList->count() > 0 && m_externalGCodePath.isEmpty()) {
        selectedRow = m_programList->count() - 1;
    }
    if (selectedRow >= 0) {
        m_programList->setCurrentRow(selectedRow);
    }
    m_updatingProgramList = false;
    updateProgramReviewSummary();
}

// 函数说明：programSourceTooltip负责当前模块的核心处理。
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

// 函数说明：syncCurrentProgramSnapshot负责当前模块的核心处理。
void MainWindow::syncCurrentProgramSnapshot()
{
    if (!m_externalGCodePath.isEmpty() || m_currentProgramId.trimmed().isEmpty() || !m_gcodeEditor) {
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

    program.generationContentEvidence.clear();
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

// 函数说明：findProgramIdForOperation负责当前模块的核心处理。
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

// 函数说明：findOperationLine负责当前模块的核心处理。
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

// 函数说明：loadProgramById负责当前模块的核心处理。
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

    m_externalGCodePath.clear();
    m_externalGCodeText.clear();
    m_externalPrograms.clear();
    if (m_externalProgramCombo) {
        const QSignalBlocker blocker(m_externalProgramCombo);
        m_externalProgramCombo->clear();
        m_externalProgramCombo->hide();
    }
    m_simViewport->setToolGeometryVisible(true);
    m_simViewport->setMesh(project->mesh());
    m_simCtrl->setMesh(project->mesh());
    m_currentProgramId = program.id;
    if(project->sourceType()==ProjectSourceType::Sketch)
        m_simCtrl->setSketchVerificationContext(programSketchContext(*project,program));
    project->setCurrentProgramId(program.id);
    m_gcodeEditor->setGCode(program.gcodeText);
    if (m_macroLibraryEditor) {
        m_macroLibraryEditor->setPlainText(program.macroText);
    }
    m_simCtrl->loadGCode(program.expandedGcodeText.isEmpty() || !program.generationContentMatches()
                              ? program.gcodeText
                              : program.expandedGcodeText, true, 0, program.modelOrigin);
    m_simulationReviewState = m_simCtrl->moves().isEmpty()
        ? SimulationReviewState::Unavailable : SimulationReviewState::Ready;
    updateProgramActionAvailability();
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

// 函数说明：appendProgramSnapshot负责当前模块的核心处理。
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
        program.toolFingerprint = currentProgramToolFingerprint(program, m_operationPanel->operations());
    }
    program.setupFingerprint = project->setupFingerprint();
    program.sourceSummary = sourceSummary;
    program.gcodeText = gcode;
    const QString programId = project->upsertProgram(program);
    loadProgramById(programId);
    return programId;
}

// 函数说明：validateCurrentGCodeForOutput负责当前模块的核心处理。
bool MainWindow::validateCurrentGCodeForOutput(const QString &actionName)
{
    if (!m_externalGCodePath.isEmpty()) {
        m_bottomBar->setStatus(tr("外部导入程序仅用于轨迹仿真，未获得 CAM/机床输出验证。"));
        return false;
    }
    if (!m_gcodeEditor) {
        return false;
    }

    const QString gcode = m_gcodeEditor->toPlainText();
    ProjectManager *project = AppController::instance().projectManager();
    if (!project || project->setupFingerprint().isEmpty()) {
        OperationMessageBox::warning(
            this,
            tr("Setup is incomplete"),
            tr("Cannot %1 until the front face, origin, work offset, and stock allowances are confirmed.")
                .arg(actionName));
        return false;
    }
    if (project && !m_currentProgramId.trimmed().isEmpty()) {
        const ProgramEntry program = project->programById(m_currentProgramId);
        if (generatedMotionEvidenceMissing(program, gcode)) {
            OperationMessageBox::warning(this, tr("Generated motion evidence is missing"),
                tr("Cannot %1 because the final text, expanded motion, or program package no longer matches its generation evidence. Regenerate the program.").arg(actionName));
            return false;
        }
        if (!program.id.isEmpty()
            && m_operationPanel) {
            const ProgramSnapshotStatus status =
                ProgramSnapshotStatus::evaluate(m_operationPanel->operations(),
                                                program.sourceOperationIds,
                                                program.sourceOperationFingerprint,
                                                project->setupFingerprint(),
                                                program.setupFingerprint,
                                                currentProgramToolFingerprint(program, project->operations()),
                                                program.toolFingerprint, program.coordinatesVerified());
            if (!status.okForOutput()) {
                OperationMessageBox::warning(this,
                                     tr("G-code snapshot is stale"),
                                     tr("Cannot %1 because the current G-code snapshot is no longer current.\n\n%2\n\nRegenerate the program before export or machine send.")
                                         .arg(actionName, status.message));
                return false;
            }
        }
    }

    const GCodeSafetyReport report = GCodeSafetyValidator::validate(gcode);
    if (report.ok) {
        // Bind this decision to the actual export text, snapshot origin and current model,
        // independently of whichever program was last loaded into the player.
        Coordinate3D origin;
        const QString text = currentInternalVerificationText(&origin);
        SimulationController verifier;
        verifier.setMesh(project->mesh());
        if(project->sourceType()==ProjectSourceType::Sketch)
            verifier.setSketchVerificationContext(programSketchContext(*project,project->programById(m_currentProgramId)));
        verifier.loadGCode(text, true, 0, origin);
        const auto &check = verifier.checkResult();
        if (!check.blocksOutput()) return true;
        OperationMessageBox::warning(this, tr("Geometric check blocks output"),
            tr("Cannot %1 because the final motion check is incomplete or has reported a problem.\n\n%2")
                .arg(actionName, check.message));
        return false;
    }

    OperationMessageBox::warning(this,
                         tr("G-code safety check failed"),
                         tr("Cannot %1 because the current G-code failed safety checks:\n\n%2")
                             .arg(actionName, report.messages.join(QLatin1Char('\n'))));
    return false;
}

// 函数说明：createMenus负责当前模块的核心处理。
void MainWindow::createMenus()
{
    m_actImportStep = new QAction(this);
    m_actImportGCode = new QAction(this);
    m_actImportGCode->setObjectName(QStringLiteral("importGCodeAction"));
    m_actOpenProject = new QAction(this);
    m_actSaveProject = new QAction(this);
    m_actSaveProjectAs = new QAction(this);
    m_actSaveProjectAs->setObjectName(QStringLiteral("saveProjectAsAction"));
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
    m_actSaveProjectAs->setShortcut(QKeySequence::SaveAs);
    m_actResetCamera->setShortcut(QKeySequence(QStringLiteral("F5")));

    m_actLangZh->setCheckable(true);
    m_actLangEn->setCheckable(true);

    m_fileMenu = menuBar()->addMenu(QString());
    m_fileMenu->addAction(m_actImportStep);
    m_fileMenu->addAction(m_actImportGCode);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_actOpenProject);
    m_fileMenu->addAction(m_actSaveProject);
    m_fileMenu->addAction(m_actSaveProjectAs);
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

// 函数说明：createToolBar负责当前模块的核心处理。
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
    // 函数说明：connect负责当前模块的核心处理。
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
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_wcsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        ProjectManager *project = AppController::instance().projectManager();
        if (!project) return;
        project->setWorkOffset(currentWorkOffset());
        m_strategyPanel->setWorkOffset(currentWorkOffset());
        updateProgramActionAvailability();
        updateDesignWorkflowSummary();
    });
    m_actMachineProfile = m_mainToolBar->addAction(tr("Machine Profile"));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_parameterDialog) {
        if (event->type() == QEvent::Show) m_featurePanel->setEnabled(false);
        else if (event->type() == QEvent::Hide) m_featurePanel->setEnabled(true);
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::showOperationParameters()
{
    auto *slopePreview=m_parameterDialog->findChild<QLabel *>("slopedFloorAccessPreview");
    if(slopePreview)slopePreview->hide();
    if(m_workflowStrategy=="mill_sloped_floor") {
        if(!slopePreview){slopePreview=new QLabel(m_parameterDialog);static_cast<QVBoxLayout *>(m_parameterDialog->layout())->insertWidget(0,slopePreview);}
        showSlopedFloorPreview(slopePreview,bindSlopedFloor(AppController::instance().projectManager()->mesh(),m_workflowSelection));
    }
    if (m_geometryDialog) m_geometryDialog->hide();
    if (m_designSideStack && m_operationPanel)
        m_designSideStack->setCurrentWidget(m_operationPanel);
    if (!m_strategyPanel->selectStrategy(m_workflowStrategy)) {
        if (m_geometryDialog) {
            if (m_designSideStack && m_selectionPanelHost)
                m_designSideStack->setCurrentWidget(m_selectionPanelHost);
            m_geometryDialog->show();
        }
        onErrorOccurred(tr("所选区域不支持此工艺，请选择匹配的几何。"));
        return;
    }
    if (m_reselecting) m_strategyPanel->restoreDraft(m_workflowDraft, m_workflowTool);
    m_reselecting = false;
    const auto strategy = StrategyFactory::instance().strategy(m_workflowStrategy);
    m_parameterDialog->setWindowTitle(tr("%1 · 工艺参数").arg(strategy ? strategy->displayName() : m_workflowStrategy));
    m_parameterDialog->show();
    m_parameterDialog->raise();
}

void MainWindow::previewAutomaticHoles()
{
    auto *project = AppController::instance().projectManager();
    if (!project || project->sourceType() != ProjectSourceType::Step || project->mesh().isEmpty()) {
        onErrorOccurred(tr("请先导入模型并识别孔。"));return;
    }
    if (project->setupFingerprint().isEmpty()) {
        onErrorOccurred(tr("请先确认坐标系和毛坯。"));return;
    }
    if (m_parameterDialog->isVisible() || (m_geometryDialog && m_geometryDialog->isVisible()) || m_operationEditDialog) {
        m_bottomBar->setStatus(tr("请先完成或取消当前工序配置。"));return;
    }
    const QString source = project->sourceFileFingerprint(), setup = project->setupFingerprint();
    const QString features = autoHoleFeatureFingerprint(project->features());
    AutoHoleDialog dialog(project->features(), this);
    connect(project, &ProjectManager::projectChanged, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    if (project->sourceFileFingerprint() != source || project->setupFingerprint() != setup
        || autoHoleFeatureFingerprint(project->features()) != features) {
        onErrorOccurred(tr("模型或加工设置已改变，请重新预览自动孔。"));return;
    }
    const auto holes = dialog.selectedHoles();
    if (holes.isEmpty()) return;
    m_workflowStrategy = QStringLiteral("hole_g81");
    m_workflowSource = source;m_workflowSetup = setup;
    m_autoHoleFeatureFingerprint = features;
    m_workflowHoles = holes;m_reselecting = false;
    m_workflowFeatureIndex = -1;
    m_workflowSelection = SelectionChain();m_workflowSelections.clear();
    m_workflowPoints.clear();m_workflowFeatureIndices.clear();m_workflowRegionPoints.clear();
    QVector<int> faces;for (const auto &hole : holes) faces += hole.faceIndices;
    m_viewport->setHighlightedFaces(faces);
    m_strategyPanel->setFeature(holes.first());
    showOperationParameters();
}

void MainWindow::previewSmartChamfer()
{
    auto *project = AppController::instance().projectManager();
    if (!project || project->sourceType() != ProjectSourceType::Step || project->mesh().isEmpty()) {
        onErrorOccurred(tr("请先导入模型并识别孔。"));return;
    }
    if (project->setupFingerprint().isEmpty()) {
        onErrorOccurred(tr("请先确认坐标系和毛坯。"));return;
    }
    if (m_parameterDialog->isVisible() || (m_geometryDialog && m_geometryDialog->isVisible()) || m_operationEditDialog) {
        m_bottomBar->setStatus(tr("请先完成或取消当前工序配置。"));return;
    }
    const QString source = project->sourceFileFingerprint(), setup = project->setupFingerprint();
    const QString features = autoHoleFeatureFingerprint(project->features());
    SmartChamferDialog dialog(project->features(), ToolLibrary::instance().allTools(), this);
    connect(project, &ProjectManager::projectChanged, &dialog, &QDialog::reject);
    connect(&ToolLibrary::instance(), &ToolLibrary::toolLibraryChanged, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    const ToolEntry chosen = dialog.selectedTool();
    if (project->sourceFileFingerprint() != source || project->setupFingerprint() != setup
        || autoHoleFeatureFingerprint(project->features()) != features
        || ProgramSnapshotFingerprint::toolFingerprint(chosen) != ProgramSnapshotFingerprint::toolFingerprint(
            ToolLibrary::instance().tool(chosen.id))) {
        onErrorOccurred(tr("模型、加工设置或刀具已改变，请重新预览孔口倒角。"));return;
    }
    const auto holes = dialog.selectedHoles();
    if (holes.isEmpty() || chosen.id <= 0) return;
    m_workflowStrategy = QStringLiteral("hole_chamfer");
    m_workflowSource = source;m_workflowSetup = setup;m_autoHoleFeatureFingerprint = features;
    m_workflowHoles = holes;m_reselecting = false;m_workflowFeatureIndex = -1;
    m_workflowSelection = SelectionChain();m_workflowSelections.clear();
    m_workflowPoints.clear();m_workflowFeatureIndices.clear();m_workflowRegionPoints.clear();
    QVector<int> faces;for (const auto &hole : holes) faces += hole.faceIndices;
    m_viewport->setHighlightedFaces(faces);
    m_strategyPanel->setFeature(holes.first());
    showOperationParameters();
    m_strategyPanel->restoreDraft(dialog.selectedParams(), chosen.id);
}

void MainWindow::beginOperationWorkflow(const QString &strategyId)
{
    if (m_setupOverviewDialog) m_setupOverviewDialog->close();
    ProjectManager *project = AppController::instance().projectManager();
    const bool sketch = project && project->sourceType() == ProjectSourceType::Sketch;
    if (!project || (sketch ? project->sketchDocument().isEmpty() : project->mesh().isEmpty())) {
        onErrorOccurred(tr("请先导入模型并识别加工几何。"));
        return;
    }
    if (sketch && project->sketchDocument().contourKind!="rectangle" && project->sketchDocument().contourKind!="polyline") {
        onErrorOccurred(tr("当前草图仅支持凸直线闭合轮廓加工；直线、圆和圆弧可绘制保存，加工尚未验证。"));
        return;
    }
    if (sketch && !project->sketchDocument().additionalContours.isEmpty()) {
        onErrorOccurred(tr("多轮廓草图可编辑和保存；加工前请只保留一个凸闭合轮廓。"));
        return;
    }
    if (sketch && strategyId != "mill_closed_contour") {
        onErrorOccurred(tr("当前草图加工仅支持凸闭合轮廓的外形铣。"));
        return;
    }
    if (project->setupFingerprint().isEmpty()) {
        onErrorOccurred(tr("请先通过“坐标系”和“毛坯”确认加工设置，再选择加工几何。"));
        return;
    }
    if (m_operationEditDialog) { m_operationEditDialog->raise(); return; }
    if (m_parameterDialog->isVisible() || (m_geometryDialog && m_geometryDialog->isVisible())) {
        QWidget *active = m_parameterDialog->isVisible() ? m_parameterDialog : m_geometryDialog.data();
        active->raise();
        active->activateWindow();
        return;
    }
    if (m_geometryDialog) m_geometryDialog->deleteLater();
    m_reselecting = false;
    m_autoHoleFeatureFingerprint.clear();
    m_workflowStrategy = strategyId;
    m_workflowFeatureIndex = -1;
    m_workflowHoles.clear();
    m_workflowSelection = SelectionChain();
    m_workflowPoints.clear();
    m_workflowSelections.clear();
    m_workflowFeatureIndices.clear();
    m_workflowRegionPoints.clear();
    const auto features = project->features();
    const QString sourceVersion = project->sourceFileFingerprint();
    const QString setupVersion = project->setupFingerprint();
    m_workflowSource = sourceVersion;
    m_workflowSetup = setupVersion;
    const auto stillCurrent = [this, sourceVersion, setupVersion]() {
        auto *current = AppController::instance().projectManager();
        if (!current || current->sourceFileFingerprint() != sourceVersion ||
            current->setupFingerprint() != setupVersion) {
            onErrorOccurred(tr("模型或加工设置已改变，请重新选择几何。"));
            return false;
        }
        return true;
    };
    if (strategyId.startsWith(QStringLiteral("hole_"))) {
        const QVector<int> checkedIndices = m_featurePanel->checkedFeatureIndices();
        if (checkedIndices.isEmpty()) {
            onErrorOccurred(tr("请先在左侧孔列表勾选加工点。"));
            return;
        }

        QVector<int> selectedFaces;
        const QVector<int> orderedIndices =
            prioritizeFeatureIndicesByActiveRegion(checkedIndices, features, m_activeRegion);
        const CheckedHoleCollection checkedHoles =
            collectCheckedHoles(orderedIndices, features);
        m_workflowHoles = checkedHoles.holes;
        for (const HoleFeature &hole : m_workflowHoles)
            selectedFaces += hole.faceIndices;
        if (m_workflowHoles.isEmpty()) {
            onErrorOccurred(tr("左侧勾选项中没有可加工孔；已忽略 %1 个非孔项。")
                                .arg(checkedHoles.ignoredCount));
            return;
        }
        if (checkedHoles.ignoredCount > 0) {
            m_bottomBar->setStatus(
                tr("已忽略 %1 个非孔勾选项；本次加工 %2 个孔。")
                    .arg(checkedHoles.ignoredCount)
                    .arg(m_workflowHoles.size()));
        }
        m_viewport->setHighlightedFaces(selectedFaces);
        m_strategyPanel->setFeature(m_workflowHoles.first());
        showOperationParameters();
        return;
    } else {
        QList<SelectionCandidate> candidates;
        QHash<QString, int> featureIndices;
        const auto openingFeatures = std::make_shared<QHash<QString, MachiningFeature>>();
        QHash<QString, ContourFeature> recognizedRegions;
        const QVector3D machiningAxis(0, 0, 1);
        const auto bindCurrentSketch = [project](const SelectionChain &selection) {
            StrategyParams params;
            params.set("compensation",0);
            const int step=selection.orderedTopologyRefs.isEmpty() || selection.orderedTopologyRefs.first().forward?1:-1;
            params.set("camOffsetSide",-SketchClearance::convexOrientation(project->sketchDocument().points)*step);
            return bindSketchSelection(project->sketchDocument(),selection,params,
                                       project->setupFingerprint(),project->workOffset());
        };
        if (sketch) {
            SelectionCandidate candidate;
            candidate.geometryId="sketch:"+project->sketchDocument().id+":"+project->sketchDocument().contourId;
            candidate.label=tr("当前草图 · 完整保留轮廓");
            candidate.source=ChainGeometrySource::Wire;
            candidate.topologyOnly=true;
            candidate.closed=true;
            candidate.outerLoopPointCount=project->sketchDocument().points.size();
            candidate.topologyEdges=project->mesh().pickEdges;
            candidate.topologyVertices=project->mesh().pickVertices;
            candidate.boundaryLoops=project->mesh().boundaryLoops;
            candidates.append(candidate);
        }
        for (int i = 0; i < features.size(); ++i) {
            const auto &feature = features[i];
            if (isHoleFeature(feature)) continue;
            if (feature.axis.z() < 0.5f) continue;
            if (isPlanarMillingStrategy(strategyId) && feature.kind != FeatureKind::FlatSurface) continue;
            if (isPocketRegionStrategy(strategyId) && !isSlotFeature(feature) &&
                feature.kind != FeatureKind::Pocket) continue;
            const ContourFeature contour = resolveRecognizedContour(feature, &project->mesh());
            const QVector<int> frontFaces = machiningPlaneFaceIndices(
                project->mesh(), feature.faceIndices, machiningAxis);
            if (frontFaces.isEmpty()) continue;
            SelectionCandidate candidate;
            candidate.geometryId = stableContourId(contour);
            for (int faceIndex : frontFaces) {
                candidate.faceIds.append(QStringLiteral("face:%1").arg(faceIndex));
            }
            candidate.label = tr("识别建议 %1 · %2 · %3 个面").arg(i + 1)
                .arg(isSlotFeature(feature) ? tr("槽区域") :
                     feature.kind == FeatureKind::Pocket ? tr("型腔区域") : tr("平面区域"))
                .arg(feature.faceIndices.size());
            candidate.referencePoint = contour.center.toVector3D();
            candidate.surfaceNormal = contour.axis;
            candidate.topologyOnly = true;
            candidate.closed = contour.subType != QStringLiteral("open_contour") && candidate.points.size() >= 3;
            candidate.outerLoopPointCount = candidate.points.size();
            candidate.islandCount = contour.islands.size();
            candidate.freeEndCount = candidate.closed ? 0 : 2;
            QSet<QString> topologyEdgeIds;
            QSet<QString> topologyVertexIds;
            for (const MeshBoundaryLoop &loop : project->mesh().boundaryLoops) {
                if (!frontFaces.contains(loop.faceIndex)) continue;
                candidate.boundaryLoops.append(loop);
                for (const QString &edgeId : loop.orderedEdgeIds) topologyEdgeIds.insert(edgeId);
            }
            for (const MeshPickEdge &edge : project->mesh().pickEdges) {
                if (!topologyEdgeIds.contains(edge.id)) continue;
                candidate.topologyEdges.append(edge);
                topologyVertexIds.insert(edge.firstVertexId);
                topologyVertexIds.insert(edge.lastVertexId);
            }
            for (const MeshPickVertex &vertex : project->mesh().pickVertices)
                if (topologyVertexIds.contains(vertex.id)) candidate.topologyVertices.append(vertex);
            if (candidate.points.isEmpty() && candidate.boundaryLoops.isEmpty()) continue;
            candidates.append(candidate);
            featureIndices.insert(candidate.geometryId, i);
            recognizedRegions.insert(candidate.geometryId,contour);
        }
        QSet<int> selectableFaces;
        for (const MeshBoundaryLoop &loop : project->mesh().boundaryLoops)
            selectableFaces.insert(loop.faceIndex);
        QList<int> allFaces = selectableFaces.values();
        std::sort(allFaces.begin(), allFaces.end());
        const QVector<int> sortedFaces = machiningPlaneFaceIndices(
            project->mesh(), QVector<int>(allFaces.cbegin(), allFaces.cend()), machiningAxis);
        for (int faceIndex : sortedFaces) {
            SelectionCandidate candidate;
            candidate.geometryId = QStringLiteral("topology:face:%1").arg(faceIndex);
            candidate.faceIds = QStringList{QStringLiteral("face:%1").arg(faceIndex)};
            candidate.label = tr("模型面 %1 · 真实边界").arg(faceIndex);
            candidate.topologyOnly = true;
            QSet<QString> edgeIds, vertexIds;
            for (const MeshBoundaryLoop &loop : project->mesh().boundaryLoops) {
                if (loop.faceIndex != faceIndex) continue;
                candidate.boundaryLoops.append(loop);
                // Source inner loops have no machining role until region resolution.
                for (const QString &id : loop.orderedEdgeIds) edgeIds.insert(id);
            }
            for (const MeshPickEdge &edge : project->mesh().pickEdges) {
                if (!edgeIds.contains(edge.id)) continue;
                candidate.topologyEdges.append(edge);
                vertexIds.insert(edge.firstVertexId);
                vertexIds.insert(edge.lastVertexId);
            }
            for (const MeshPickVertex &vertex : project->mesh().pickVertices)
                if (vertexIds.contains(vertex.id)) candidate.topologyVertices.append(vertex);
            for (const Triangle &triangle : project->mesh().triangles) {
                if (triangle.faceIndex != faceIndex) continue;
                candidate.surfaceNormal = triangle.normal;
                candidate.referencePoint = (triangle.v0 + triangle.v1 + triangle.v2) / 3.0f;
                break;
            }
            candidates.append(candidate);
        }
        const auto sourcePlanes = selectionSourcePlanes(project->mesh());
        for (auto &candidate : candidates)
            if (candidate.faceIds.size() == 1)
                candidate.sourcePlane = sourcePlanes.value(candidate.faceIds.first().section(':', 1).toInt());
        const auto regionErrors = std::make_shared<QHash<QString, QString>>();
        if (candidates.isEmpty()) { onErrorOccurred(tr("没有与此工艺匹配的可选区域。")); return; }
        auto *dialog = new SelectionChainDialog(m_selectionPanelHost);
        dialog->setWindowFlags(Qt::Widget);
        if (m_selectionPanelHost && m_selectionPanelHost->layout())
            m_selectionPanelHost->layout()->addWidget(dialog);
        if (m_designSideStack && m_selectionPanelHost)
            m_designSideStack->setCurrentWidget(m_selectionPanelHost);
        m_geometryDialog = dialog;
        m_viewport->installEventFilter(dialog);
        dialog->setRequireClosedBoundary(machiningRequirements(strategyId).closedBoundary);
        dialog->setEdgePickingOnly(machiningRequirements(strategyId).edgePickingOnly);
        dialog->setContext(sourceVersion, setupVersion, currentWorkOffset());
        if (isPocketRegionStrategy(strategyId)) {
            dialog->resolveBoundary = [sourceVersion, setupVersion, openingFeatures, regionErrors, strategyId]
                (SelectionCandidate &candidate, const MeshBoundaryLoop &loop) {
                const QString key = loop.faceId + "/" + loop.orderedEdgeIds.join(',');
                openingFeatures->remove(key);
                regionErrors->remove(key);
                candidate.regionErrors.remove(key);
                for (int i = candidate.machiningBoundaries.size() - 1; i >= 0; --i) {
                    const auto &old = candidate.machiningBoundaries[i];
                    if (old.faceId == loop.faceId && old.orderedEdgeIds == loop.orderedEdgeIds)
                        candidate.machiningBoundaries.removeAt(i);
                }
                SelectionBoundaryLoop proof;
                MachiningFeature opening;
                QString error;
                auto *current = AppController::instance().projectManager();
                if (!current || current->sourceFileFingerprint() != sourceVersion ||
                    current->setupFingerprint() != setupVersion) {
                    error = QStringLiteral("SELECTION_SOURCE_OR_SETUP_STALE");
                } else if (verifyPocketOpening(current->mesh(), loop, &proof, &opening, &error)) {
                    double floorDepth=0;
                    if(strategyId=="mill_pocket_rough" || strategyId=="mill_pocket_finish" ||
                       verifyPocketFlatFloor(current->mesh(),loop,proof,&floorDepth,&error)) {
                        candidate.machiningBoundaries.append(proof);
                        openingFeatures->insert(key, opening);
                        return;
                    }
                }
                if (proof.machiningRole == MachiningBoundaryRole::Island)
                    candidate.machiningBoundaries.append(proof);
                candidate.regionErrors.insert(key, error);
                regionErrors->insert(key, error);
            };
        }
        const auto selectedFeatureIndex=[featureIndices,recognizedRegions,project](const SelectionChain &selection,const QString &id){
            if(featureIndices.contains(id) && selectionMatchesContour(selection,recognizedRegions.value(id),project->mesh()))return featureIndices.value(id);
            int match=-1;
            for(auto it=featureIndices.cbegin();it!=featureIndices.cend();++it){
                if(!selectionMatchesContour(selection,recognizedRegions.value(it.key()),project->mesh()))continue;
                if(match>=0 && match!=it.value())return -1;
                match=it.value();
            }
            return match;
        };
        dialog->acceptanceError = [selectedFeatureIndex, openingFeatures, regionErrors, strategyId, sourceVersion, setupVersion, sketch, bindCurrentSketch](const SelectionChain &selection, const QString &id) {
            const auto *current = AppController::instance().projectManager();
            if (!current || current->sourceFileFingerprint() != sourceVersion ||
                current->setupFingerprint() != setupVersion)
                return tr("模型或加工设置已改变，请重新选择几何。");
            if (sketch) return bindCurrentSketch(selection).error;
            if(strategyId=="mill_sloped_floor")return bindSlopedFloor(current->mesh(),selection).error;
            for (const auto &loop : selection.boundaryLoops)
                if (selectionContainsCompleteOpening(selection,loop) &&
                    openingFeatures->contains(loop.faceId + "/" + loop.orderedEdgeIds.join(','))) return QString();
            if(selectedFeatureIndex(selection,id)>=0)return QString();
            if(isPocketRegionStrategy(strategyId)) {
                if(selection.selectionMode==ChainSelectionMode::Face)
                    return tr("实体面不能替代具体型腔环；请选择该面的目标边界，所有模式共用同一选区验证。");
                for(const auto &loop:selection.boundaryLoops){const auto error=regionErrors->value(loop.faceId+"/"+loop.orderedEdgeIds.join(','));if(!error.isEmpty())return tr("区域尚未验证：%1。请选择受支持的完整边界并核对 Setup。").arg(error);}
                return tr("加工区域未判定；请选择一个完整的真实闭环。");
            }
            return tr("所选真实环尚未绑定到受支持的加工区域；请使用匹配的识别候选或重新选择边界。");
        };
        dialog->confirmedTopologyPreviewChanged =
            [this](const QVector<QVector3D> &points, bool closed) {
                m_viewport->setConfirmedContourPreview(points, closed);
            };
        dialog->topologyPreviewChanged = [this, dialog](const QVector<QVector3D> &points, bool closed) {
            if (dialog->hasCandidatePreview())
                m_viewport->setContourChoicePreview(points, closed, 0, true);
            else
                m_viewport->clearContourChoicePreview();
        };
        dialog->topologyHoverChanged = [this](const QVector<QVector3D> &points) {
            m_viewport->setTopologyHoverPreview(points);
        };
        dialog->facePreviewChanged = [this](const QStringList &ids) {
            QVector<int> faces;
            for (const QString &id : ids) faces.append(id.section(QLatin1Char(':'), 1).toInt());
            m_viewport->setHighlightedFaces(faces);
        };
        dialog->pickModeChanged = [this](int mode) {
            m_viewport->setTopologyPickMode(mode);
        };
        dialog->topologyScopeChanged = [this](const QStringList &edges,
                                               const QStringList &vertices,
                                               const QVector<int> &faces) {
            m_viewport->setTopologyPickFilter(edges, vertices, faces);
        };
        dialog->setCandidates(candidates);
        dialog->setPreviousSelections(project->lastEstablishedSelections());
        m_viewport->setTopologyPickMode(static_cast<int>(ViewportWidget::TopologyPickMode::Edge));
        connect(dialog, &QDialog::accepted, this, [this, dialog, selectedFeatureIndex, openingFeatures, features, stillCurrent, sketch, bindCurrentSketch, strategyId]() {
            if (!stillCurrent()) return;
            const auto selections = dialog->selections();
            if (sketch && selections.size()!=1) { onErrorOccurred(tr("草图加工请选择一个完整轮廓。")); return; }
            if(strategyId=="mill_sloped_floor" && selections.size()!=1) {onErrorOccurred(tr("斜面加工请选择一个完整实体面。"));return;}
            QVector<int> indices;
            QVector<QVector<QVector3D>> regionPoints;
            if (selections.size() > 1) {
                for (const auto &selection : selections) {
                    const int index = selectedFeatureIndex(selection, selection.candidateGeometryId);
                    if (index < 0) { onErrorOccurred(tr("整组选区中的区域已失效，请重新选择。")); return; }
                    indices.append(index);
                    regionPoints.append(dialog->selectedPoints(selection));
                }
            }
            m_workflowSelections = selections;
            m_workflowFeatureIndices = indices;
            m_workflowRegionPoints = regionPoints;
            m_workflowSelection = dialog->selection();
            m_workflowPoints = dialog->selectedPoints();
            const QString id = dialog->selectedCandidateGeometryId();
            if(strategyId=="mill_sloped_floor") {
                const auto *current=AppController::instance().projectManager();
                const auto binding=bindSlopedFloor(current->mesh(),m_workflowSelection);
                if(!binding.error.isEmpty()){onErrorOccurred(binding.error);return;}
                m_workflowFeatureIndex=-1;m_workflowStrategy=strategyId;
                m_workflowPoints=binding.feature.boundaryPoints;
                m_strategyPanel->setContourFeature(binding.feature);
                showOperationParameters();
                m_viewport->setConfirmedContourPreview(binding.feature.boundaryPoints,true);
                m_bottomBar->setStatus(binding.curvedWallCandidate
                    ? tr("曲线斜面候选边界；可达性尚未证明。生成时须通过完整实体刀具与刀柄扫掠检查；孔边与加工余量保留。")
                    : tr("仅加工当前 +Z 装夹可达区域；轮廓外条带保留。遮挡侧壁：%1。孔边与加工余量另行保留。")
                        .arg(binding.overhangingWallIds.isEmpty()?tr("无"):binding.overhangingWallIds.join(", ")));
                return;
            }
            if (sketch) {
                const auto binding=bindCurrentSketch(m_workflowSelection);
                if(!binding.error.isEmpty()){onErrorOccurred(binding.error);return;}
                MachiningFeature feature;
                feature.kind=FeatureKind::Boss;
                feature.subType="closed_contour";
                feature.region=FaceRegion::Front;
                feature.center=binding.points.first();
                m_workflowPoints.clear();
                for(const auto &point:binding.points) m_workflowPoints.append(point.toVector3D());
                feature.boundaryPoints=m_workflowPoints;
                m_strategyPanel->setContourFeature(feature);
                showOperationParameters();
                return;
            }
            for (const auto &loop : m_workflowSelection.boundaryLoops) {
                const QString key = loop.faceId + "/" + loop.orderedEdgeIds.join(',');
                if (!selectionContainsCompleteOpening(m_workflowSelection,loop) || !openingFeatures->contains(key)) continue;
                m_workflowFeatureIndex = -1;
                m_workflowStrategy = strategyId;
                m_strategyPanel->setContourFeature(openingFeatures->value(key));
                showOperationParameters();
                double floorDepth=0,throughDepth=0;
                const auto *current=AppController::instance().projectManager();
                if(current)for(const auto &source:current->mesh().boundaryLoops)
                    if(source.faceId==loop.faceId && source.orderedEdgeIds==loop.orderedEdgeIds) {
                        if(!verifyPocketFlatFloor(current->mesh(),source,loop,&floorDepth) && strategyId!="mill_pocket_floor_finish")
                            verifyPocketThroughExit(current->mesh(),source,loop,&throughDepth);
                        break;
                    }
                if(floorDepth>0)
                    m_bottomBar->setStatus(tr("已验证真实平底深度 %1 mm；请确认加工深度，侧壁与底面余量分别设置。深度取整时向浅处取值。")
                        .arg(floorDepth,0,'g',12));
                else if(throughDepth>0)
                    m_bottomBar->setStatus(tr("已验证所选实体的贯通出口深度 %1 mm；粗加工与侧壁不得超过该深度。带孔斜面须另建底面工序。")
                        .arg(throughDepth,0,'g',12));
                else
                    m_bottomBar->setStatus(tr("槽口作为加工外边界；源内环身份保留。请明确输入深度，公共可加工上限 %1 mm；更深斜底及下层开口未验证。")
                        .arg(loop.maximumDepth,0,'f',3));
                return;
            }
            const int index = selectedFeatureIndex(m_workflowSelection,id);
            if(index<0) { onErrorOccurred(tr("所选区域已失效。")); return; }
            m_workflowFeatureIndex = index;
            m_featurePanel->selectFeature(index);
            if (m_workflowStrategy == QStringLiteral("mill_pocket_rough") && isSlotFeature(features[index]))
                m_workflowStrategy = features[index].subType == QStringLiteral("open_slot")
                    ? QStringLiteral("mill_slot") : QStringLiteral("mill_blind_slot");
            m_strategyPanel->setContourFeature(features[index]);
            showOperationParameters();
        });
        connect(dialog, &QDialog::finished, this, [this, strategyId](int result) {
            m_viewport->setTopologyPickMode(static_cast<int>(ViewportWidget::TopologyPickMode::Face));
            m_viewport->clearTopologyPickFilter();
            if(strategyId!="mill_sloped_floor" || result!=QDialog::Accepted)m_viewport->clearConfirmedContourPreview();
            m_viewport->clearContourChoicePreview();
            if (m_designSideStack && m_operationPanel)
                m_designSideStack->setCurrentWidget(m_operationPanel);
        });
    }
    connect(m_geometryDialog, &QDialog::rejected, this, [this]() {
        if (!m_reselecting) return;
        m_strategyPanel->restoreDraft(m_workflowDraft, m_workflowTool);
        m_reselecting = false;
        m_parameterDialog->show();
        m_parameterDialog->raise();
    });
    m_geometryDialog->setModal(false);
    m_geometryDialog->show();
}

// 函数说明：createPages负责当前模块的核心处理。
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
    m_workspaceTitleLabel->hide();
    headerLayout->addStretch(1);
    m_cq8ConnectionBadge->hide();
    m_machineModeBadge->hide();
    m_activeTaskBadge->hide();
    m_safetyStateBadge->hide();

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

    auto *designCommandStrip = new QWidget(m_designPage);
    designCommandStrip->setObjectName(QStringLiteral("designCommandStrip"));
    auto *commandLayout = new QHBoxLayout(designCommandStrip);
    commandLayout->setContentsMargins(10, 8, 10, 8);
    commandLayout->setSpacing(7);
    const auto addGroup = [commandLayout, designCommandStrip](const QString &title) {
        auto *group = new QGroupBox(title, designCommandStrip);
        auto *layout = new QHBoxLayout(group);
        layout->setContentsMargins(8, 12, 8, 8);
        layout->setSpacing(4);
        commandLayout->addWidget(group);
        return layout;
    };
    QHash<QString, QHBoxLayout *> commandGroups;
    for (const auto &entry : designFeatureCatalog()) {
        if (entry.commandGroup.isEmpty()) continue;
        auto *layout = commandGroups.value(entry.commandGroup, nullptr);
        if (!layout) {
            layout = addGroup(tr(entry.commandGroup.toUtf8().constData()));
            commandGroups.insert(entry.commandGroup, layout);
        }
        auto *button = new QToolButton(m_designPage);
        button->setObjectName(QStringLiteral("designFeature_") + entry.id);
        button->setText(tr(entry.commandLabel.toUtf8().constData()));
        button->setMinimumSize(54, 52);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIcon(processCommandIcon(entry.strategy));
        button->setIconSize(QSize(28, 28));
        button->setEnabled(entry.commandEnabled);
        button->setToolTip(entry.scope);
        layout->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, strategy = entry.strategy]() {
            beginOperationWorkflow(strategy);
        });
    }
    auto *output = addGroup(tr("验证与输出"));
    auto *verify = new QPushButton(tr("仿真"), designCommandStrip);
    output->addWidget(verify);
    connect(verify, &QPushButton::clicked, this, [this]() { m_pageNav->setCurrentRow(1); });
    auto *generate = new QPushButton(tr("生成程序"), designCommandStrip);
    output->addWidget(generate);
    connect(generate, &QPushButton::clicked, this, [this]() {
        QMetaObject::invokeMethod(m_operationPanel, "onGenerateProgram", Qt::DirectConnection);
    });
    commandLayout->addStretch();
    designLayout->addWidget(designCommandStrip);
    m_designWorkflowStrip->hide();

    auto *resources = new QGroupBox(tr("设置与资源"), m_designPage);
    resources->setMinimumWidth(170);
    resources->setMaximumWidth(220);
    auto *resourceLayout = new QVBoxLayout(resources);
    const auto resourceAction = [resources, resourceLayout](const QString &label, QAction *action) {
        auto *button = new QToolButton(resources);
        button->setText(label);
        button->setIcon(action->icon());
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setMinimumHeight(42);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        resourceLayout->addWidget(button);
        QObject::connect(button, &QToolButton::clicked, action, &QAction::trigger);
    };
    resourceAction(tr("导入模型"), m_actImportStep);
    auto *sketchButton = new QToolButton(resources);
    sketchButton->setText(tr("二维绘图"));
    sketchButton->setObjectName(QStringLiteral("sketchEditorButton"));
    sketchButton->setMinimumHeight(42);
    sketchButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    resourceLayout->addWidget(sketchButton);
    connect(sketchButton, &QToolButton::clicked, this, &MainWindow::onEditSketch);

    auto *setupButton = new QToolButton(resources);
    setupButton->setText(tr("加工设置"));
    setupButton->setObjectName(QStringLiteral("setupOverviewButton"));
    setupButton->setMinimumHeight(42);
    setupButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    resourceLayout->addWidget(setupButton);
    connect(setupButton, &QToolButton::clicked, this, &MainWindow::showSetupOverview);
    resourceAction(tr("坐标系"), m_actSetupOrigin);
    resourceAction(tr("毛坯"), m_actStockDefinition);
    auto *toolsButton = new QPushButton(tr("刀具库"), resources);
    toolsButton->setMinimumHeight(42);
    resourceLayout->addWidget(toolsButton);
    auto *toolDialog = new QDialog(this);
    toolDialog->setWindowTitle(tr("刀具库"));
    auto *toolLayout = new QVBoxLayout(toolDialog);
    toolLayout->addWidget(m_toolPanel);
    toolDialog->resize(560, 640);
    connect(toolsButton, &QPushButton::clicked, toolDialog, &QDialog::show);
    auto *templatesButton = new QPushButton(tr("工艺模板"), resources);
    templatesButton->setObjectName(QStringLiteral("processTemplateManagerButton"));
    templatesButton->setMinimumHeight(42);
    resourceLayout->addWidget(templatesButton);
    connect(templatesButton, &QPushButton::clicked, this, [this]() {
        if (m_parameterDialog->isVisible() || (m_geometryDialog && m_geometryDialog->isVisible()) || m_operationEditDialog) {
            m_bottomBar->setStatus(tr("请先完成或取消当前工序配置，再管理模板。"));
            return;
        }
        m_strategyPanel->showTemplateManager();
    });
    auto *materialsButton = new QPushButton(tr("材料库"), resources);
    materialsButton->setObjectName(QStringLiteral("materialLibraryButton"));
    materialsButton->setMinimumHeight(42);
    resourceLayout->addWidget(materialsButton);
    connect(materialsButton, &QPushButton::clicked, this, [this]() {
        if (m_parameterDialog->isVisible() || (m_geometryDialog && m_geometryDialog->isVisible()) || m_operationEditDialog) {
            m_bottomBar->setStatus(tr("请先完成或取消当前工序配置，再管理材料建议。"));
            return;
        }
        m_strategyPanel->showMaterialLibrary();
    });
    auto *autoHoleButton = new QPushButton(tr("自动孔"), resources);
    autoHoleButton->setObjectName(QStringLiteral("autoHoleButton"));
    autoHoleButton->setMinimumHeight(42);
    resourceLayout->addWidget(autoHoleButton);
    connect(autoHoleButton, &QPushButton::clicked, this, &MainWindow::previewAutomaticHoles);
    auto *smartChamferButton = new QPushButton(tr("智能倒角"), resources);
    smartChamferButton->setObjectName(QStringLiteral("smartChamferButton"));
    smartChamferButton->setMinimumHeight(42);resourceLayout->addWidget(smartChamferButton);
    connect(smartChamferButton, &QPushButton::clicked, this, &MainWindow::previewSmartChamfer);
    m_featurePanel->setObjectName(QStringLiteral("recognizedFeatureColumn"));
    m_featurePanel->setMinimumWidth(260);
    resourceLayout->addStretch();
    auto *modelArea = new QWidget(m_designPage);
    modelArea->setObjectName(QStringLiteral("designModelArea"));
    auto *modelLayout = new QVBoxLayout(modelArea);
    modelLayout->setContentsMargins(0, 0, 0, 0);
    modelLayout->setSpacing(6);
    modelLayout->addWidget(m_viewport, 1);
    auto *viewBar = new QWidget(modelArea);
    viewBar->setObjectName(QStringLiteral("modelViewBar"));
    auto *views = new QHBoxLayout(viewBar);
    views->setContentsMargins(0, 0, 0, 0);
    views->setSpacing(4);
    auto *modelVisibility = new QPushButton(tr("显示模型"), viewBar);
    modelVisibility->setObjectName(QStringLiteral("modelVisibilityButton"));
    modelVisibility->setCheckable(true);
    modelVisibility->setChecked(m_viewport->modelVisible());
    modelVisibility->setMinimumHeight(34);
    modelVisibility->setToolTip(tr("仅改变当前窗口的模型显示与拾取；保留刀路和加工几何。"));
    views->addWidget(modelVisibility);
    connect(modelVisibility, &QPushButton::toggled, m_viewport, &ViewportWidget::setModelVisible);
    connect(m_viewport, &ViewportWidget::modelVisibilityChanged, modelVisibility, &QPushButton::setChecked);
    const QStringList viewNames = {tr("前视"), tr("后视"), tr("左视"), tr("右视"),
                                   tr("顶视"), tr("底视"), tr("等轴测"), tr("适应窗口")};
    for (int i = 0; i < viewNames.size(); ++i) {
        auto *button = new QPushButton(viewNames[i], viewBar);
        button->setObjectName(QStringLiteral("modelView_%1").arg(i));
        button->setMinimumHeight(34);
        views->addWidget(button, 1);
        connect(button, &QPushButton::clicked, this, [this, i]() {
            if (i == 7) m_viewport->fitAll();
            else m_viewport->setStandardView(static_cast<ViewportWidget::StandardView>(i));
        });
    }
    modelLayout->insertWidget(0, viewBar);
    m_parameterDialog = new QDialog(this);
    m_parameterDialog->installEventFilter(this);
    m_parameterDialog->setWindowTitle(tr("工艺参数"));
    m_parameterDialog->setModal(false);
    auto *parameterLayout = new QVBoxLayout(m_parameterDialog);
    parameterLayout->addWidget(m_strategyPanel);
    m_parameterDialog->resize(500, 710);
    auto *cancelParameters = new QPushButton(tr("取消"), m_parameterDialog);
    parameterLayout->addWidget(cancelParameters, 0, Qt::AlignRight);
    connect(cancelParameters, &QPushButton::clicked, m_parameterDialog, &QDialog::reject);
    connect(m_strategyPanel, &StrategyPanel::reselectGeometryRequested, this, [this]() {
        m_workflowDraft = m_strategyPanel->parameterDraft();
        m_workflowTool = m_strategyPanel->currentTool().id;
        if (m_workflowStrategy.startsWith(QStringLiteral("hole_"))) {
            m_bottomBar->setStatus(
                tr("当前孔批次已固定；如需更换孔，请取消参数页后重新选择。"));
            return;
        }
        m_reselecting = true;
        m_parameterDialog->hide();
        if (m_geometryDialog) {
            if (m_designSideStack && m_selectionPanelHost)
                m_designSideStack->setCurrentWidget(m_selectionPanelHost);
            m_geometryDialog->show();
            m_geometryDialog->raise();
        }
    });
    m_designSideStack = new QStackedWidget(m_designPage);
    m_designSideStack->setObjectName(QStringLiteral("designSideStack"));
    m_designSideStack->setMinimumWidth(360);
    m_designSideStack->setMaximumWidth(440);
    m_designSideStack->addWidget(m_operationPanel);
    m_selectionPanelHost = new QWidget(m_designSideStack);
    m_selectionPanelHost->setObjectName(QStringLiteral("selectionPanelHost"));
    auto *selectionPanelLayout = new QVBoxLayout(m_selectionPanelHost);
    selectionPanelLayout->setContentsMargins(0, 0, 0, 0);
    selectionPanelLayout->setSpacing(0);
    m_designSideStack->addWidget(m_selectionPanelHost);
    m_designSideStack->setCurrentWidget(m_operationPanel);
    auto *designCenterSplitter = new QSplitter(Qt::Horizontal, m_designPage);
    designCenterSplitter->addWidget(resources);
    designCenterSplitter->addWidget(m_featurePanel);
    designCenterSplitter->addWidget(modelArea);
    designCenterSplitter->addWidget(m_designSideStack);
    designCenterSplitter->setStretchFactor(0, 0);
    designCenterSplitter->setStretchFactor(1, 0);
    designCenterSplitter->setStretchFactor(2, 1);
    designCenterSplitter->setStretchFactor(3, 0);
    designCenterSplitter->setSizes({180, 280, 800, 380});
    designLayout->addWidget(designCenterSplitter, 1);

    connect(m_designModelStage, &QToolButton::clicked,
            m_actImportStep, &QAction::trigger);
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_designSetupStage, &QToolButton::clicked, this, [this]() {
        if (m_activeRegion == FaceRegion::Unknown) {
            m_actSetFrontFace->trigger();
        } else if (!AppController::instance().projectManager()->setupOrigin().confirmed) {
            onEditSetupOrigin();
        } else {
            onEditStockDefinition();
        }
    });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_designFeatureStage, &QToolButton::clicked, this, [this]() {
        if (m_featureDock) m_featureDock->raise();
        if (m_featurePanel) m_featurePanel->setFocus();
    });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_designOperationStage, &QToolButton::clicked, this, [this]() {
        if (m_operationDock) m_operationDock->raise();
        if (m_operationPanel) m_operationPanel->setFocus();
    });
    // 函数说明：connect负责当前模块的核心处理。
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
    auto *btnImportGCode = new QToolButton(m_machiningActionsGroup);
    btnImportGCode->setObjectName(QStringLiteral("importGCodeButton"));
    btnImportGCode->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btnImportGCode->setDefaultAction(m_actImportGCode);
    machiningActionsLayout->addWidget(btnImportGCode);
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
    m_externalProgramCombo = new QComboBox(m_machiningActionsGroup);
    m_externalProgramCombo->setObjectName(QStringLiteral("externalProgramSelector"));
    m_externalProgramCombo->setAccessibleName(tr("所选外部程序段"));
    m_externalProgramCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_externalProgramCombo->setMinimumContentsLength(18);
    m_externalProgramCombo->hide();
    machiningActionsLayout->addWidget(m_externalProgramCombo);
    connect(m_externalProgramCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::selectExternalProgram);
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

    m_pageNav->setFlow(QListView::LeftToRight);
    m_pageNav->setFixedSize(360, 40);
    m_pageNav->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pageNav->setGridSize(QSize(116, 38));
    headerLayout->insertWidget(2, m_pageNav);
    m_mainToolBar->hide();
    bodyLayout->addWidget(m_pageStack, 1);
    rootLayout->addWidget(systemHeader);
    rootLayout->addWidget(bodyShell, 1);
    setCentralWidget(central);

    m_workspaceRouter = new AppWorkspaceRouter(*m_pageNav, *m_pageStack,
                                               *m_workspaceTitleLabel, this);
    m_workspaceRouter->showWorkspace(WorkspaceArchitecture::WorkspaceId::ProcessDesign);
}

// 函数说明：createStatusBar负责当前模块的核心处理。
void MainWindow::createStatusBar()
{
    statusBar()->addPermanentWidget(m_bottomBar, 1);
}

// 函数说明：connectSignals负责当前模块的核心处理。
void MainWindow::connectSignals()
{
    connect(m_actImportGCode, &QAction::triggered, this, &MainWindow::onImportGCode);
    const auto captureTools = []() {
        QHash<int, QString> fingerprints;
        for (const ToolEntry &tool : ToolLibrary::instance().allTools()) {
            fingerprints.insert(tool.id, ProgramSnapshotFingerprint::toolFingerprint(tool));
        }
        return fingerprints;
    };
    connect(&ToolLibrary::instance(), &ToolLibrary::toolLibraryChanged, this,
            [this, captureTools, previousTools = captureTools()](bool persist) mutable {
        const QHash<int, QString> currentTools = captureTools();
        ProjectManager *project = AppController::instance().projectManager();
        if (project) {
            QSet<int> changedTools;
            for (const MachiningOperation &operation : project->operations()) {
                if (previousTools.value(operation.toolId) != currentTools.value(operation.toolId)) {
                    changedTools.insert(operation.toolId);
                }
            }
            project->invalidateToolOperations(changedTools);
        }
        if (previousTools != currentTools && m_externalGCodePath.isEmpty()) {
            m_simCtrl->loadGCode(QString());
            m_simViewport->clearToolPath();
            m_bottomBar->showProgress(false);
            m_simulationReviewState = SimulationReviewState::Unavailable;
        }
        previousTools = currentTools;
        const QString toolsPath = QCoreApplication::applicationDirPath() + QStringLiteral("/tools.json");
        if (persist && !ToolLibrary::instance().saveToFile(toolsPath)) {
            const QString error = ToolLibrary::instance().lastError();
            QTimer::singleShot(0, this, [this, error]() {
                OperationMessageBox::warning(this, tr("Tool library was not saved"),
                    tr("Tool changes are available in this session but were not saved to disk.\n\n%1").arg(error));
            });
        }
        syncProgramList();
        updateProgramActionAvailability();
    });

    connect(m_actImportStep, &QAction::triggered, this, &MainWindow::onImportStep);    connect(m_actOpenProject, &QAction::triggered, this, &MainWindow::onOpenProject);
    connect(m_actSaveProject, &QAction::triggered, this, &MainWindow::onSaveProject);
    connect(m_actSaveProjectAs, &QAction::triggered, this, &MainWindow::onSaveProjectAs);
    connect(m_actExportGCode, &QAction::triggered, this, &MainWindow::onExportGCode);
    connect(m_actExit, &QAction::triggered, this, &QWidget::close);
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
    // 函数说明：connect负责当前模块的核心处理。
    connect(this, &MainWindow::activeRegionChanged, this,
            [this](FaceRegion) { updateDesignWorkflowSummary(); });

    connect(m_simCtrl, &SimulationController::toolPathReady,
            m_simViewport, &ViewportWidget::setToolPath);
    connect(m_simCtrl, &SimulationController::toolPathReady, this,
            [this](const QVector<QVector3D> &path, const QVector<bool> &rapid, const QVector<QVector3D> &starts) {
        if (m_externalGCodePath.isEmpty()) m_viewport->setToolPath(path, rapid, starts);
        refreshOperationPathVisibility();
    });
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
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_simCtrl, &SimulationController::checkResultChanged, this, [this]() {
        const auto &check = m_simCtrl->checkResult();
        if (!check.message.isEmpty()) m_bottomBar->setStatus(check.message);
        updateProgramActionAvailability();
    });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_gcodeEditor, &GCodeEditor::lineSelected, this, [this](int line) {
        if (m_simCtrl->isRunning()) {
            return;
        }
        if (!ensureSimulationLoaded()) return;
        if (!m_externalGCodePath.isEmpty()) {
            const auto &section = m_externalPrograms.at(m_externalProgramCombo->currentIndex());
            if (line < section.firstLine || line > section.lastLine) return;
        }
        m_simCtrl->seekToGCodeLine(line);
    });
    connect(m_operationPanel, &OperationListPanel::currentOperationChanged,
            this, &MainWindow::jumpToGeneratedOperation);
    const auto showOperationGeometry = [this]() {
        const QString id = m_operationPanel->currentOperationId();
        m_viewport->clearHighlight();
        m_viewport->clearContourChoicePreview();
        if (!m_operationPanel->operationVisible(id)) return;
        for (const auto &operation : m_operationPanel->operations()) {
            if (operation.id != id) continue;
            if (operation.opType == OperationType::Hole)
                m_viewport->setHighlightedFaces(operation.holeFeature.faceIndices);
            else
                m_viewport->setContourChoicePreview(contourPreviewPoints(operation.contourFeature),
                    operation.contourFeature.subType != QStringLiteral("open_contour"), 0);
            break;
        }
    };
    connect(m_operationPanel, &OperationListPanel::currentOperationChanged, this,
            [showOperationGeometry](int) { showOperationGeometry(); });
    connect(m_operationPanel, &OperationListPanel::operationVisibilityChanged, this,
            [this, showOperationGeometry](const QString &, bool) {
                showOperationGeometry();
                refreshOperationPathVisibility();
                m_bottomBar->setStatus(tr("已更新最终刀路显示。合并孔批次仅在关联工序全部隐藏时隐藏；显示不改变加工启用状态。"));
            });
    connect(m_operationPanel, &OperationListPanel::editOperationRequested, this,
            [this](const QString &id) {
        if (m_parameterDialog->isVisible() || (m_geometryDialog && m_geometryDialog->isVisible()) || m_operationEditDialog) return;
        for (const auto &operation : m_operationPanel->operations()) {
            if (operation.id != id) continue;
            const MachiningOperation original = operation;
            auto *dialog = new OperationEditDialog(operation, this);
            m_operationEditDialog = dialog;
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            connect(dialog, &QDialog::accepted, this, [this, dialog, original]() {
                auto operations = m_operationPanel->operations();
                for (auto &item : operations) {
                    if (item.id != original.id) continue;
                    if (item.params.values != original.params.values || item.toolId != original.toolId) {
                        QMetaObject::invokeMethod(this, [this]() {
                            onErrorOccurred(tr("工序已被其他操作修改，请重新打开编辑窗口。"));
                        }, Qt::QueuedConnection);
                        return;
                    }
                    item.params = dialog->params();
                    if (item.opType != OperationType::Hole)
                        item.contourFeature=applyMachiningParameters(item.contourFeature,item.params);
                    item.toolId = dialog->toolId();
                    item.markToolpathStale(tr("工序参数已修改，请重新计算"));
                    m_operationPanel->setOperations(operations);
                    emit m_operationPanel->operationsEdited(operations);
                    m_operationPanel->selectOperationById(original.id);
                    break;
                }
            });
            dialog->show();
            break;
        }
    });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_operationPanel, &OperationListPanel::batchEditOperationsRequested, this,
            [this](const QStringList &ids) {
        if (m_parameterDialog->isVisible() || (m_geometryDialog && m_geometryDialog->isVisible()) || m_operationEditDialog) return;
        auto *project = AppController::instance().projectManager();
        QList<MachiningOperation> originals;
        for (const auto &operation : m_operationPanel->operations())
            if (ids.contains(operation.id)) originals.append(operation);
        if (originals.isEmpty() || originals.size() != ids.size()) return;
        const QString source = project->sourceFileFingerprint(), setup = project->setupFingerprint();
        auto *dialog = new OperationBatchEditDialog(originals, this);
        m_operationEditDialog = dialog;
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &QDialog::accepted, this, [this, dialog, originals, ids, source, setup]() {
            auto *project = AppController::instance().projectManager();
            auto operations = m_operationPanel->operations();
            QList<MachiningOperation> current;
            for (const auto &operation : operations)
                if (ids.contains(operation.id)) current.append(operation);
            bool unchanged = current.size() == originals.size() &&
                project->sourceFileFingerprint() == source && project->setupFingerprint() == setup &&
                ProgramSnapshotFingerprint::calculate(current, ids) == ProgramSnapshotFingerprint::calculate(originals, ids);
            for (const auto &original : originals) {
                const auto found = std::find_if(current.cbegin(), current.cend(), [&](const MachiningOperation &item) { return item.id == original.id; });
                unchanged &= found != current.cend() && found->enabled == original.enabled;
            }
            if (!unchanged) {
                // The accepted signal is still on the dialog's stack. A nested
                // warning loop could process its deferred deletion before it returns.
                QMetaObject::invokeMethod(this, [this]() {
                    onErrorOccurred(tr("工序或加工设置已改变，批量修改未提交，请重新打开编辑窗口。"));
                }, Qt::QueuedConnection);
                return;
            }
            for (const auto &replacement : dialog->operations())
                for (auto &operation : operations)
                    if (operation.id == replacement.id) operation = replacement;
            m_operationPanel->setOperations(operations);
            emit m_operationPanel->operationsEdited(operations);
            m_operationPanel->selectOperationById(ids.first());
        });
        dialog->show();
    });
    connect(m_operationPanel, &OperationListPanel::operationsEdited,
            this, [this](const QList<MachiningOperation> &operations) {
        ProjectManager *project = AppController::instance().projectManager();
        if (project) {
            const QScopedValueRollback<bool> syncing(m_syncingOperationsFromPanel,true);
            project->setOperations(operations);
        }
        syncProgramList();
        updateProgramActionAvailability();
    });
    connect(m_bottomBar, &BottomBar::simulationSpeedChanged,
            m_simCtrl, &SimulationController::setSpeed);
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_simCtrl, &SimulationController::simulationFinished, this, [this]() {
        m_simulationReviewState = SimulationReviewState::Completed;
        updateProgramReviewSummary();
        const auto &check = m_simCtrl->checkResult();
        m_bottomBar->setStatus(check.message.isEmpty()
            ? tr("Trajectory playback ended. Geometric screening does not verify the full tool envelope.")
            : tr("Trajectory playback ended. %1").arg(check.message));
        m_bottomBar->showProgress(false);
    });

    AppController &ctrl = AppController::instance();
    connect(&ctrl, &AppController::stepImported, this, &MainWindow::onStepImported);
    connect(&ctrl, &AppController::gcodeReady, this, &MainWindow::onGCodeReady);
    connect(&ctrl, &AppController::statusMessage, this, &MainWindow::onStatusMessage);
    connect(&ctrl, &AppController::errorOccurred, this, &MainWindow::onErrorOccurred);
#ifdef CNEXT_ENABLE_OCC
    // 函数说明：connect负责当前模块的核心处理。
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
            if (m_externalGCodePath.isEmpty()) m_simViewport->setMesh(pm->mesh());
            m_featurePanel->setFeatures(pm->features());
            updateDesignWorkflowSummary();
        });
        connect(project, &ProjectManager::operationsChanged, this, [this]() {
            ProjectManager *pm = AppController::instance().projectManager();
            // A checkbox edit already changed this panel. Rebuilding here deletes
            // its active item before Qt's itemChanged stack has returned.
            if (pm && !m_syncingOperationsFromPanel) {
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

    // 函数说明：connect负责当前模块的核心处理。
    connect(m_gcodeEditor, &QPlainTextEdit::textChanged, this, [this]() {
        m_simCtrl->loadGCode(QString());
        m_simViewport->clearToolPath();
        m_bottomBar->showProgress(false);
        m_simulationReviewState = SimulationReviewState::Unavailable;
        updateProgramActionAvailability();
    });
    updateProgramActionAvailability();
    updateDesignWorkflowSummary();

    // 函数说明：connect负责当前模块的核心处理。
    connect(m_featurePanel, &FeatureListPanel::featureSelected, this, [this](int idx) {
        if (m_parameterDialog->isVisible()) return;
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

    // 函数说明：connect负责当前模块的核心处理。
    connect(m_featurePanel, &FeatureListPanel::featureGroupSelected, this, [this](const QVector<int> &indices) {
        if (m_parameterDialog->isVisible()) return;
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
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_featurePanel, &FeatureListPanel::checkedFeaturesChanged, this, [this](const QVector<int> &indices) {
        if (m_parameterDialog->isVisible()) return;
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

    // 函数说明：connect负责当前模块的核心处理。
    connect(m_viewport, &ViewportWidget::facePointPicked, this, [this](int faceIndex, const QVector3D &hitPoint) {
        if (m_parameterDialog->isVisible() || m_operationEditDialog) return;
        if (m_geometryDialog && m_geometryDialog->isVisible()) {
            auto *project = AppController::instance().projectManager();
            for (const auto &feature : project->features()) {
                if (!feature.faceIndices.contains(faceIndex)) continue;
                if (auto *holes = dynamic_cast<HoleSelectionDialog *>(m_geometryDialog.data())) {
                    if (isHoleFeature(feature)) holes->toggleGeometry(stableFeatureId(feature));
                }
                break;
            }
            return;
        }
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

    connect(m_viewport, &ViewportWidget::topologyPicked, this,
            [this](const QString &id, int kind, const QVector3D &point, double parameter) {
        if (!m_geometryDialog || !m_geometryDialog->isVisible()) return;
        if (auto *chain = dynamic_cast<SelectionChainDialog *>(m_geometryDialog.data())) {
            QVector<int> faces;
            if (kind == static_cast<int>(ViewportWidget::TopologyPickMode::Face)) {
                bool ok = false;
                const int faceIndex = id.section(QLatin1Char(':'), 1, 1).toInt(&ok);
                if (ok) faces.append(faceIndex);
            }
            if (!faces.isEmpty()) m_viewport->setHighlightedFaces(faces);
            chain->selectTopology(id, kind, point, parameter);
            m_viewport->setFocus(Qt::MouseFocusReason);
        }
    });
    connect(m_viewport, &ViewportWidget::topologyHovered, this,
            [this](const QString &id, int kind, const QVector3D &point, double parameter) {
        if (!m_geometryDialog || !m_geometryDialog->isVisible()) return;
        if (auto *chain = dynamic_cast<SelectionChainDialog *>(m_geometryDialog.data()))
            chain->previewTopology(id, kind, point, parameter);
    });

    // 函数说明：connect负责当前模块的核心处理。
    connect(m_strategyPanel, &StrategyPanel::operationProposalConfirmed,
            this, [this](const OperationProposal &proposal) {
        if (proposal.kind != OperationProposalKind::Hole) {
            return;
        }
        auto *workflowProject = AppController::instance().projectManager();
        if (!workflowProject || workflowProject->sourceFileFingerprint() != m_workflowSource ||
            workflowProject->setupFingerprint() != m_workflowSetup) {
            onErrorOccurred(tr("模型或加工设置已改变，请重新选择几何。"));
            return;
        }
        const HoleFeature &feature = proposal.holeFeature;
        if (!m_autoHoleFeatureFingerprint.isEmpty()
            && autoHoleFeatureFingerprint(workflowProject->features()) != m_autoHoleFeatureFingerprint) {
            onErrorOccurred(tr("识别孔已改变，请重新预览孔建议。"));return;
        }
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
        selectedHoles = m_workflowHoles;
        if (selectedHoles.isEmpty()) {
            onErrorOccurred(tr("请先选择孔几何。"));
            return;
        }
        if (!selectedHoles.isEmpty()) {
            selectedFeature = selectedHoles.first();
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
        if (batchHoles && stratId != QStringLiteral("hole_spot") &&
            stratId != QStringLiteral("hole_g81")) {
            if (!holeFeaturesShareGroup(selectedHoles)) {
                onErrorOccurred(zh
                    ? QStringLiteral("当前批量选择包含不同孔型或尺寸。除定点钻和 G81 外，请一次只勾选一个孔分类组。")
                    : QStringLiteral("The batch contains different hole types or dimensions. Except for spot drilling and G81, select one hole-classification group at a time."));
                return;
            }
        }
        if (batchHoles || stratId == QStringLiteral("hole_spot") ||
            stratId == QStringLiteral("hole_g81")) {
            for (const HoleFeature &hole : selectedHoles) {
                const QString holeError = holeSetupRestrictionMessage(hole, zh);
                if (!holeError.isEmpty()) {
                    onErrorOccurred(holeError);
                    return;
                }
            }
        }
        if (batchHoles || stratId == QStringLiteral("hole_spot") ||
            stratId == QStringLiteral("hole_g81")) {
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
            stratId == "hole_chamfer" ? holeCuttingDepth(stratId, selectedFeature, params, tool)
                : params.get(QStringLiteral("depth"), selectedFeature.depth));
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
            if (!ProcessDesignWorkspaceFacade::candidateCanBeConfirmed(oneProposal)) {
                onErrorOccurred(zh ? QStringLiteral("工艺候选缺少策略或刀具，不能确认工序。")
                                   : QStringLiteral("The process candidate has no valid strategy or tool."));
                return;
            }
            const OperationConfirmationResult confirmation =
                confirmOperationProposal(
                    oneProposal, OperationConfirmationIntent::ExplicitUser);
            if (!confirmation.ok) {
                onErrorOccurred(confirmation.error);
                return;
            }
            confirmedOperations.append(confirmation.operation);
        }

        const auto origin = machiningOrigin(AppController::instance().projectManager());
        auto workHoles = selectedHoles;
        for (auto &hole : workHoles) hole = inWorkCoordinates(hole,origin);
        const auto checkedHoles=batchHoles ? workHoles : QVector<HoleFeature>{inWorkCoordinates(selectedFeature,origin)};
        for(const auto &hole:checkedHoles){const auto error=workHeightError(hole,params,stratId);if(!error.isEmpty()){onErrorOccurred(error);return;}}
        const ToolpathResult result = batchHoles
            ? s->generate(workHoles, tool, params)
            : s->generate(inWorkCoordinates(selectedFeature,origin), tool, params);
        if (!result.ok || result.gcode.isEmpty()) {
            onErrorOccurred(result.errorMsg.isEmpty() ? tr("G-code generation failed.") : result.errorMsg);
            return;
        }

        for (MachiningOperation &operation : confirmedOperations) {
            operation.markToolpathValid();
        }
        const QStringList addedIds =
            m_operationPanel->addConfirmedOperations(confirmedOperations);
        if (!addedIds.isEmpty()) {
            m_parameterDialog->hide();
            m_operationPanel->selectOperationById(addedIds.constLast());
        }
        m_bottomBar->setStatus(
            zh ? QStringLiteral("已确认 %1 道孔工序；请从“已确认工序”生成程序。")
                     .arg(addedIds.size())
               : QStringLiteral("Confirmed %1 hole operation(s). Generate the program from Confirmed Operations.")
                     .arg(addedIds.size()));
    });

    // 函数说明：connect负责当前模块的核心处理。
    connect(m_strategyPanel, &StrategyPanel::operationProposalConfirmed,
            this, [this](const OperationProposal &proposal) {
        if (proposal.kind != OperationProposalKind::Contour) {
            return;
        }
        auto *workflowProject = AppController::instance().projectManager();
        if (!workflowProject || workflowProject->sourceFileFingerprint() != m_workflowSource ||
            workflowProject->setupFingerprint() != m_workflowSetup) {
            onErrorOccurred(tr("模型或加工设置已改变，请重新选择几何。"));
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
        ContourFeature requestedFeature = applyMachiningParameters(feature, params);
        if (isSlotMillingStrategy(resolvedStrategyId)) {
            logSlotContourFeature("requested-after-ui-override", requestedFeature, params);
        }
        if (project) {
            const auto &features = project->features();
            const int currentIndex = m_workflowFeatureIndex;
            int slotIndex = -1;
            if (currentIndex >= 0 && currentIndex < features.size() && isSlotFeature(features[currentIndex])) {
                slotIndex = currentIndex;
            } else if (isSlotMillingStrategy(resolvedStrategyId)) {
                onErrorOccurred(tr("所选范围没有已确认的槽加工映射，请重新选择几何。"));
                return;
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
                    resolveRecognizedContour(features[slotIndex], &project->mesh()),
                    features[slotIndex],
                    params);
                logSlotContourFeature("requested-after-recognized-slot", requestedFeature, params);
            }
        }
        StrategyParams generationParams = isSlotMillingStrategy(resolvedStrategyId)
                                              ? slotMachiningParamsWithoutGeometry(params)
                                              : params;
        if(isPlanarMillingStrategy(resolvedStrategyId)) {
            const auto origin=machiningOrigin(project);
            const auto workFeature=inWorkCoordinates(requestedFeature,origin);
            QString error=planarMachiningError(workFeature,tool,generationParams);
            if(error.isEmpty()) error=project && project->stockDefinition().confirmed
                ? planarStockEntryError(workFeature,tool,generationParams,
                    inWorkCoordinates(machiningStockBounds(project),origin))
                : tr("请先确认毛坯。");
            if(!error.isEmpty()){onErrorOccurred(error);return;}
        }
        const ToolCompatibilityReport toolReport = reviewToolCompatibility(
            resolvedStrategyId,
            tool,
            requestedFeature,
            zh,
            generationParams.get(QStringLiteral("depth"), requestedFeature.depth)
                - (isPlanarMillingStrategy(resolvedStrategyId) ? generationParams.get("stockToLeave",0) : 0));
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
        if (m_workflowSelections.size() > 1) {
            if (!project || m_workflowFeatureIndices.size() != m_workflowSelections.size()) {
                onErrorOccurred(tr("整组选区已失效，请重新选择。")); return;
            }
            QVector<MachiningFeature> selectedRegions;
            for (int index : m_workflowFeatureIndices)
                if (index >= 0 && index < project->features().size())
                    selectedRegions.append(project->features().at(index));
            if (isSlotMillingStrategy(resolvedStrategyId) && !contourFeaturesShareGroup(selectedRegions)) {
                onErrorOccurred(tr("批量槽加工包含不同类型、尺寸、深度或角度，请按槽分组分别确认。"));
                return;
            }
            for (int index : m_workflowFeatureIndices) {
                if (index < 0 || index >= project->features().size()) {
                    onErrorOccurred(tr("整组选区已失效，请重新选择。")); return;
                }
                const auto &source = project->features().at(index);
                contourBatch.append(applySlotOverridesFromParams(
                    resolveRecognizedContour(source, &project->mesh()), source, params));
            }
        }
        if (isSlotMillingStrategy(resolvedStrategyId) && !m_workflowSelection.explicitUserSelection) {
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
                            resolveRecognizedContour(features[index], &project->mesh()),
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
        QList<SelectionChain> confirmedWorkflowSelections = m_workflowSelections.size() > 1
            ? m_workflowSelections : QList<SelectionChain>{m_workflowSelection};
        if (isManualContourChoiceStrategy(resolvedStrategyId)) {
            SketchSelectionBinding sketchChoiceSource;
            if (project->sourceType()==ProjectSourceType::Sketch) {
                StrategyParams bindingParams=generationParams;
                bindingParams.set("compensation",0);
                const int step=m_workflowSelection.orderedTopologyRefs.first().forward?1:-1;
                bindingParams.set("camOffsetSide",-SketchClearance::convexOrientation(project->sketchDocument().points)*step);
                sketchChoiceSource=bindSketchSelection(project->sketchDocument(),m_workflowSelection,
                    bindingParams,project->setupFingerprint(),project->workOffset());
                if(!sketchChoiceSource.error.isEmpty()){onErrorOccurred(sketchChoiceSource.error);return;}
            }
            for (int i = 0; i < contourBatch.size(); ++i)
                contourBatch[i].points = m_workflowSelections.size() > 1
                    ? m_workflowRegionPoints.at(i) : m_workflowPoints;
            ContourMachiningChoiceDialog choiceDialog(this);
            choiceDialog.setChineseUi(zh);
            choiceDialog.setContext(resolvedStrategyId,
                                    contourBatch.first(),
                                    generationParams, m_workflowSelection.machiningSide);
            if (resolvedStrategyId == QStringLiteral("mill_closed_contour")) {
                QVector<int> allowedStarts;
                for (int index = 0; index < contourBatch.first().points.size(); ++index) {
                    bool valid = true;
                    for (int region = 0; region < contourBatch.size() && valid; ++region) {
                        for (auto direction : {ContourTraversalDirection::Forward, ContourTraversalDirection::Reverse}) {
                            auto selection = confirmedWorkflowSelections.at(region);
                            ContourMachiningChoice choice;
                            choice.startPointIndex = index;
                            choice.direction = direction;
                            valid = bindContourMachiningChoice(selection, contourBatch.at(region).points,
                                                               choice, project->mesh());
                            if (!valid) break;
                        }
                    }
                    if (valid) allowedStarts.append(index);
                }
                choiceDialog.setAllowedStartIndices(allowedStarts);
            }
            const ContourFeature previewSource = contourBatch.first();
            choiceDialog.setChoiceChangedCallback(
                [this, previewSource](const ContourMachiningChoice &choice) {
                    ContourFeature preview = previewSource;
                    preview.points = contourPreviewPoints(previewSource);
                    StrategyParams previewParams;
                    applyContourMachiningChoice(preview, previewParams, choice);
                    const int cutterSide = previewParams.get("compensation") == 0
                        ? int(previewParams.get("camOffsetSide", 1))
                        : int(previewParams.get("compensation"));
                    m_viewport->setContourChoicePreview(preview.points,
                                                        choice.closedContour,
                                                        cutterSide);
                });
            m_parameterDialog->hide();
            const int choiceResult = choiceDialog.exec();
            m_parameterDialog->show();
            m_viewport->clearContourChoicePreview();
            if (choiceResult != QDialog::Accepted) {
                m_bottomBar->setStatus(
                    zh ? QStringLiteral("已取消轮廓加工确认。")
                       : QStringLiteral("Contour machining confirmation canceled."));
                return;
            }
            for (int region = 0; region < contourBatch.size(); ++region) {
                if (!bindContourMachiningChoice(confirmedWorkflowSelections[region],
                        contourBatch.at(region).points, choiceDialog.choice(), project->mesh())) {
                    onErrorOccurred(tr("轮廓起点无法唯一绑定当前选择，请重新选择。"));
                    return;
                }
                applyContourMachiningChoice(contourBatch[region], generationParams, choiceDialog.choice());
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
        for (int regionIndex = 0; regionIndex < contourBatch.size(); ++regionIndex) {
            const ContourFeature &oneFeature = contourBatch.at(regionIndex);
            OperationProposal oneProposal = proposal;
            oneProposal.strategyId = resolvedStrategyId;
            oneProposal.params = generationParams;
            oneProposal.contourFeature = oneFeature;
            oneProposal.selectionEvidence = confirmedWorkflowSelections.size() > 1
                ? confirmedWorkflowSelections.at(regionIndex) : confirmedWorkflowSelections.first();
            if (!oneProposal.selectionEvidence.explicitUserSelection) {
                onErrorOccurred(tr("请先完成几何选择。"));
                return;
            }
            confirmedProposals.append(oneProposal);
        }
        const OperationConfirmationContext confirmationContext{
            project ? project->sourceFileFingerprint() : QString(),
            project ? project->setupFingerprint() : QString(),
            currentWorkOffset(), project ? &project->mesh() : nullptr,
            project && project->sourceType()==ProjectSourceType::Sketch ? &project->sketchDocument() : nullptr};
        for (const OperationProposal &oneProposal : confirmedProposals) {
            if (isPlanarMillingStrategy(resolvedStrategyId)) {
                const auto origin = machiningOrigin(project);
                const auto feature = inWorkCoordinates(oneProposal.contourFeature, origin);
                QString error = planarMachiningError(feature, tool, oneProposal.params);
                if (error.isEmpty()) error = project && project->stockDefinition().confirmed
                    ? planarStockEntryError(feature, tool, oneProposal.params,
                        inWorkCoordinates(machiningStockBounds(project), origin))
                    : tr("请先确认毛坯。");
                if (!error.isEmpty()) { onErrorOccurred(error); return; }
            }
            const QString oneSlotError = slotSetupRestrictionMessage(oneProposal.contourFeature,
                                                                     resolvedStrategyId,
                                                                     tool,
                                                                     oneProposal.params,
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
            if (!ProcessDesignWorkspaceFacade::candidateCanBeConfirmed(oneProposal)) {
                onErrorOccurred(isChineseUi()
                                    ? QStringLiteral("工艺候选缺少策略或刀具，不能确认工序。")
                                    : QStringLiteral("The process candidate has no valid strategy or tool."));
                return;
            }
            const OperationConfirmationResult confirmation =
                confirmOperationProposal(
                    oneProposal,
                    OperationConfirmationIntent::ExplicitUser,
                    confirmationContext);
            if (!confirmation.ok) {
                onErrorOccurred(confirmation.error);
                return;
            }
            confirmedOperations.append(confirmation.operation);
        }

        if (m_workflowSelections.size() > 1) {
            const QString groupId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            for (auto &operation : confirmedOperations) operation.selectionGroupId = groupId;
        }
        const MillingBatchCommitResult batch = validateAndCommitMillingBatch(confirmedOperations,
            *s, tool, [this](const QList<MachiningOperation> &operations) {
                return m_operationPanel->addConfirmedOperations(operations);
            }, machiningOrigin(project));
        if (!batch.ok) {
            onErrorOccurred(batch.error);
            return;
        }
        m_parameterDialog->hide();
        const QStringList addedIds = batch.operationIds;
        if (!addedIds.isEmpty()) {
            m_operationPanel->selectOperationById(addedIds.constLast());
        }
        m_bottomBar->setStatus(
            zh ? QStringLiteral("已确认 %1 道铣削工序；请从“已确认工序”生成程序。")
                     .arg(addedIds.size())
               : QStringLiteral("Confirmed %1 milling operation(s). Generate the program from Confirmed Operations.")
                     .arg(addedIds.size()));
    });

    // 函数说明：connect负责当前模块的核心处理。
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
            if (!ProgramVerificationWorkspaceFacade::canGenerate(op)) {
                onErrorOccurred(QStringLiteral("Operation %1: toolpath is not valid or operation is disabled.")
                                    .arg(opIndex + 1));
                return;
            }
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
        snapshotOptions.sourceFingerprint = projectManager->sourceFileFingerprint();
        snapshotOptions.setupFingerprint = projectManager->setupFingerprint();
        snapshotOptions.modelOrigin = machiningOrigin(projectManager);
        snapshotOptions.sourceMesh = &projectManager->mesh();
        snapshotOptions.stockConfirmed = projectManager->stockDefinition().confirmed;
        snapshotOptions.stockBounds = machiningStockBounds(projectManager);
        snapshotOptions.sourceType = projectManager->sourceType();
        if(projectManager->sourceType()==ProjectSourceType::Sketch)
            snapshotOptions.sketchContext=currentSketchContext(*projectManager,operations,
                [](int id){return ToolLibrary::instance().tool(id);});

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

    // 函数说明：connect负责当前模块的核心处理。
    connect(m_operationPanel, &OperationListPanel::recalculateRequested,
            this, [this](const QList<MachiningOperation> &operations) {
        const bool zh = isChineseUi();
        int succeeded = 0;
        int failed = 0;
        for (const MachiningOperation &operation : operations) {
            std::shared_ptr<StrategyBase> strategy =
                StrategyFactory::instance().strategy(operation.strategyId);
            const ToolEntry tool = ToolLibrary::instance().tool(operation.toolId);
            if (!strategy || tool.id <= 0 || tool.diameter <= 0.0) {
                m_operationPanel->setToolpathResult(
                    operation.id, false,
                    zh ? QStringLiteral("策略或刀具不可用")
                       : QStringLiteral("Strategy or tool is unavailable"));
                ++failed;
                continue;
            }

            auto feature=applyMachiningParameters(operation.contourFeature,operation.params);
            if(operation.opType!=OperationType::Hole){
                const auto *project=AppController::instance().projectManager();
                QString error;
                if(operation.selectionEvidence.explicitUserSelection && project){
                    const auto access=evaluateSelectionAccess(operation.selectionEvidence,project->sourceFileFingerprint(),project->setupFingerprint(),
                        currentWorkOffset(),machiningRequirements(operation.strategyId).closedBoundary);
                    if(!access.ok())error=access.issueCode;
                }
                if(error.isEmpty())error=contourSideOperationError(operation.selectionEvidence,feature,operation.params,operation.strategyId);
                if(error.isEmpty())error=pocketOpeningOperationError(operation.selectionEvidence,feature,operation.params,operation.strategyId,project?&project->mesh():nullptr);
                if(error.isEmpty())error=slopedFloorOperationError(operation.selectionEvidence,feature,operation.strategyId,project?&project->mesh():nullptr);
                if(!error.isEmpty()){m_operationPanel->setToolpathResult(operation.id,false,error);++failed;continue;}
            }
            const StrategyParams params = isSlotMillingStrategy(operation.strategyId)
                ? slotMachiningParamsWithoutGeometry(operation.params)
                : operation.params;
            const auto *project=AppController::instance().projectManager();
            const auto origin=machiningOrigin(project);
            if(project && project->sourceType()==ProjectSourceType::Sketch) {
                const auto context=currentSketchContext(*project,{operation},[](int id){return ToolLibrary::instance().tool(id);});
                if(!context.error.isEmpty()){m_operationPanel->setToolpathResult(operation.id,false,context.error);++failed;continue;}
                const auto binding=bindSketchSelection(project->sketchDocument(),operation.selectionEvidence,operation.params,
                                                       project->setupFingerprint(),project->workOffset());
                if(!binding.error.isEmpty()){m_operationPanel->setToolpathResult(operation.id,false,binding.error);++failed;continue;}
                feature.precisePoints=binding.points;
                feature.center.setZ(project->sketchDocument().planeZ);
            }
            const auto workFeature=inWorkCoordinates(feature,origin);
            const auto heightError=operation.opType==OperationType::Hole
                ? workHeightError(inWorkCoordinates(operation.holeFeature,origin),params,operation.strategyId)
                : workHeightError(workFeature,params);
            if(!heightError.isEmpty()){m_operationPanel->setToolpathResult(operation.id,false,heightError);++failed;continue;}
            if(isPlanarMillingStrategy(operation.strategyId) && project) {
                const auto error=planarStockEntryError(workFeature,tool,params,
                    inWorkCoordinates(machiningStockBounds(project),origin));
                if(!error.isEmpty()){m_operationPanel->setToolpathResult(operation.id,false,error);++failed;continue;}
            }
            const ToolpathResult result = operation.opType == OperationType::Hole
                ? strategy->generate(inWorkCoordinates(operation.holeFeature,origin), tool, params)
                : strategy->generate(workFeature, tool, params);
            if (!result.ok || result.gcode.isEmpty()) {
                m_operationPanel->setToolpathResult(
                    operation.id, false,
                    result.errorMsg.isEmpty()
                        ? (zh ? QStringLiteral("刀路重新生成失败")
                              : QStringLiteral("Toolpath recalculation failed"))
                        : result.errorMsg);
                ++failed;
                continue;
            }

            m_operationPanel->setToolpathResult(operation.id, true);
            ++succeeded;
        }
        m_bottomBar->setStatus(
            zh ? QStringLiteral("刀路重新生成完成：成功 %1，道失败 %2。")
                     .arg(succeeded)
                     .arg(failed)
               : QStringLiteral("Toolpath recalculation finished: %1 succeeded, %2 failed.")
                     .arg(succeeded)
                     .arg(failed));
    });

    // 函数说明：connect负责当前模块的核心处理。
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

bool MainWindow::loadExternalGCodeFile(const QString &filePath, QString *error)
{
    if (error) error->clear();
    if (filePath.isEmpty()) return false;
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return fail(tr("无法读取文件：%1").arg(file.errorString()));
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    QString text = stream.readAll();
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    if (stream.status() != QTextStream::Ok || file.error() != QFile::NoError)
        return fail(tr("读取文件失败，原程序保持不变。"));
    if (text.contains(QChar::ReplacementCharacter) || text.contains(QChar(0)))
        return fail(tr("请使用 UTF-8/ASCII 或带 BOM 的 Unicode 文本文件。"));
    if (text.trimmed().isEmpty()) return fail(tr("文件为空，原程序保持不变。"));
    QString unsupported;
    const auto programs = SimulationController::externalPrograms(text, &unsupported);
    if (!unsupported.isEmpty()) return fail(unsupported);

    // Everything above is read-only: a failed import must preserve even playback.
    syncCurrentProgramSnapshot();
    m_externalGCodePath = QFileInfo(filePath).absoluteFilePath();
    m_externalGCodeText = text;
    m_externalPrograms = programs;
    {
        const QSignalBlocker blocker(m_externalProgramCombo);
        m_externalProgramCombo->clear();
        for (const auto &section : programs) {
            m_externalProgramCombo->addItem(section.label);
            m_externalProgramCombo->setItemData(m_externalProgramCombo->count() - 1,
                tr("原文件行 %1–%2").arg(section.firstLine + 1).arg(section.lastLine + 1), Qt::ToolTipRole);
        }
        m_externalProgramCombo->setCurrentIndex(0);
    }
    m_currentProgramId.clear();
    AppController::instance().projectManager()->setCurrentProgramId(QString());
    if (m_programList) {
        const QSignalBlocker blocker(m_programList);
        m_programList->setCurrentRow(-1);
    }
    if (m_macroLibraryEditor) m_macroLibraryEditor->clear();
    m_simViewport->clearToolPath();
    m_simViewport->setToolModelPath(QString());
    m_simViewport->setToolGeometryVisible(false);
    m_simViewport->setToolDiameter(0.0);
    m_simViewport->setMesh(MeshData{});
    m_simCtrl->setMesh(MeshData{});
    m_gcodeEditor->setGCode(text);
    selectExternalProgram(0);
    m_simulationReviewState = SimulationReviewState::Ready;
    m_bottomBar->showProgress(false);
    m_pageNav->setCurrentRow(1);
    m_simViewport->fitAll();
    updateProgramActionAvailability();
    m_bottomBar->setStatus(tr("已导入 %1，仅用于轨迹仿真。").arg(QFileInfo(filePath).fileName()));
    return true;
}

void MainWindow::onImportGCode()
{
    const QString path = QFileDialog::getOpenFileName(this,
        isChineseUi() ? QStringLiteral("导入 G 代码") : QStringLiteral("Import G-code"), QString(),
        tr("G-code (*.nc *.tap *.gcode *.cnc *.txt);;All files (*)"));
    QString error;
    if (!loadExternalGCodeFile(path, &error) && !error.isEmpty())
        OperationMessageBox::warning(this, tr("G 代码导入失败"), error);
}

void MainWindow::selectExternalProgram(int index)
{
    if (m_externalGCodePath.isEmpty() || index < 0 || index >= m_externalPrograms.size()) return;
    if (m_gcodeEditor->toPlainText() != m_externalGCodeText) return;
    const auto &section = m_externalPrograms.at(index);
    m_externalProgramCombo->setToolTip(tr("%1；原文件行 %2–%3")
        .arg(section.label).arg(section.firstLine + 1).arg(section.lastLine + 1));
    m_simCtrl->loadGCode(section.text, false, section.firstLine);
    m_simulationReviewState = SimulationReviewState::Ready;
    m_bottomBar->showProgress(false);
    m_simViewport->fitAll();
    updateProgramActionAvailability();
}

QString MainWindow::currentInternalVerificationText(Coordinate3D *origin) const
{
    *origin = {};
    const QString text = m_gcodeEditor ? m_gcodeEditor->toPlainText() : QString();
    auto *project = AppController::instance().projectManager();
    if (project && !m_currentProgramId.isEmpty()) {
        const auto program = project->programById(m_currentProgramId);
        *origin = program.modelOrigin;
        if (program.gcodeText == text && !program.expandedGcodeText.isEmpty() && program.generationContentMatches()) return program.expandedGcodeText;
    }
    return text;
}

bool MainWindow::ensureSimulationLoaded()
{
    QString text = m_gcodeEditor->toPlainText();
    Coordinate3D modelOrigin;
    if (!m_externalGCodePath.isEmpty()) {
        const int index = m_externalProgramCombo->currentIndex();
        if (text != m_externalGCodeText || index < 0 || index >= m_externalPrograms.size()) {
            m_bottomBar->setStatus(tr("外部原文已改变，请重新导入后仿真。"));
            return false;
        }
        const auto &section = m_externalPrograms.at(index);
        if (m_simCtrl->sourceText() != section.text || m_simCtrl->moves().isEmpty())
            m_simCtrl->loadGCode(section.text, false, section.firstLine);
        return !m_simCtrl->moves().isEmpty();
    }
    text = currentInternalVerificationText(&modelOrigin);
    const auto *project=AppController::instance().projectManager();
    if(project && project->sourceType()==ProjectSourceType::Sketch)
        m_simCtrl->setSketchVerificationContext(programSketchContext(*project,project->programById(m_currentProgramId)));
    if (!m_simCtrl->checkMatches(text, modelOrigin,programSketchContextFingerprint(project,m_currentProgramId)) || m_simCtrl->moves().isEmpty()) {
        m_simCtrl->loadGCode(text, m_externalGCodePath.isEmpty(), 0, modelOrigin);
    }
    return !m_simCtrl->moves().isEmpty();
}

// 函数说明：onImportStep负责当前模块的核心处理。
void MainWindow::onEditSketch()
{
    if (m_parameterDialog->isVisible() || (m_geometryDialog && m_geometryDialog->isVisible()) || m_operationEditDialog) {
        m_bottomBar->setStatus(tr("请先完成或取消当前工序配置，再编辑草图。"));
        return;
    }
    ProjectManager *project = AppController::instance().projectManager();
    if (!project) return;
    const bool editing = project->sourceType() == ProjectSourceType::Sketch;
    if (!editing && !confirmProjectReplacement()) return;
    const QString sourceFingerprint = project->sourceFileFingerprint();
    SketchDocument draft = editing ? project->sketchDocument() : SketchDocument();
    if (!editing) draft.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    SketchEditorDialog dialog(draft, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    if ((!editing && sourceFingerprint != project->sourceFileFingerprint()) ||
        !(editing ? project->updateSketchDocument(dialog.document(), sourceFingerprint, &error)
                  : project->createSketchProject(dialog.document(), &error))) {
        onErrorOccurred(error.isEmpty() ? tr("项目来源已改变，请重新打开草图。") : error);
        return;
    }
    if (editing && sourceFingerprint == project->sourceFileFingerprint()) {
        if(dialog.document().toJson()!=draft.toJson())m_bottomBar->setStatus(tr("草图标注已更新。"));
        return;
    }
    if (!editing) resetSourceViews();
    if (m_wcsCombo) {
        const QSignalBlocker blocker(m_wcsCombo);
        const int index = m_wcsCombo->findData(project->workOffset());
        m_wcsCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    m_strategyPanel->setWorkOffset(project->workOffset());
    m_settingFrontFace = false;
    m_actSetFrontFace->setChecked(false);
    m_activeRegion = project->activeRegion();
    emit activeRegionChanged(m_activeRegion);
    m_viewport->clearContourChoicePreview();
    m_viewport->clearConfirmedContourPreview();
    m_simViewport->clearContourChoicePreview();
    m_simViewport->clearConfirmedContourPreview();
    m_simCtrl->loadGCode(QString());
    m_simCtrl->setMesh(project->mesh());
    m_viewport->clearToolPath();
    m_simViewport->clearToolPath();
    m_viewport->setMesh(project->mesh());
    m_simViewport->setMesh(project->mesh());
    m_viewport->setStandardView(ViewportWidget::StandardView::Top);
    m_simViewport->setStandardView(ViewportWidget::StandardView::Top);
    syncProgramList();
    updateDesignWorkflowSummary();
    updateProgramActionAvailability();
    m_bottomBar->setStatus(editing ? tr("草图已更新；历史程序已过期，请重新确认原点和毛坯。")
                                  : tr("草图已创建；加工方向固定 +Z，请确认原点和毛坯。"));
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
    if (!confirmProjectReplacement()) return;
    m_bottomBar->setStatus(tr("Importing STEP..."));
    AppController::instance().importStep(path);
}

// 函数说明：onSaveProject负责当前模块的核心处理。
void MainWindow::onSaveProject()
{
    saveCurrentProject();
}

void MainWindow::onSaveProjectAs()
{
    saveCurrentProject(true);
}

bool MainWindow::saveCurrentProject(bool saveAs)
{
    QString path = m_projectFilePath;
    if (saveAs || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, tr("Save Project"), m_projectFilePath,
                                          tr("CNEXT project (*.cnext)"));
    }
    if (path.isEmpty()) return false;
    ProjectManager *project = AppController::instance().projectManager();
    if (project) {
        project->setOperations(m_operationPanel->operations());
        project->setActiveRegion(m_activeRegion);
        project->setWorkOffset(currentWorkOffset());
        syncCurrentProgramSnapshot();
    }
    if (!m_projectCommandController->saveProject(path)) {
        m_bottomBar->setStatus(tr("Unable to save project"));
        return false;
    }
    m_projectFilePath = path;
    return true;
}

bool MainWindow::confirmProjectReplacement()
{
    syncCurrentProgramSnapshot();
    const auto *project = AppController::instance().projectManager();
    if (!project || !project->isModified()) return true;
    const bool zh = isChineseUi();
    QMessageBox prompt(QMessageBox::Question,
        zh ? QStringLiteral("未保存的项目") : QStringLiteral("Unsaved Project"),
        zh ? QStringLiteral("当前项目有未保存的更改。是否先保存？")
           : QStringLiteral("The current project has unsaved changes. Save them first?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    prompt.button(QMessageBox::Save)->setText(zh ? QStringLiteral("保存") : QStringLiteral("Save"));
    prompt.button(QMessageBox::Discard)->setText(zh ? QStringLiteral("不保存") : QStringLiteral("Don't Save"));
    prompt.button(QMessageBox::Cancel)->setText(zh ? QStringLiteral("取消") : QStringLiteral("Cancel"));
    prompt.setDefaultButton(QMessageBox::Save);
    prompt.setEscapeButton(QMessageBox::Cancel);
    const int decision = prompt.exec();
    if (decision == QMessageBox::Discard) return true;
    if (decision == QMessageBox::Save) return saveCurrentProject();
    return false;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmProjectReplacement()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

// 函数说明：onOpenProject负责当前模块的核心处理。
void MainWindow::onOpenProject()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open Project"),
        QString(),
        tr("CNEXT project (*.cnext)"));
    if (!path.isEmpty()) {
        if (!confirmProjectReplacement()) return;
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
        m_projectFilePath = path;
        ProjectManager *project = controller.projectManager();
        if (project) {
            m_externalGCodePath.clear();
    m_externalGCodeText.clear();
    m_externalPrograms.clear();
    if (m_externalProgramCombo) {
        const QSignalBlocker blocker(m_externalProgramCombo);
        m_externalProgramCombo->clear();
        m_externalProgramCombo->hide();
    }
            m_simViewport->setToolGeometryVisible(true);
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
            m_strategyPanel->setWorkOffset(project->workOffset());
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

// 中文说明：导出前再次检查当前程序快照是否有效、安全校验是否通过；
// UI 不绕过服务层直接写出未经验证的 G 代码。
void MainWindow::onExportGCode()
{
    syncCurrentProgramSnapshot();
    if (!validateCurrentGCodeForOutput(tr("export"))) return;

    ProjectManager *project = AppController::instance().projectManager();
    const ProgramEntry program = project && !m_currentProgramId.trimmed().isEmpty()
                                     ? project->programById(m_currentProgramId) : ProgramEntry();
    auto files = program.packageFiles;
    QString mainName = program.mainProgramFileName;
    const bool cq8Package = files.size() == 2 && mainName == QStringLiteral("CQ8_MAIN.NC") &&
        std::any_of(files.cbegin(), files.cend(), [](const ProgramFileEntry &file) {
            return file.kind == QStringLiteral("macro") && file.fileName == QStringLiteral("CQ8_MACROS.NC");
        });
    const bool singleFilePackage = files.size() == 1 && files.first().kind == QStringLiteral("main") &&
        files.first().fileName == mainName;
    QString directory, path;
    if (!files.isEmpty() && !cq8Package && !singleFilePackage) {
        directory = QFileDialog::getExistingDirectory(this, QStringLiteral("导出完整程序包（保留全部配套文件）"));
        if (directory.isEmpty()) return;
    } else {
        QFileDialog dialog(this, cq8Package ? QStringLiteral("另存完整 CQ8 程序包（同时保存主程序和配套宏）")
                                            : QStringLiteral("另存最终 G 代码"));
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setOption(QFileDialog::DontConfirmOverwrite); // Confirm the normalized path and every package file below.
        QStringList filters{QStringLiteral("NC 程序 (*.nc)"), QStringLiteral("文本程序 (*.txt)")};
        const auto *processor = PostProcessorRegistry::instance().get(Settings::instance().postProcessorId());
        const QString nativeExtension = processor ? processor->fileExtension() : QStringLiteral(".nc");
        if (!cq8Package && nativeExtension != QStringLiteral(".nc") && nativeExtension != QStringLiteral(".txt"))
            filters.append(QStringLiteral("后处理原生程序 (*%1)").arg(nativeExtension));
        dialog.setNameFilters(filters);
        dialog.selectFile(cq8Package ? QStringLiteral("CQ8_MAIN.nc") : QStringLiteral("program.nc"));
        dialog.setLabelText(QFileDialog::Accept, QStringLiteral("保存"));
        dialog.setLabelText(QFileDialog::Reject, QStringLiteral("取消"));
        dialog.setLabelText(QFileDialog::FileName, QStringLiteral("文件名："));
        dialog.setLabelText(QFileDialog::FileType, QStringLiteral("文件类型："));
        if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
        path = dialog.selectedFiles().first();
        const QString suffix = dialog.selectedNameFilter() == filters[1] ? QStringLiteral("txt")
            : dialog.selectedNameFilter() == filters[0] ? QStringLiteral("nc") : nativeExtension.mid(1);
        const QFileInfo selected(path);
        if (selected.suffix().compare(suffix, Qt::CaseInsensitive) != 0)
            path = selected.dir().filePath((selected.suffix().isEmpty() ? selected.fileName() : selected.completeBaseName()) + "." + suffix);
        directory = QFileInfo(path).absolutePath();
        if (cq8Package) {
            mainName = QFileInfo(path).fileName();
            QString stem = QFileInfo(path).completeBaseName();
            if (stem.endsWith(QStringLiteral("_MAIN"), Qt::CaseInsensitive)) stem.chop(5);
            for (auto &file : files)
                file.fileName = file.kind == QStringLiteral("main") ? mainName : stem + QStringLiteral("_MACROS.") + suffix;
        } else if (singleFilePackage) {
            mainName = QFileInfo(path).fileName();
            files.first().fileName = mainName;
        }
    }
    QStringList existing;
    if (files.isEmpty()) {
        if (QFileInfo::exists(path)) existing << path;
    } else for (const auto &file : files) {
        const QString target = QDir(directory).filePath(file.fileName);
        if (QFileInfo::exists(target)) existing << target;
    }
    if (!existing.isEmpty()) {
        QMessageBox question(QMessageBox::Question, QStringLiteral("确认覆盖文件"),
            QStringLiteral("以下文件已存在。是否用当前最终程序覆盖？\n\n%1").arg(existing.join('\n')),
            QMessageBox::Yes | QMessageBox::No, this);
        question.setTextFormat(Qt::PlainText);
        question.button(QMessageBox::Yes)->setText(QStringLiteral("覆盖"));
        question.button(QMessageBox::No)->setText(QStringLiteral("取消"));
        question.setDefaultButton(QMessageBox::No);
        if (question.exec() != QMessageBox::Yes) return;
    }
    if (!files.isEmpty()) {
        const auto report = ProgramPackageExporter::exportFiles(directory, mainName, files, cq8Package);
        if (!report.ok) {
            OperationMessageBox::warning(this, QStringLiteral("程序包导出失败"), report.error);
            return;
        }
        m_bottomBar->setStatus(QStringLiteral("已保存完整程序包（%1 个文件）：%2；请保留主程序与配套文件。").arg(report.writtenFiles.size()).arg(directory));
        return;
    }
    QSaveFile file(path);
    const QByteArray bytes = m_gcodeEditor->toPlainText().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        OperationMessageBox::warning(this, QStringLiteral("G 代码保存失败"),
            QStringLiteral("FINAL_PROGRAM_WRITE_FAILED\n%1\n%2").arg(path, file.errorString()));
        return;
    }
    m_bottomBar->setStatus(QStringLiteral("最终 G 代码已保存：%1").arg(path));
}

// 中文说明：发送动作保持人工确认边界，只允许发送当前有效且已审核的快照；
// 本项目不在此处实现实时控制或自动下发。
void MainWindow::onSendToMachine()
{
    syncCurrentProgramSnapshot();
    if (!validateCurrentGCodeForOutput(tr("send to machine"))) {
        return;
    }
    ProjectManager *project = AppController::instance().projectManager();
    const ProgramEntry program = project && !m_currentProgramId.trimmed().isEmpty()
                                     ? project->programById(m_currentProgramId)
                                     : ProgramEntry{};
    if (!MachineOperationWorkspaceFacade::canSend(program)) {
        OperationMessageBox::warning(
            this,
            tr("Program Snapshot is incomplete"),
            tr("Cannot send to the machine because the current program has no confirmed operation provenance. Regenerate it from confirmed operations."));
        return;
    }
    CncSendDialog dlg(m_gcodeEditor->toPlainText(), this);
    dlg.exec();
}

// 函数说明：onResetCamera负责当前模块的核心处理。
void MainWindow::onResetCamera()
{
    m_viewport->resetCamera();
    m_simViewport->resetCamera();
}

// 函数说明：onSimPlay负责当前模块的核心处理。
void MainWindow::onSimPlay()
{
    if (!ensureSimulationLoaded()) {
        m_simulationReviewState = SimulationReviewState::Unavailable;
        updateProgramReviewSummary();
        return;
    }
    m_simCtrl->play();
    if (!m_simCtrl->isRunning()) {
        m_simulationReviewState = SimulationReviewState::Unavailable;
        m_bottomBar->showProgress(false);
        updateProgramReviewSummary();
        m_bottomBar->setStatus(tr("Simulation requires a known tool position."));
        return;
    }
    m_bottomBar->showProgress(true);
    m_simulationReviewState = SimulationReviewState::Running;
    updateProgramReviewSummary();
    m_bottomBar->setStatus(tr("Simulation playing.") + QLatin1Char(' ') + m_simCtrl->checkResult().message);
}

// 函数说明：onSimPause负责当前模块的核心处理。
void MainWindow::onSimPause()
{
    m_simCtrl->pause();
    m_simulationReviewState = m_simCtrl->moves().isEmpty()
        ? SimulationReviewState::Unavailable : SimulationReviewState::Paused;
    updateProgramReviewSummary();
    m_bottomBar->setStatus(tr("Simulation paused.") + QLatin1Char(' ') + m_simCtrl->checkResult().message);
}

// 函数说明：onSimStop负责当前模块的核心处理。
void MainWindow::onSimStop()
{
    m_simCtrl->stop();
    m_simulationReviewState = m_simCtrl->moves().isEmpty()
        ? SimulationReviewState::Unavailable
        : SimulationReviewState::Stopped;
    updateProgramReviewSummary();
    m_bottomBar->showProgress(false);
    m_bottomBar->setStatus(tr("Simulation stopped.") + QLatin1Char(' ') + m_simCtrl->checkResult().message);
}

// 中文说明：设置加工正面会影响特征区域和刀路坐标系，因此必须触发相关
// 工序/程序失效，并要求用户重新确认 Setup。
void MainWindow::showSetupOverview()
{
    if (m_parameterDialog->isVisible() || (m_geometryDialog && m_geometryDialog->isVisible())
        || m_operationEditDialog) {
        m_bottomBar->setStatus(tr("请先完成或取消当前工序配置，再修改加工设置。"));
        return;
    }
    if (m_setupOverviewDialog) {
        m_setupOverviewDialog->show();
        m_setupOverviewDialog->raise();
        m_setupOverviewDialog->activateWindow();
        return;
    }
    auto *project = AppController::instance().projectManager();
    if (!project) return;
    const bool zh = isChineseUi();
    const auto ui = [zh](const char *chinese, const char *english) {
        return zh ? QString::fromUtf8(chinese) : QString::fromLatin1(english);
    };
    auto *dialog = new QDialog(this);
    m_setupOverviewDialog = dialog;
    dialog->setObjectName(QStringLiteral("setupOverviewDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(ui("加工设置", "Machining setup"));
    dialog->resize(620, 420);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(18);
    auto *status = new QLabel(dialog);
    status->setObjectName(QStringLiteral("setupOverviewStatus"));
    status->setWordWrap(true);
    QFont headingFont = status->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 2);
    status->setFont(headingFont);
    layout->addWidget(status);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(20);
    grid->setColumnStretch(1, 1);
    auto *directionText = new QLabel(dialog);
    auto *originText = new QLabel(dialog);
    auto *stockText = new QLabel(dialog);
    auto *direction = new QPushButton(ui("选择加工方向", "Select direction"), dialog);
    auto *origin = new QPushButton(ui("设置坐标原点", "Set work origin"), dialog);
    auto *stock = new QPushButton(ui("设置毛坯", "Set stock"), dialog);
    direction->setObjectName(QStringLiteral("setupDirectionAction"));
    origin->setObjectName(QStringLiteral("setupOriginAction"));
    stock->setObjectName(QStringLiteral("setupStockAction"));
    const QStringList titles = {ui("1. 加工方向", "1. Direction"), ui("2. 坐标原点", "2. Work origin"), ui("3. 毛坯", "3. Stock")};
    const QList<QLabel *> descriptions = {directionText, originText, stockText};
    const QList<QPushButton *> actions = {direction, origin, stock};
    for (int row = 0; row < 3; ++row) {
        descriptions[row]->setWordWrap(true);
        grid->addWidget(new QLabel(titles[row], dialog), row, 0);
        grid->addWidget(descriptions[row], row, 1);
        actions[row]->setMinimumHeight(36);
        grid->addWidget(actions[row], row, 2);
    }
    layout->addLayout(grid);
    auto *consequence = new QLabel(ui(
        "重新选择加工方向会清空现有工序和程序，并要求重新确认原点与毛坯。修改原点或毛坯后，相关刀路需要重新计算。",
        "Reselecting the direction clears existing operations and programs and requires origin and stock confirmation again. Changing the origin or stock requires toolpath recalculation."), dialog);
    consequence->setWordWrap(true);
    layout->addWidget(consequence);
    layout->addStretch();
    auto *close = new QPushButton(ui("关闭", "Close"), dialog);
    layout->addWidget(close, 0, Qt::AlignRight);
    connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    connect(direction, &QPushButton::clicked, dialog, [this, dialog]() {
        dialog->close();
        onSetFrontFace(true);
    });
    connect(origin, &QPushButton::clicked, dialog, [this, dialog]() {
        dialog->close();
        onEditSetupOrigin();
    });
    connect(stock, &QPushButton::clicked, dialog, [this, dialog]() {
        dialog->close();
        onEditStockDefinition();
    });
    const auto refresh = [=]() {
        const bool sketch = project->sourceType() == ProjectSourceType::Sketch;
        const bool modelReady = (!project->mesh().isEmpty() || (project->sourceType() == ProjectSourceType::Sketch && !project->sketchDocument().isEmpty()));
        const bool directionReady = modelReady && project->activeRegion() != FaceRegion::Unknown;
        const bool originReady = directionReady && project->setupOrigin().confirmed;
        const bool stockReady = originReady && project->stockDefinition().confirmed;
        direction->setEnabled(modelReady && !sketch);
        origin->setEnabled(directionReady);
        stock->setEnabled(originReady);
        status->setText(!modelReady ? ui("请先导入 STEP 模型", "Import a STEP model first")
            : !directionReady ? ui("下一步：选择并确认加工方向", "Next: select and confirm the machining direction")
            : !originReady ? ui("下一步：确认坐标原点", "Next: confirm the work origin")
            : !stockReady ? ui("下一步：确认毛坯", "Next: confirm the stock")
            : ui("加工设置已完成，可选择加工几何", "Setup complete. Select machining geometry."));
        directionText->setText(sketch ? ui("XY 平面，固定 +Z", "XY plane, fixed +Z") : directionReady ? ui("已确认", "Confirmed") : ui("未确认", "Not confirmed"));
        const Coordinate3D point = machiningOrigin(project);
        originText->setText(originReady ? QStringLiteral("%1\nX %2  Y %3  Z %4 mm")
            .arg(project->workOffset()).arg(point.x(), 0, 'f', 3).arg(point.y(), 0, 'f', 3).arg(point.z(), 0, 'f', 3)
            : ui("未确认", "Not confirmed"));
        const Coordinate3D size = machiningStockBounds(project).size();
        stockText->setText(stockReady ? QStringLiteral("%1 × %2 × %3 mm")
            .arg(size.x(), 0, 'f', 3).arg(size.y(), 0, 'f', 3).arg(size.z(), 0, 'f', 3)
            : ui("未确认", "Not confirmed"));
    };
    connect(project, &ProjectManager::projectChanged, dialog, refresh);
    connect(this, &MainWindow::activeRegionChanged, dialog, refresh);
    refresh();
    dialog->show();
}

void MainWindow::onSetFrontFace(bool checked)
{
    if (AppController::instance().projectManager()->sourceType() == ProjectSourceType::Sketch) {
        m_actSetFrontFace->setChecked(false);
        m_settingFrontFace = false;
        m_bottomBar->setStatus(tr("草图加工方向固定为 XY 平面的 +Z。"));
        return;
    }
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

// 函数说明：onEditSetupOrigin负责当前模块的核心处理。
void MainWindow::onEditSetupOrigin()
{
    ProjectManager *project = AppController::instance().projectManager();
    const bool zh = isChineseUi();
    if (!project || !(!project->mesh().isEmpty() || (project->sourceType() == ProjectSourceType::Sketch && !project->sketchDocument().isEmpty()))) {
        OperationMessageBox::warning(this,
                             zh ? QStringLiteral("无法设置原点")
                                : QStringLiteral("Cannot Set Origin"),
                             zh ? QStringLiteral("请先导入 STEP 模型或绘制非空草图。")
                                : QStringLiteral("Import a STEP model or draw a nonempty sketch first."));
        return;
    }
    if (m_activeRegion == FaceRegion::Unknown) {
        OperationMessageBox::warning(this,
                             zh ? QStringLiteral("需要先设置正面")
                                : QStringLiteral("Front Face Required"),
                             zh ? QStringLiteral("请先人工选择并确认加工正面，再设置工件原点。")
                                : QStringLiteral("Select and confirm the machining front face before setting the work origin."));
        return;
    }

    SetupOriginDialog dialog(zh, this);
    const auto modelBounds = machiningModelBounds(project);
    dialog.setModelBounds(modelBounds.minimum, modelBounds.maximum);
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
    m_strategyPanel->setWorkOffset(workOffset);

    const Coordinate3D point = machiningOrigin(project);
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

// 函数说明：onSetOriginFromSelectedHole负责当前模块的核心处理。
void MainWindow::onSetOriginFromSelectedHole()
{
    ProjectManager *project = AppController::instance().projectManager();
    const bool zh = isChineseUi();
    const int featureIndex = m_featurePanel ? m_featurePanel->currentFeatureIndex() : -1;
    if (!project || featureIndex < 0 || featureIndex >= project->features().size()
        || !isHoleFeature(project->features()[featureIndex])) {
        OperationMessageBox::warning(
            this,
            zh ? QStringLiteral("未选择孔") : QStringLiteral("No Hole Selected"),
            zh ? QStringLiteral("请在识别特征列表中选中一个具体孔，而不是孔分组。")
               : QStringLiteral("Select one specific hole in the recognized-feature list, not a hole group."));
        return;
    }
    const MachiningFeature &hole = project->features()[featureIndex];
    if (m_activeRegion == FaceRegion::Unknown
        // 函数说明：requiresActiveRegionConfirmation负责当前模块的核心处理。
        || SetupOrientation::requiresActiveRegionConfirmation(m_activeRegion, hole.region)) {
        OperationMessageBox::warning(
            this,
            zh ? QStringLiteral("孔不属于当前 Setup") : QStringLiteral("Hole Outside Current Setup"),
            zh ? QStringLiteral("所选孔必须属于已确认的当前加工正面。")
               : QStringLiteral("The selected hole must belong to the confirmed active machining face."));
        return;
    }

    const Coordinate3D currentOrigin = project->setupOrigin().confirmed
        ? machiningOrigin(project)
        : Coordinate3D(0.0, 0.0, project->mesh().bbMax.z());
    const Coordinate3D holeOrigin(hole.center.x(), hole.center.y(), currentOrigin.z());
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

// 函数说明：onEditStockDefinition负责当前模块的核心处理。
void MainWindow::onEditStockDefinition()
{
    ProjectManager *project = AppController::instance().projectManager();
    const bool zh = isChineseUi();
    if (!project || !(!project->mesh().isEmpty() || (project->sourceType() == ProjectSourceType::Sketch && !project->sketchDocument().isEmpty()))) {
        OperationMessageBox::warning(this,
                             zh ? QStringLiteral("无法设置毛坯")
                                : QStringLiteral("Cannot Set Stock"),
                             zh ? QStringLiteral("请先导入 STEP 模型或绘制非空草图。")
                                : QStringLiteral("Import a STEP model or draw a nonempty sketch first."));
        return;
    }
    if (m_activeRegion == FaceRegion::Unknown || !project->setupOrigin().confirmed) {
        OperationMessageBox::warning(this,
                             zh ? QStringLiteral("Setup 尚未完成")
                                : QStringLiteral("Setup Incomplete"),
                             zh ? QStringLiteral("请先确认加工正面和工件原点，再设置毛坯。")
                                : QStringLiteral("Confirm the machining front face and work origin before setting stock."));
        return;
    }

    StockDefinitionDialog dialog(zh, this);
    const auto modelBounds = machiningModelBounds(project);
    dialog.setPartBounds(modelBounds.minimum, modelBounds.maximum);
    dialog.setStockDefinition(project->stockDefinition());
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString previousFingerprint = project->setupFingerprint();
    project->setStockDefinition(dialog.stockDefinition());
    const StockBounds bounds = machiningStockBounds(project);
    const Coordinate3D size = bounds.size();
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

// 函数说明：onEditMachineProfile负责当前模块的核心处理。
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

// 函数说明：onLanguageChinese负责当前模块的核心处理。
void MainWindow::onLanguageChinese()
{
    switchLanguage(QStringLiteral("zh_CN"));
}

// 函数说明：onLanguageEnglish负责当前模块的核心处理。
void MainWindow::onLanguageEnglish()
{
    switchLanguage(QStringLiteral("en_US"));
}

// 函数说明：onAbout负责当前模块的核心处理。
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

// 函数说明：onStepImported负责当前模块的核心处理。
void MainWindow::onStepImported(const QString &filePath)
{
    resetSourceViews();
    m_bottomBar->setStatus(tr("Imported STEP: %1").arg(filePath));
}

void MainWindow::resetSourceViews()
{
    m_projectFilePath.clear();
    m_externalGCodePath.clear();
    m_externalGCodeText.clear();
    m_externalPrograms.clear();
    if (m_externalProgramCombo) {
        const QSignalBlocker blocker(m_externalProgramCombo);
        m_externalProgramCombo->clear();
        m_externalProgramCombo->hide();
    }
    m_simViewport->setToolGeometryVisible(true);
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

}

// 函数说明：jumpToGeneratedOperation负责当前模块的核心处理。
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

// 函数说明：onGCodeReady负责当前模块的核心处理。
void MainWindow::onGCodeReady(const QString &gcode)
{
    appendProgramSnapshot(tr("Program"), gcode, QStringLiteral("external"));
    m_bottomBar->showProgress(true);
    m_bottomBar->setStatus(tr("G-code loaded."));
}

// 函数说明：onStatusMessage负责当前模块的核心处理。
void MainWindow::onStatusMessage(const QString &msg)
{
    m_bottomBar->setStatus(msg);
}

// 函数说明：onErrorOccurred负责当前模块的核心处理。
void MainWindow::onErrorOccurred(const QString &msg)
{
    m_bottomBar->setStatus(OperationMessageBox::summary(msg));
    OperationMessageBox::warning(this, tr("Error"), msg);
}

// 函数说明：switchLanguage负责当前模块的核心处理。
void MainWindow::switchLanguage(const QString &lang)
{
    Settings::instance().setLanguage(lang);
    Settings::instance().save();

    qApp->removeTranslator(&m_appTranslator);
    qApp->removeTranslator(&m_qtTranslator);

    if (m_qtTranslator.load(QStringLiteral("qt_") + lang,
                            // 函数说明：location负责当前模块的核心处理。
                            QLibraryInfo::location(QLibraryInfo::TranslationsPath))) {
        qApp->installTranslator(&m_qtTranslator);
    }
    if (m_appTranslator.load(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations/") + lang)) ||
        // 函数说明：load负责当前模块的核心处理。
        m_appTranslator.load(QDir(QCoreApplication::applicationDirPath()).filePath(lang)) ||
        m_appTranslator.load(QStringLiteral(":/translations/") + lang)) {
        qApp->installTranslator(&m_appTranslator);
    }

    retranslateUi();
}

// 函数说明：retranslateUi负责当前模块的核心处理。
void MainWindow::retranslateUi()
{
    const bool zh = isChineseUi();
    if (m_actImportGCode) {
        m_actImportGCode->setText(zh ? QStringLiteral("导入 G 代码...") : QStringLiteral("Import G-code..."));
        m_actImportGCode->setToolTip(zh ? QStringLiteral("无需模型；支持毫米绝对坐标、XY 平面 IJ 圆弧。外部程序仅用于轨迹回放。")
                                      : QStringLiteral("No model required; millimetres, absolute XYZ and XY IJ arcs. External programs are preview only."));
    }
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

    if (m_designPage) {
        const QList<QToolButton*> designButtons =
            m_designPage->findChildren<QToolButton*>();
        for (QToolButton *button : designButtons) {
            if (!button->property("designCommandButton").toBool()) {
                continue;
            }
            const QString category = button->property("designCategory").toString();
            if (category == QStringLiteral("Holes"))
                button->setText(zh ? QStringLiteral("孔类") : QStringLiteral("Holes"));
            else if (category == QStringLiteral("Threads"))
                button->setText(zh ? QStringLiteral("牙类") : QStringLiteral("Threads"));
            else if (category == QStringLiteral("Surfaces"))
                button->setText(zh ? QStringLiteral("平面/曲面") : QStringLiteral("Surfaces"));
            else if (category == QStringLiteral("Slots"))
                button->setText(zh ? QStringLiteral("槽/型腔") : QStringLiteral("Slots / Pockets"));
            else if (category == QStringLiteral("Contours"))
                button->setText(zh ? QStringLiteral("外形") : QStringLiteral("Contours"));
            else if (category == QStringLiteral("Automation"))
                button->setText(zh ? QStringLiteral("自动化") : QStringLiteral("Automation"));
            else if (category == QStringLiteral("Assist"))
                button->setText(zh ? QStringLiteral("辅助/便捷") : QStringLiteral("Assist / Utilities"));
            button->setToolTip(zh ? QStringLiteral("第七章设计功能入口；灰色项目尚未纳入当前 CAM 验收范围。")
                                  : QStringLiteral("Chapter 7 design commands; grey items are outside the current CAM acceptance scope."));
        }
        const QList<QAction*> designActions = m_designPage->findChildren<QAction*>();
        for (QAction *action : designActions) {
            const QString command = action->property("designCommand").toString();
            if (command.isEmpty()) {
                continue;
            }
            QString label;
            if (command == QStringLiteral("reviewHoles")) label = zh ? QStringLiteral("查看孔特征") : QStringLiteral("Review hole features");
            else if (command == QStringLiteral("spotDrill")) label = zh ? QStringLiteral("打点 / G81-G85") : QStringLiteral("Spot / G81-G85");
            else if (command == QStringLiteral("peckDrill")) label = zh ? QStringLiteral("啄钻 / 深孔") : QStringLiteral("Peck / deep drilling");
            else if (command == QStringLiteral("holeChamfer")) label = zh ? QStringLiteral("孔倒角") : QStringLiteral("Hole chamfer");
            else if (command == QStringLiteral("boring")) label = zh ? QStringLiteral("镗 G76 / G86") : QStringLiteral("Boring G76 / G86");
            else if (command == QStringLiteral("reviewThreads")) label = zh ? QStringLiteral("查看螺纹孔") : QStringLiteral("Review threaded holes");
            else if (command == QStringLiteral("tap")) label = zh ? QStringLiteral("攻牙") : QStringLiteral("Tapping");
            else if (command == QStringLiteral("threadMill")) label = zh ? QStringLiteral("铣牙") : QStringLiteral("Thread milling");
            else if (command == QStringLiteral("reviewSurfaces")) label = zh ? QStringLiteral("查看平面特征") : QStringLiteral("Review planar features");
            else if (command == QStringLiteral("faceMill")) label = zh ? QStringLiteral("铣平面") : QStringLiteral("Face milling");
            else if (command == QStringLiteral("slopeMill")) label = zh ? QStringLiteral("铣斜面 / 3D 斜面") : QStringLiteral("Slope / 3D slope milling");
            else if (command == QStringLiteral("filletMill")) label = zh ? QStringLiteral("铣圆角") : QStringLiteral("Fillet milling");
            else if (command == QStringLiteral("reviewSlots")) label = zh ? QStringLiteral("查看槽与型腔") : QStringLiteral("Review slots and pockets");
            else if (command == QStringLiteral("openSlot")) label = zh ? QStringLiteral("开口槽 / 盲槽") : QStringLiteral("Open / blind slot");
            else if (command == QStringLiteral("pocket")) label = zh ? QStringLiteral("型腔开粗") : QStringLiteral("Pocket roughing");
            else if (command == QStringLiteral("adaptiveMill")) label = zh ? QStringLiteral("动态铣") : QStringLiteral("Adaptive milling");
            else if (command == QStringLiteral("islandMill")) label = zh ? QStringLiteral("铣孤岛 / 环形铣") : QStringLiteral("Island / annular milling");
            else if (command == QStringLiteral("reviewContours")) label = zh ? QStringLiteral("查看轮廓") : QStringLiteral("Review contours");
            else if (command == QStringLiteral("contourMill")) label = zh ? QStringLiteral("铣形 / 轮廓精加工") : QStringLiteral("Contour finishing");
            else if (command == QStringLiteral("outerChamfer")) label = zh ? QStringLiteral("外形 / 3D 倒角") : QStringLiteral("Outer / 3D chamfer");
            else if (command == QStringLiteral("engrave")) label = zh ? QStringLiteral("刻字 / 空间曲线") : QStringLiteral("Engraving / spatial curves");
            else if (command == QStringLiteral("featureRecognition")) label = zh ? QStringLiteral("特征识别") : QStringLiteral("Feature recognition");
            else if (command == QStringLiteral("autoHole")) label = zh ? QStringLiteral("自动孔") : QStringLiteral("Automatic holes");
            else if (command == QStringLiteral("smartChamfer")) label = zh ? QStringLiteral("智能倒角") : QStringLiteral("Smart chamfer");
            else if (command == QStringLiteral("processTemplates")) label = zh ? QStringLiteral("工艺模板") : QStringLiteral("Process templates");
            else if (command == QStringLiteral("reviewTools")) label = zh ? QStringLiteral("刀具库") : QStringLiteral("Tool library");
            else if (command == QStringLiteral("reviewSimulation")) label = zh ? QStringLiteral("最终代码仿真") : QStringLiteral("Final-code simulation");
            else if (command == QStringLiteral("measurement")) label = zh ? QStringLiteral("测量 / 标注") : QStringLiteral("Measure / annotate");
            else if (command == QStringLiteral("cadEdit")) label = zh ? QStringLiteral("绘制与 CAD 编辑") : QStringLiteral("Drawing and CAD editing");
            if (!action->isEnabled()) {
                label += zh ? QStringLiteral("（规划中）") : QStringLiteral(" (Planned)");
            }
            action->setText(label);
            action->setToolTip(action->isEnabled()
                ? (zh ? QStringLiteral("进入当前已实现的 CAM 工作流。")
                      : QStringLiteral("Open the available CAM workflow."))
                : (zh ? QStringLiteral("此功能已按参考手册保留入口，但尚未实现，不能生成刀路。")
                      : QStringLiteral("This reference-manual command is reserved but cannot generate toolpaths yet.")));
        }
    }

    if (m_actImportStep) m_actImportStep->setText(zh ? QStringLiteral("导入 STEP...")
                                                     : QStringLiteral("Import STEP..."));
    if (m_actOpenProject) m_actOpenProject->setText(zh ? QStringLiteral("打开项目...")
                                                       : QStringLiteral("Open Project..."));
    if (m_actSaveProject) m_actSaveProject->setText(zh ? QStringLiteral("保存项目")
                                                       : QStringLiteral("Save Project"));
    if (m_actSaveProjectAs) m_actSaveProjectAs->setText(zh ? QStringLiteral("项目另存为...")
                                                           : QStringLiteral("Save Project As..."));
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
        if (m_workspaceRouter) m_workspaceRouter->refreshTitle();
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
            zh ? QStringLiteral("可直接导入 G 代码仿真，无需模型。\n也可在“工艺设计”中生成程序快照。")
               : QStringLiteral("Import G-code for preview without a model, or generate a snapshot in Process Design."));
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

void MainWindow::refreshOperationPathVisibility()
{
    if (!m_externalGCodePath.isEmpty()) {
        m_simViewport->setToolPathVisibility({});
        return;
    }
    QSet<QString> hidden;
    for (const auto &operation : m_operationPanel->operations())
        if (!m_operationPanel->operationVisible(operation.id)) hidden.insert(operation.id);
    QVector<int> sourceLines;
    sourceLines.reserve(m_simCtrl->moves().size());
    for (const auto &move : m_simCtrl->moves())
        if (move.fromKnownAxes == 7 && move.toKnownAxes == 7) sourceLines.append(move.sourceLine);
    const auto mask = operationPathVisibility(m_simCtrl->sourceText(), sourceLines, hidden);
    m_viewport->setToolPathVisibility(mask);
    m_simViewport->setToolPathVisibility(mask);
}
