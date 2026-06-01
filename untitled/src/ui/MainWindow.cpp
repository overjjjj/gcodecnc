#include "MainWindow.h"

#include "BottomBar.h"
#include "CncSendDialog.h"
#include "FeatureListPanel.h"
#include "GCodeEditor.h"
#include "OperationListPanel.h"
#include "StrategyPanel.h"
#include "ToolLibraryPanel.h"
#include "ViewportWidget.h"
#include "../core/AppController.h"
#include "../core/Settings.h"
#include "../postprocessor/PostProcessorBase.h"
#include "../postprocessor/PostProcessorRegistry.h"
#include "../simulation/SimulationController.h"
#include "../strategies/StrategyFactory.h"
#include "../strategies/hole/HoleStrategyUtils.h"
#include "../strategies/mill/SlotMachiningGeometry.h"
#include "../tool/ToolLibrary.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QLibraryInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QDebug>
#include <QQuaternion>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QTextStream>
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
        return double(feature.center.z()) + feature.depth * 0.5;
    }
    return double(feature.center.z());
}

static bool nearlyEqual(double a, double b, double tolerance = 1.0e-6)
{
    return std::abs(a - b) <= tolerance;
}

static void refineSlotContourFromMesh(const MachiningFeature &source,
                                      const MeshData &mesh,
                                      ContourFeature &contour)
{
    if (source.subType == QStringLiteral("straight_slot") ||
        source.subType == QStringLiteral("arc_slot")) {
        return;
    }
    refineSlotContourFromMeshData(source, mesh, contour);
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
    if (feature.subType == QStringLiteral("open_slot")) {
        contour.openSide = 1.0;
    }
    if (mesh) {
        refineSlotContourFromMesh(feature, *mesh, contour);
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
    if (params.values.contains(QStringLiteral("depth"))) {
        const double value = params.get(QStringLiteral("depth"), baseFeature.depth);
        if (!nearlyEqual(value, baseFeature.depth, 1.0e-3)) {
            feature.depth = value;
        }
    }
    if (params.values.contains(QStringLiteral("slotLength"))) {
        const double value = params.get(QStringLiteral("slotLength"),
                                        baseFeature.length > 0.0 ? baseFeature.length
                                                                 : baseFeature.radius * 2.0);
        const double baseValue = baseFeature.length > 0.0 ? baseFeature.length
                                                          : baseFeature.radius * 2.0;
        if (!nearlyEqual(value, baseValue, 1.0e-3)) {
            feature.length = value;
        }
    }
    if (params.values.contains(QStringLiteral("slotWidth"))) {
        const double value = params.get(QStringLiteral("slotWidth"),
                                        baseFeature.width > 0.0 ? baseFeature.width
                                                                : baseFeature.radius);
        const double baseValue = baseFeature.width > 0.0 ? baseFeature.width
                                                         : baseFeature.radius;
        if (!nearlyEqual(value, baseValue, 1.0e-3)) {
            feature.width = value;
        }
    }
    if (params.values.contains(QStringLiteral("angle"))) {
        const double value = params.get(QStringLiteral("angle"), baseFeature.angle);
        if (!nearlyEqual(value, baseFeature.angle, 1.0e-3)) {
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
        << QStringLiteral("[slot-debug] %1 subtype=%2 center=(%3,%4,%5) L=%6 W=%7 D=%8 A=%9 openSide=%10 paramL=%11 paramW=%12 paramD=%13 paramA=%14")
              .arg(QString::fromLatin1(label))
              .arg(feature.subType)
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
        return zh ? QStringLiteral("Invalid hole geometry.") : QStringLiteral("The selected hole is missing valid geometry, so G-code cannot be generated.");
    }

    if (feature.region == FaceRegion::Side) {
        return zh ? QStringLiteral("Side holes require a dedicated setup.") : QStringLiteral("The current drilling workflow only supports a single front-face Z setup. Side holes need fixture rotation or a dedicated setup workflow.");
    }

    if (feature.region == FaceRegion::Back) {
        return zh ? QStringLiteral("Back-face holes are not supported yet.") : QStringLiteral("Back-face setup transformation is not implemented yet. Back-face holes cannot be generated with the current front-face Z setup.");
    }

    return QString();
}

static bool isSlotMillingStrategy(const QString &strategyId)
{
    return strategyId == QStringLiteral("mill_slot") ||
           strategyId == QStringLiteral("mill_blind_slot");
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

    createMenus();
    createToolBar();
    createDocks();
    createStatusBar();
    connectSignals();
    retranslateUi();

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
    m_mainToolBar->setMovable(false);
    m_mainToolBar->addAction(m_actImportStep);
    m_mainToolBar->addAction(m_actSaveProject);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_actResetCamera);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_actExportGCode);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_actSimPlay);
    m_mainToolBar->addAction(m_actSimPause);
    m_mainToolBar->addAction(m_actSimStop);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_actSetFrontFace);
    m_mainToolBar->addSeparator();

    m_ppCombo = new QComboBox(this);
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

    m_wcsCombo = new QComboBox(this);
    for (int code = 54; code <= 59; ++code) {
        const QString wcs = QStringLiteral("G%1").arg(code);
        m_wcsCombo->addItem(wcs, wcs);
    }
    m_wcsCombo->setToolTip(QStringLiteral("Work coordinate system"));
    m_mainToolBar->addWidget(m_wcsCombo);
}

void MainWindow::createDocks()
{
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_viewport);
    splitter->addWidget(m_gcodeEditor);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    m_featureDock = new QDockWidget(this);
    m_featureDock->setWidget(m_featurePanel);
    m_featureDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, m_featureDock);

    m_strategyDock = new QDockWidget(this);
    m_strategyDock->setWidget(m_strategyPanel);
    m_strategyDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, m_strategyDock);

    m_toolDock = new QDockWidget(this);
    m_toolDock->setWidget(m_toolPanel);
    m_toolDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, m_toolDock);
    tabifyDockWidget(m_strategyDock, m_toolDock);
    m_strategyDock->raise();

    m_operationDock = new QDockWidget(this);
    m_operationDock->setWidget(m_operationPanel);
    m_operationDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, m_operationDock);
    splitDockWidget(m_featureDock, m_operationDock, Qt::Vertical);
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

    connect(m_simCtrl, &SimulationController::toolPathReady,
            m_viewport, &ViewportWidget::setToolPath);
    connect(m_simCtrl, &SimulationController::toolPositionChanged,
            m_viewport, &ViewportWidget::setToolPosition);
    connect(m_simCtrl, &SimulationController::toolDiameterChanged,
            m_viewport, &ViewportWidget::setToolDiameter);
    connect(m_simCtrl, &SimulationController::toolModelPathChanged,
            m_viewport, &ViewportWidget::setToolModelPath);
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
    connect(m_bottomBar, &BottomBar::simulationSpeedChanged,
            m_simCtrl, &SimulationController::setSpeed);
    connect(m_simCtrl, &SimulationController::simulationFinished, this, [this]() {
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
        connect(project, &ProjectManager::operationsChanged, this, [this]() {
            ProjectManager *pm = AppController::instance().projectManager();
            if (pm) {
                m_operationPanel->setOperations(pm->operations());
            }
        });
    }

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

        for (int index : indices) {
            if (index < 0 || index >= features.size()) {
                continue;
            }
            if (preferredIndex < 0 && index == currentIndex) {
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
                    if (m_pendingFrontFaceIndex == faceIndex) {
                        // 缂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ょ紓宥咃躬瀵鎮㈤崗灏栨嫽闁诲酣娼ф竟濠偽ｉ鍓х＜闁绘劦鍓欓崝銈囩磽瀹ュ拑韬€殿喖顭烽弫鎰緞婵犲嫷鍚呴梻浣瑰缁诲倿骞夊☉銏犵缂備焦顭囬崢杈ㄧ節閻㈤潧孝闁稿﹤缍婂畷鎴﹀Ψ閳哄倻鍘搁柣蹇曞仩椤曆勬叏閸屾壕鍋撳▓鍨灍闁瑰憡濞婇獮鍐ㄢ枎瀵版繂婀遍埀顒婄秵娴滄瑦绔熼弴銏♀拺闁告挻褰冩禍婵囩箾閸欏澧甸柟顔惧仱瀹曞綊顢曢悩杈╃泿闂備胶鎳撻幖顐⑽涘Δ浣侯洸濡わ絽鍟埛鎴︽煕濠靛嫬鍔氶柡瀣捣閻ヮ亞绱掗姀鐘茬闂佸憡甯楃敮鐐哄箯閻樿鍦偓锝庡亽濞兼梹绻濈喊妯活潑闁搞劌鐖煎銊╂焼瀹ュ繒绋忛悗骞垮劚閹冲寮ㄦ禒瀣厓闁芥ê顦伴ˉ婊堟煟韫囥儳绉柡灞界Т閻ｏ繝骞嶉纰辨毇闂佸憡顨夋ご鎼佸Φ閸曨垰鍐€闁靛ě浣插亾閹烘梻纾奸柍閿亾闁稿鎹囧缁樻媴閸︻厽鑿囬梺鎼炲姀濡嫰鈥﹂崶顏嶆Ъ缂備礁鍊圭敮锟犲极閸愵喖纾兼繛鎴炶壘楠炲牓姊绘笟鈧褔鈥﹂崼銉ョ鐎光偓閸曨剙浜楅梺褰掑亰閸犳氨澹曢懖鈹惧亾閸忓浜鹃梺鍛婃处娴滄繈宕板鑸碘拺閻犲洠鈧櫕鐏嶇紓渚囧枛閻線鎮橀幒妤佺厽闁绘ê寮舵径鍕喐閺夊灝鏆為悗闈涘悑閹棃濡搁敂瑙勫濠电偠鎻徊鍧楁偤閺傛鐒介柟鎵閻撴洟骞栫划鍏夊亾閾忣偄鈧垶姊洪柅娑氣敀闁告梹鍨舵穱濠囨嚋闂堟稓绐為柣搴秵娴滄瑧鑺遍悽鍛娾拻濞达絽鎲￠崯鐐烘煕閺冩挾鐣电€规洑鍗冲浠嬵敃閵堝棴绱￠柣鐔哥矋濡啫顕ｆ繝姘櫜濠㈣泛锕﹂悿鈧俊鐐€栭幐楣冨窗閹惧墎鐭欓柛銉戝本瀵岄梺闈涚墕閹冲酣顢楅姀銏㈢＜妞ゆ棁鍋愭晥婵犵绱曢弫璇茬暦閻旂⒈鏁嶆慨姗嗗弾濡喐绻濋悽闈涗粶婵☆偅鐩畷鎴﹀箻鐠囪尙鍘洪梺鍓插亖閸庢煡鍩涢幋锔界厱婵犻潧妫楅顐︽煟閹烘洘纭剁紒杈ㄥ笚濞煎繘濡搁敂缁㈡Ч闂備胶纭堕弬渚€宕戦幘鎰佹富闁靛牆妫楃粭鎺楁煥閺囶亜顩紒顔芥濡啫霉閵忋垺鍤€妞ゎ厹鍔嶉幆鏂库攽鐎ｎ亙绮ｉ梺璇叉唉椤煤閳哄啰绀婂ù锝呮憸閺嗭箓鏌ｉ弮鍌楁嫛闁轰礁绉电换娑㈠箣閻愯泛顥濆Δ鐘靛仦閸旀瑥顫忛搹瑙勫珰闁肩⒈鍓涢濠囨⒑缁嬫鍎戝┑鐐╁亾濡炪們鍨烘穱娲囬崘鐫酣宕惰閺€鑽ょ磼鏉炴壆鐭欑€规洏鍔嶇换婵嬪礋椤愩垻鐟查梻鍌氬€峰ù鍥ㄦ叏閵堝鏅俊鐐€х粻鎺懳涢崟顐㈢カ闂備礁缍婂Λ鍧楁倿閿曞倸绠犳慨妯垮煐閻撱儲绻濋棃娑欘棡闁革絿鍏橀弻宥堫檨闁告挻姘ㄩ弫顕€骞掑Δ瀣◤濠电娀娼ч鍡涘磻閸岀偛绠归弶鍫濆⒔鐎靛ジ鏌熺€电鍘存慨濠勭帛閹峰懐绮电€ｎ亝鐣伴梻渚€娼荤紞鍡涘垂閸愬樊鍤楀┑鐘插暟椤╃兘鎮楅敐搴濈敖闁告ɑ鎸冲铏规兜閸涱喖娑ч梻鍌氬鐎氭澘鐣烽幇顓фЧ閹煎瓨锚娴滈箖鏌ｉ悢鍛婄凡妞ゃ儱绻橀弻娑㈡偐瀹曞洤鈪归梺宕囩帛閹瑰洭骞婂鍫熷仺闁汇垻鏁稿Σ鍥ㄧ節閻㈤潧浠滈柣妤€妫濋幃妯衡攽鐎ｎ偄鈧爼姊洪鈧粔鐢告偂閺囩喓绠鹃柛鈩冾殘缁犱即鏌￠崱妯兼噰闁哄矉缍侀弫鎰板炊閵娧冨Ы闂備線娼уú锕傚礉濞嗘挾宓侀柟鐑橆殔缁狅綁鏌ㄩ弴妤€浜鹃梺杞扮缂嶅﹤顫忕紒妯诲闁告稑锕ラ崕鎾斥攽閻愯尙婀撮柛鏃€鍨甸悾鐑藉箣閿旇棄浜归柣鐘叉穿鐏忔瑩顢欓幋锔解拺闁告繂瀚婵嬫煕鐎ｎ偄濮嶇€殿喓鍔嶇粋鎺斺偓锝庡亞閸樹粙姊鸿ぐ鎺戜喊闁告鏅槐鐐哄箣閿旂晫鍘介棅顐㈡处濞叉牠寮稿☉銏＄厸閻忕偛澧藉ú鏉戔攽閳╁啯鍊愬┑锛勫厴閺佸啯鎷呮笟顖涙暏缂傚倸鍊搁崐椋庢閿熺姴绐楅柡宥庡幐閳ь剨绠撻幃婊堟寠婢跺鈧剟姊鸿ぐ鎺戜喊闁告鍋愬▎銏ゆ倷閻戞ê鈧敻鏌ㄥ┑鍡涱€楀褜鍠氶埀顒侇問閸犳稑鈻嶉弴鐘冲床婵犻潧顑嗛崑銊╂⒒閸喓鈻撻柡瀣噹閳规垿鎮欓弶鍨殶闂佺绻掗崑鎾剁矙閹达箑鐓橀柟瀵稿Л閸嬫捇鏁愰崨顖欑驳闂佸搫鎳忕粙鎴﹀煘閹寸偛绠犻梺绋匡攻閹瑰洭骞婂Δ鍛濞达綀妗ㄧ粭澶嬩繆閵堝繒鍒伴柛鐕佸灦閹繝寮撮悢缈犵盎闂侀€涘嵆濞佳勭濠婂厾鐟邦煥閸曨厾鐓傞梺閫炲苯澧い鏃€鐗犲畷鎶筋敋閳ь剙鐣烽弶璇炬棃宕ㄩ鐘垫澖闂備線娼ч…顓熶繆閸モ晛濮柍褜鍓熷娲川婵犱胶绻侀梺闈╃秵閸犳绮嬮幒妤€鐓涢柛娑卞枓閹锋椽鎮峰鍛暭閻㈩垪鏅涜灋闁瑰瓨绺鹃弨浠嬫煟閹般劍娅呭ù婊堢畺濮婄粯鎷呯憴鍕哗闂佸憡姊归崹鍦垝婵犳艾鍐€妞ゆ挾鍋熼鎰版偡濠婂懎顣奸悽顖楁櫊瀵偊宕橀鐣屽帾闂佸壊鍋呯换鍌烆敂椤忓牊鐓曢柣妯哄暱閸濈儤鎱ㄦ繝鍌ょ吋鐎规洘甯掗～婵嬵敄閽樺澹曢悗鐟板閸ｇ銇愰幒鎴犲€炲銈嗗笒閿曪妇绮欒箛鏃傜瘈闁靛骏绲介悡鎰版煕閺冣偓閻楃姴鐣烽幋锕€围濠㈣泛顑囬崢鎼佹⒑閹肩偛鍔楅柡鍛⊕缁傛帟顦归柡宀嬬節瀹曢亶顢橀悩鍨闂備礁鎼惌澶岀礊娴ｈ鍙忛柍褜鍓熼弻鏇㈠醇濠靛浂妫￠梻浣诡儥閸欏啫顫忓ú顏勭闁绘劖褰冮‖澶岀磽娴ｇ瓔鍤欓柛鐔跺嵆楠炴垿濮€閻橆偅顫嶉梺闈涚箳婵挳鎳撻崹顔规斀闁宠棄妫楅悘鐘绘煙绾板崬浜伴柨婵堝仜閻ｆ繈鍩€椤掑倹顫曢柟鎯х摠婵绱掔€ｎ偒鍎ラ柡鍡愬灲濮婅櫣绮欏▎鎯у壄闂佺锕ョ换鍌烆敋閿濆棛绡€婵﹩鍘兼禍婊堟⒑缁嬭法绠洪柛瀣姍瀹曟繈鎮滈懞銉㈡嫼闂佸湱顭堢€涒晝澹曢悽鍛婄厱閻庯綆鍋呯亸顓熴亜椤撯€冲姷妞わ箓浜堕弻娑樜旀担绯曟灆閻庢鍠涢褔鍩ユ径鎰潊闁绘鏁稿澶愭⒒娴ｇ瓔娼愰柛搴ゆ珪閺呰埖鎯旈妸銉﹁緢闂佺粯锚濡﹤銆掓繝姘厪闁割偅绻勯崙鍦磼閵娿儺鐓奸柡灞剧洴閹垺顦版惔銏″枛濠电儑绲藉ú銈夋晝椤忓嫮鏆﹀┑鍌滎焾閸楁娊鏌ｉ幇顖氳敿闁逞屽墮閻忔繈鍩為幋鐐茬疇闂佺锕ュú鐔风暦椤栨稑顕遍柡澶嬪灩閻ゅ洭姊虹化鏇炲⒉缂佸甯￠崺娑㈠箣閿旇棄浠梺鎼炲劘閸斿瞼寰婄紒妯镐簻闁瑰搫顑呴悘鏌ユ煛鐏炲墽娲存鐐差儏閳规垿宕奸埗瑁ゅ姂閹鎲撮崟顒傗敍缂傚倸绉崇粈渚€鎮惧畡閭︽建闁逞屽墴閵嗕礁顫滈埀顒勫箖閵堝纾兼繝濠傛嫅缁辨娊姊婚崒姘偓鎼佸磹閹间礁纾瑰瀣捣閻棗霉閿濆懏璐￠柣婵婃硾閳规垿鎮╅崣澶嬫倷缂佺偓鍎抽…鐑藉蓟濞戙垹鐒洪柛鎰典簴濡插牏绱撴担鐟板妞ゃ劌锕璇测槈閵忕姷鍔撮梺鍛婂姇婢т粙宕濆畡閭︽富闁靛牆鍊瑰▍鍛偓娈垮枛婢у酣骞戦姀鐘斀闁搞儮鏅濋惁鍫ユ⒑缁嬫寧婀伴柛鎴ｎ潐缁傛帞鈧稒顭囩粻楣冩倵濞戞瑯鐒介柣顓烆儑缁辨帡顢欓懞銉ョ３閻庢鍠栭…鐑藉箖閵忋垻鐭欐繛鍡樺劤濞堝ジ姊虹拠鎻掝劉缂佸甯熼幗顐︽⒒娴ｈ鍎ラ柛妯兼櫕閹广垹鈽夊锝呬壕婵炴垶鐟悞浠嬫煟椤撶儐鍎戠紒杈ㄥ浮椤㈡瑩鎳為妷銉э紦闁诲孩顔栭崰鏍€﹂悜钘夋瀬鐎广儱顦粈瀣煏婵炲灝鍔欏瑙勬礋濮婃椽骞愭惔锝囩暤闂佸摜鍣ラ崑鍛┍婵犲啰闄勯柛娑橈功閸樻捇鎮峰鍕煉鐎规洘绮岄～婵嬬叕濞村浜鹃柛鎰靛枛闁卞洦绻濋姀鈺€鎮嶇紓宥勭窔閻涱噣宕堕鈧痪褔鎮规笟顖滃帥闁哥偟鍎ょ换婵嗩嚗闁垮绶查柍褜鍓氶崝娆忕暦閹达箑绠婚柛鎾茶兌閿涙粓姊虹紒妯忣亞澹曢銏犵厐闁哄洢鍨洪悡蹇涙煕閵夋垵鍠氭导鍐ㄎ旈悩闈涗沪闁挎岸鎽堕弽顓熺厓鐟滄粓宕滃☉銏犵劦妞ゆ帊绀侀崵顒勬煕閿濆繒鍒版い鏇秮楠炲棝鎳滈鎸庣潖闂備礁婀遍崕銈夊箰閹绢喖绠紓浣诡焽缁犻箖寮堕崼婵嗏挃闁告帊鍗抽弻鐔烘嫚瑜忕弧鈧Δ鐘靛仜濡繂鐣锋總绋课ㄩ柨鏃€鍎抽獮妤佷繆閻愵亜鈧牕顔忔繝姘；闁瑰墽绮悡蹇涙煕椤愮姴鐒洪柣鎺楃畺閺岋綁鏁愭径瀣殸闁诲酣娼ч妶绋款嚕閸洖绠ｉ柣娆屽亾闁哥喎顑夊娲嚒閵堝憛銏ゆ煙绾板崬浜版鐐寸墵椤㈡洟鏁傞挊澶嗗亾鐠鸿　鏀介柣妯诲絻娴滅偞绻涢幘鎰佺吋闁哄本娲熷畷鐓庘攽閸ヨ埖锛侀梺璇茬箰缁绘帡寮繝姘摕婵炴垶鍩冮崑鎾绘晲鎼粹€茬盎婵犳鍠栧ú顓炵暦閿熺姵鐒肩€广儱妫涢崢鍗烆渻閵堝棗濮傞柛濠冩礋瀵顓奸崶鈺冿紲闁荤姴娲ゅ鍫曀夐悙鐑樼厪闁搞儜鍐句純閻庢鍠曠划娆愪繆濮濆矈妲奸梺闈╃秵閸犳鎹㈠☉銏犵闁绘劖娼欑壕鈺呮煟韫囨挾绠查柣鐔濆吘锝夊箛閺夎法顔婇梺瑙勫劤椤曨厾绮旈崼鏇熲拺閻庡湱濮甸妴鍐╀繆閻愭潙绗掗崡閬嶆煙闁箑鍘撮柡鈧懞銉ｄ簻闁哄啫鍊堕埀顒€顑嗚灋婵﹩鍘剧粻楣冩煕濠婂啫鏆熺紒澶樺枟椤ㄣ儵鎮欓幓鎺撴闁诲酣娼ч妶绋款嚕閸洖绠ｉ柣妯夸含缁€鍫ユ⒒閸屾瑨鍏屾い顓炵墢閹广垽骞囬弶璺ㄧ枃闁瑰吋鐣崜顐ｅ緞婵犲孩鍍甸梺鐓庢憸椤ｄ粙骞忔繝姘拺缂佸瀵у﹢浼存煟閻曞倻鐣甸挊婵嗩渻鐎ｎ亝鎹ｇ痪鍓у帶鍗遍悘鐐插⒔婢ь亪鏌＄€ｃ劌鈧鍩€椤掆偓閻忔艾顭垮鈧弫鍐Ψ瑜忛惌澶愭煙閻戞ɑ鐓涢柛瀣崌閺佹劖鎯旈垾鑼泿闂備礁鎲￠幐鎼佹偉閻撳寒娼栭柧蹇撴贡绾惧吋淇婇婵愬殭缁炬澘绉归弻锝嗘償閵堝骸娈愰梺绋款儏鐎氫即宕洪妷锕€绶為柟閭﹀墻濞煎﹪姊洪崘鍙夋儓闁稿﹦鎳撻埢宥夊冀閵娧呯槇闂佹眹鍨藉褎绂掗敃鍌涚厱闁靛鍎抽崺锝夋煙椤斻劌娲ょ猾宥夋煕鐏炲墽绠撶亸蹇撯攽閻樺灚鏆╁┑顔炬暩閸犲﹤顓兼径濠勫幈闂佺鎻梽鍕偂閺囥垺鐓ラ柡鍐ㄦ祩閸ゆ瑦顨ラ悙瀵稿ⅹ闂囧绻濇繝鍌涘櫣濞存粓绠栭弻鏇㈠炊瑜嶉顓炩攽閳╁啯鍊愬┑锛勫厴閺佸啴鍩€椤掆偓閳绘挸螖閸涱喖鈧敻鎮峰▎蹇擃仾濠㈣泛瀚伴弻娑㈠Ω閵壯冪厽闂侀潧妫旂粈渚€鍩ユ径鎰潊闁炽儲鏋奸崑鎾绘倻閼恒儳鍘告繝銏ｆ硾椤戝懘鎮樺澶嬬厱閻庯綆鍋呭畷宀€鈧娲栭妶绋款嚕閹绢喗鍋勯柛婵嗗缁犱即姊婚崒娆戝妽闁诡喖鐖煎畷鏇㈩敋閳ь剙顕ｉ幓鎺嗘闁靛繒濮烽ˇ顓㈡煛婢跺﹦澧戦柛鏂挎捣缁?
                        const QVector3D setupNormal = normalizedOr(normal, QVector3D(0.0f, 0.0f, 1.0f));
                        const QQuaternion rotation =
                            QQuaternion::rotationTo(setupNormal, QVector3D(0.0f, 0.0f, 1.0f));
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
#endif
                        const MeshData &setupMesh = project->mesh();
                        const QVector<MachiningFeature> &setupFeatures = project->features();

                        m_viewport->setMesh(setupMesh);
                        m_viewport->clearHighlight();
                        m_viewport->clearToolPath();
                        m_featurePanel->setFeatures(setupFeatures);
                        m_operationPanel->setOperations(project->operations());
                        m_simCtrl->setMesh(setupMesh);
                        m_simCtrl->loadGCode(QString());
                        m_gcodeEditor->setGCode(QString());
                        clearStrategyPanelSelection(m_strategyPanel);

                        m_settingFrontFace = false;
                        m_pendingFrontFaceIndex = -1;
                        m_actSetFrontFace->setChecked(false);
                        m_bottomBar->setStatus(tr("Front-face setup updated."));
                    } else {
                        // 缂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ょ紓宥咃躬瀵鎮㈤崗灏栨嫽闁诲酣娼ф竟濠偽ｉ鍓х＜闁绘劦鍓欓崝銈囩磽瀹ュ拑韬€殿喖顭烽弫鎰緞婵犲嫷鍚呴梻浣瑰缁诲倿骞夊☉銏犵缂備焦顭囬崢杈ㄧ節閻㈤潧孝闁稿﹤缍婂畷鎴﹀Ψ閳哄倻鍘搁柣蹇曞仩椤曆勬叏閸屾壕鍋撳▓鍨灍闁瑰憡濞婇獮鍐ㄢ枎瀵版繂婀遍埀顒婄秵娴滄瑦绔熼弴銏♀拺闁告挻褰冩禍婵囩箾閸欏澧甸柟顔惧仱瀹曞綊顢曢悩杈╃泿闂備胶鎳撻幖顐⑽涘Δ浣侯洸濡わ絽鍟埛鎴︽煕濠靛嫬鍔氶柡瀣捣閻ヮ亞绱掗姀鐘茬闂佸憡甯楃敮鐐哄箯閻樿鍦偓锝庡亽濞兼梹绻濈喊妯活潑闁搞劌鐖煎銊╂焼瀹ュ繒绋忛悗骞垮劚閹冲寮ㄦ禒瀣厓闁芥ê顦伴ˉ婊堟煟韫囥儳绉柡灞界Т閻ｏ繝骞嶉纰辨毇闂佸憡顨夋ご鎼佸Φ閸曨垰鍐€闁靛ě浣插亾閹烘梻纾奸柍閿亾闁稿鎹囧缁樻媴閸︻厽鑿囬梺鎼炲姀濡嫰鈥﹂崶顏嶆Ъ缂備礁鍊圭敮锟犲极閸愵喖纾兼繛鎴炶壘楠炲牓姊绘笟鈧褔鈥﹂崼銉ョ鐎广儱娲﹂崗婊堟煟閹伴潧鍘靛ù婊勭矋閵囧嫰骞囬崜浣瑰仹缂備胶濮烽崑娑㈠煘閹达箑鐒洪柛鎰ㄦ櫅閳ь剚鍔欓弻娑㈠煘閹冣拤闂佺懓鍢查幊蹇曠箔閻旂⒈鏁嶆繛鎴炵懄濞堛垹鈹戦悩鍨毄濠殿喚鏁婚、娆撳冀椤撶偟鐛ュ┑掳鍊曢幊澶愬焵椤掑﹦鐣电€规洖銈告俊鐑藉Ψ閿斿彨姘舵⒒娴ｅ憡鎯堢紒瀣╃窔瀹曘垺绂掔€ｎ亞锛涢梺绋挎湰缁嬫挾绮绘ィ鍐╃厵閻庣數顭堥崜鍐层€掑顓犳创闁哄矉缍佸顒勫箰鎼搭喗锛嗛梻浣筋嚃閸犳盯锝炴径濠庣劷妞ゅ繐鐗嗙粻顖溾偓鍏夊亾濠电姴鍊搁煢闂傚倸鍊烽懗鍫曗€﹂崼鐕佹闁告縿鍎插畷鍙夋叏濡炶浜鹃悗娈垮枛椤兘寮幇鏉块唶闁靛繈鍨哄鎴︽⒒娴ｇ儤鍤€濠⒀呮櫕濡叉劙鎮㈠ú缁樷枌闂備礁鎼径鍥焵椤掆偓绾绢參寮抽崱娑欏€甸柨婵嗛婢т即鏌ㄥ☉姘灈婵﹥妞介弻鍛存倷閼艰泛顏繝鈷€鍐ㄢ挃缂佽鲸鎸搁濂稿川椤曞懏锛佺紓鍌欑閸婂摜绮旇ぐ鎺嬧偓渚€寮崼婵嬪敹濠电娀娼ч悧蹇涘储椤掑嫭鈷掑ù锝呮啞閸熺偞绻涚拠褏鐣电€规洖缍婇弻鍡楊吋閸℃ぞ绨垫繝鐢靛仦閸垶宕瑰ú顏勭９闁割偅娲橀悡鐔兼煙娴兼潙浜伴柡澶嬫そ閺屾盯濡堕崱娆愬櫑闂佸疇顫夐崹鍧楀垂妤ｅ啯鍊烽柦妯侯槷閸掓帡姊绘担鍛婃儓闁兼椿鍨崇划鏃囥亹閹烘垹顔囨繝鐢靛Т閸燁偆娆㈤悙缈犵箚妞ゆ牗绋愰幉楣冩煥閺囩偛鈧綊鎮￠敐鍚ゅ綊宕楅懖鈺傚櫘闂佸憡甯婇崡鎶藉蓟閺囥垹骞㈡俊顖濇閻熴劌顪冮妶搴′簼缂佽鐗撻獮鍐焺閸愨晛鍔呴梺鎸庣箓鐎氼參宕哄☉銏♀拻闁稿本鐟︾粊鐗堛亜閺囩喓澧电€规洘濞婇、娑橆煥閸愵亞鐡樺┑鐐差嚟婵挳顢栭崨瀛樺亗婵せ鍋撻柡宀€鍠愬蹇斻偅閸愨晩鈧秹姊虹粙鍖¤含妞ゃ儲鎹囬崺鈧い鎺戝€归崵鈧繝銏㈡嚀閿曨亜鐣锋导鏉戝唨鐟滃寮搁弮鍫熺厾闁告縿鍎查弳鈺冪磼閳锯偓閸嬫挻绻濋悽闈涗粶闁绘妫濋幃妯衡攽鐎ｎ偄鈧爼姊洪鈧粔鐢告偂閺囩喓绠鹃柛鈩冾殘缁犱即鏌￠崱妯兼噰闁哄矉缍侀弫鎰板炊閵娧冨Ы闂備線娼уú锕傚礉濞嗘挾宓侀柟鐑橆殔缁狅綁鏌ㄩ弴妤€浜鹃梺杞扮缂嶅﹤顫忕紒妯诲闁告稑锕ラ崕鎾斥攽閻愯尙婀撮柛鏃€鍨甸悾鐑藉箣閿旇棄浜归柣鐘叉穿鐏忔瑩顢欓幋锔解拺闁告繂瀚婵嬫煕鐎ｎ偄濮嶇€殿喓鍔嶇粋鎺斺偓锝庡亞閸樹粙姊鸿ぐ鎺戜喊闁告鏅槐鐐哄箣閿旂晫鍘介棅顐㈡处濞叉牠寮稿☉銏＄厸閻忕偛澧藉ú鏉戔攽閳╁啯鍊愬┑锛勫厴閺佸啯鎷呮笟顖涙暏缂傚倸鍊搁崐椋庢閿熺姴绐楅柡宥庡幐閳ь剨绠撻幃婊堟寠婢跺鈧剟姊洪崷顓烆暭婵犮垺锕㈤悰顕€濮€閿涘嫮顔曢梺绯曞墲閿氶柣蹇婃櫆娣囧﹪骞撻幒鎾存倷闂佸疇顫夐崹鍧楀箖閳哄懎鍨傛い鎰剁稻閻﹀骸鈹戦悩鎰佸晱闁哥姵顨婇獮鎰板礃閼碱剚娈鹃梺褰掑亰閸剚绂嶈ぐ鎺撶厵闁绘垶锚閻忓瓨淇婇銈呭幋婵﹪缂氶妵鎰板箳閹垮嫮鍚归梻浣虹帛鐢亞绮婚幘宕囨殾闁硅揪闄勯弲鎼佹煟濡櫣锛嶉柛妯绘倐濮婃椽骞栭悙鎻掑闂佸搫鎳忕粙鎺旂矉閹烘鏅滃┑顔藉姃缁ㄥ姊洪棃娑崇础闁告劕鍟ㄩ崕宕囨閹烘挻缍囬柕濞垮劤閻熸彃鈹戦悙闈涘付缂佺粯蓱娣囧﹪骞栨担瑙勬珖闂侀€炲苯澧存俊顐㈠椤撳吋寰勭€Ｑ勫闂備浇宕甸崰鎰熆濡綍锝嗙節濮橆厾鍘甸梺鍛婂姌鐏忔瑧绮诲畷鍥╃＜妞ゆ梻鈷堥崕鏃€顨ラ悙鏉戞诞鐎殿噮鍓熷畷顐﹀礋椤忓嫷妫滈梻鍌欑劍鐎笛呮崲閸屾粏濮抽柤娴嬫櫈婵啿鈹戦崒姘暈闁绘挻鐟╅弻褑绠涢敐鍛敖闂佺懓鍟垮ú锕傚箞閵婏妇绡€闁告洦鍘肩粭锟犳⒑閻熸澘妲婚柟铏耿楠炴劖绻濋崘銊х獮婵犵數濮撮崯顐﹀礉閹烘鈷掗柛灞剧懅椤︼箓鏌熼懞銉х煉鐎规洜澧楅幆鏃堝煡閸℃瑥濮洪梻浣哄仺閸庢煡宕滃璺哄惞闁哄洢鍨洪悡娑㈡倶閻愰鍤欓柛鏃€绮撻弻娑氣偓锝庡亜婵绱掓潏銊﹀鞍闁瑰嘲鎳忛幈銊╁箣椤撴繄鍑圭紓浣稿€圭敮锟犵嵁閹烘绠ｆい鎾跺枎閻︽粓姊绘笟鈧褔鎮ч崱妞㈡稑鈻庨幘宥咁樀瀹曞ジ濡烽敂鎯у箰濠电偞鎸婚懝鎯洪妶鍛瀺鐎广儱娲犻崑鎾舵喆閸曨剛顦ㄩ梺鎼炲妼濞硷繝鎮伴鍢夌喖鎳栭埡鍐跨床婵犵妲呴崹宕囧垝椤栫偞鍋熼柟鎯板Г閳锋垹鐥鐐村櫣濞存粌缍婇幃璺衡槈閺嵮冨Е閻庤娲樼划宀勫煘閹寸姭鍋撻敐搴′簻闁逞屽墮閻栧ジ寮诲☉銏╂晝闁绘ɑ褰冩慨鏇㈡⒑閹惰姤鏁遍柛銊ユ健瀵鈽夊Ο閿嬵潔濠殿喗顨呴悧鍡樺閹邦厾绡€婵炲牆鐏濋弸鎾绘煕鐎ｎ偅宕屾慨濠勭帛閹峰懏绗熼婊冨Ъ婵＄偑鍊栭崹闈浳涘┑瀣祦闁硅揪绠戦悙濠冦亜閹哄棗浜鹃梺钘夊暟閸犳牠寮婚妸銉㈡斀闁糕檧鏅滄晥闂備胶顭堥鍛偓姘嵆瀵鈽夊Ο閿嬫杸闂佸憡娲﹂崑鍕叏閵忋倖鈷戠紓浣股戠亸銊╂煕鐎ｎ偅灏电紒杈ㄦ崌瀹曟帒顫濋钘変壕濡炲娴烽惌鍡椼€掑锝呬壕闁芥ɑ绻冮妵鍕冀閵娧呯厒缂備讲鍋撳┑鐘插€甸弨浠嬫煟濡搫绾х€瑰憡绻勯埀顒冾潐閹搁娆㈠璺鸿摕婵炴垯鍨圭粻娑㈡煃鏉炴壆顦︽い銉ヮ儔濮婃椽宕崟顐ｆ疁闂佺顑嗛幑鍥蓟閻旈鏆嬮柣妤€鐗嗗▍锝夋煟閹惧崬鈧牠濡甸崟顔剧杸闁圭偓娼欓娑㈡⒑闁偛鑻晶顔剧磼婢跺本鍤€妞ゎ偄绻掔槐鎺懳熺拠宸偓鎾绘⒑閸涘﹦鈽夐柨鏇樺€濆鎶藉醇閵忋垻锛濇繛杈剧到婢瑰﹪宕曢幇鐗堢厱闁靛鍎查崑銉р偓娈垮櫘閸嬪﹤鐣峰鈧、娆撴嚍閵夛妇褰囬梻鍌欒兌椤牓寮甸鍕殞濡わ絽鍟壕鑽も偓骞垮劚椤︿即鍩涢幒妤佺厱閻忕偟鍋撻惃鎴濐熆瑜庨惄顖炲蓟濞戞粎鐤€闁哄倸鐏濋幗鐢告倵濞堝灝娅橀柛锝忕到閻ｉ攱绺介崨濠備簻闂佺偓鑹鹃崐褰掓儓韫囨稒鈷掗柛灞捐壘閳ь剚鎮傚畷鎰板箹娴ｇ懓浜辨繝鐢靛Т鐎氼噣鎯屽▎鎾寸厵闂侇叏绠戦弸娑㈡煕閵婏妇绠為柡灞剧洴椤㈡洟鏁愰崱娆樻О闂備焦濞婇弨閬嶅垂閸ф绠栫憸鐗堝笒閻愬﹥銇勮箛鎾愁伀婵絻鍨荤槐鎾存媴閸濆嫅顒勬倶韫囨梻鎳呮俊鍙夊姍楠炴帡寮埀顒傗偓姘哺閺屻倗鍠婇崡鐐差潻闂佹剚浜褑鐏冮梺缁橈耿濞佳勭濠婂牊鐓曢柣鏇氱娴滀即鏌ㄥ┑鍫濅粶妞ゎ厹鍔戝畷鐓庘攽閸繂袝濠碉紕鍋戦崐鏍暜閹烘柡鍋撳鐓庡⒋鐎殿喕鍗虫俊鐑藉煛閸屾粌寮抽梻浣虹帛閺屻劑骞栭銏㈡懃闂佽娴烽幊鎾诲箟閿涘嫭宕查柛鎰靛枛妗呴梺鍛婃处閸ㄦ壆鐚惧澶嬬厓鐟滄粓宕滈悢椋庢殾闁哄洢鍨圭粻顕€鏌ｉ敐鍛板缂佷緤绠撳娲礈閹绘帊绨撮梺绋垮閻擄繝宕哄☉銏犵闁圭偨鍔岀紞濠囧极閹版澘宸濇い鏃囨閺嬫垿姊绘担鍛婃儓妞ゆ垵鎳樺畷顖烆敍濞戞埃鏀虫繝鐢靛Т閸嬪棗銆掓繝姘厽闁圭偓濞婇妤併亜椤愩垺鍠樻慨濠呮濞戠敻宕ㄩ鎯ф锭婵＄偑鍊戦崝宀勬晝閵堝鍋╅梺鍨儑闂勫嫮绱掔€ｎ亞浠㈢€规挸妫濆铏圭磼濡崵鍙嗛梺姹囧妽缁诲牆鐣烽幋锕€绠婚悹鍥ㄧ叀閺佹粌鈹戞幊閸婃捇鎳楅崼鏇炲偍闁告鍋愰弨浠嬫煟濡偐甯涙繛鎳峰洦鍊垫慨妯煎帶婢у鈧娲樼换鍌濈亙闂佸憡渚楅崰姘跺储闁秵鐓熼幖鎼灣缁夌敻鏌涚€ｎ亝鍣藉ù婊勬倐閹粙宕ㄦ繝鍕箥闂備礁鎲￠悷銉┧囬幍顔荤剨闁挎棃鏁崑鎾舵喆閸曨剛顦梺鍝ュУ閻楃娀濡存担鑲濇棃宕ㄩ鐙呯床闂備線娼ч悧鍡涘蓟婵犲洦鍊锋繛鏉戭儐閺傗偓闂備焦瀵х粙鎴犫偓姘煎墯缁傚秵绺介崨濠勫幈婵犵數濮撮崯顐︽倶鐎电硶鍋撶憴鍕缂佽鍊块幃鎯р攽鐎ｎ亞顦板銈嗗笂閻掞箓鎮甸弽顓熲拻濞撴埃鍋撴繛浣冲洦鍋嬮柛鈩冭泲閸ャ劎顩烽悗锝庝簼閻庮剟鏌ｆ惔顖滅У闁哥姵顨婇幃锟犲Ψ閿斿墽鐦堥梻鍌氱墛缁嬫帡鏁嶅鍡曠箚闁圭粯甯炵粔娲煛鐏炲墽銆掗柍褜鍓ㄧ徊濠氬礉鐏炵偓鍙忛柛宀€鍋為悡娑㈡倶閻愭彃鈷旈柍钘夘樀閺屽秹鎸婃径妯恍﹀銈庡亝缁挸鐣烽悡搴樻斀闁告侗鍨抽悡鎴濃攽閻樺灚鏆╅柛瀣洴椤㈡岸顢橀悢绋垮伎闂傚倸鐗婃笟妤€銆掓繝姘參婵☆垯璀﹀Ο鍫熺箾閸忚偐澧甸柡灞剧☉閳规垿宕卞Δ鍐ㄧ到闁荤喐绮嶉弻銊╂偩閻戣棄绠氶梺顓ㄩ檮闉嬫繝鐢靛仜椤曨厽鎱ㄩ幘顔嘉х紒瀣儥濞兼牠鏌ц箛姘兼綈闁稿繑绮撻弻娑橆煥閳ь剛绮堟担绯曟灁婵犻潧鐗忕壕钘壝归敐鍫燁仩閻㈩垱绋撶槐鎺旀嫚閼碱剙鈪甸梺璇″枙閸楁娊銆佸璺虹劦妞ゆ巻鍋撻柣锝囧厴瀹曪繝鎮欓埡鍌ゆ綌婵犵妲呴崹鎶藉煕閸惊锝夋偋閸粎绠氶梺缁樺姦娴滄粓鍩€椤掍胶澧摶鐐裁归敐鍥╂憘闁哄棔鍗冲缁樻媴閻戞ê娈岄梺鎼炲灪閻擄繝鍨鹃敃鈧悾锟犳焽閿旂晫绋侀梻浣瑰劤缁绘劕锕㈤柆宥嗗剹婵炲棙鎸婚悡娆戠磼鐎ｎ亞浠㈡い鎺嬪灲閺岀喖鎮滈幋鎺撳枤濠殿喖锕ュ浠嬪箖閳╁啯鍎熼柨婵嗘肠閵娧呯＝濞达絽鎼牎闂佺粯顨堟慨鎾偩瀹勬噴娲敂閸曨厼濮︽俊鐐€栫敮濠囨⒔瀹ュ棛顩叉繝濠傜墛閻撴瑩鏌涘┑鍡楊伀閼叉牠姊洪崨濠冪叆缂佸缍婂璇测槈濞嗘垹鐦堥梺鍛婂姂閸斿矂锝為幒妤佲拺闁告繂瀚悘閬嶆煕閻樺磭澧甸柕鍡曠閳诲酣骞樺鍕ㄦ櫊閺屾洘寰勯崼婵嗗闂佽绻戦幑鍥ь潖閾忓湱纾兼俊顖濐嚙閽勫ジ姊虹粙鎸庢崳闁轰浇顕ч锝囨嫚濞村顫嶉梺闈涚箳婵兘鎮块埀顒佷繆閻愵亜鈧牕顔忔繝姘；闁瑰墽绮悡鍐⒑閸噮鍎愭い銉ョ墦閺岋紕浠﹂崜褎鍒涢梺璇″枟閻熲晠骞婇悩娲绘晞閻犳亽鍔戦埞蹇曠磽閸屾艾鈧兘鎳楅崜浣稿灊妞ゆ牗绮庨惌娆撴煙閻戞ê鐒鹃柣鎺嶇矙閻擃偊宕堕妸褉妲堢紓浣插亾闁割偁鍨洪崰鎰板箹濞ｎ剙濡肩痪鎹愬吹閹叉悂寮▎鐐稁闂佹儳绻愬﹢閬嶆儗濞嗘挻鍋ｉ柟顓熷笒婵¤姤淇婂ù瀣壕闂傚倸鍊烽懗鍫曗€﹂崼銉︽櫇闁靛／鍕簥闂佸壊鍋侀崕閬嶅几娓氣偓閺岀喖骞嶉纰辨毉闂佺锕﹂崗姗€寮诲☉妯锋斀闁糕剝顨忔禒濂告倵鐟欏嫭灏俊顐ｇ箓椤繘鎼归崷顓狅紲濠碘槅鍨抽崕鐢稿箯鐠囧樊娓婚柕鍫濋楠炴牠鏌ｅΔ鈧Λ婵嬨€佸鑸垫櫜濠㈣泛锕ら悗顓烆渻閵堝棙顥嗘い顐㈩樀瀵剟鍩€椤掑嫭鈷掑ù锝呮憸閺嬪啯淇婇銏狀仼閾荤偞淇婇妶鍛櫤闁稿﹤鐖奸弻锝呂熼懖鈺佺闂佺粯鎸鹃崰鏍偂椤愶箑鐐婇柕濠忚吂閹疯崵绱撴担鍝勑ラ柛瀣ㄥ€濆濠氭偄绾拌鲸鏅梺绯曞墲閻熝囨儊閸惊鏃堟偐闂堟稐绮跺┑鐐叉▕閸欏啴鐛崘顓ф▌閻庤娲栭妶鍛婃叏閳ь剟鏌ｅΟ鍨毢妞ゆ柨娲濠氬磼濮橆兘鍋撴搴ｇ焼濞撴埃鍋撴鐐差樀閺佸秹宕熼銏喊闂備礁澹婇崑渚€宕规繝姘剹闁糕剝顦鸿ぐ鎺撴櫜闁割偒鍋呯紞鍫濃攽閻愬弶鍣藉┑顔炬暬婵＄敻宕熼姘敤濡炪倖鍔﹀鈧繛宀婁邯濮婅櫣绱掑Ο璇查瀺闂佽崵鍠嗛崕鍨繆閹绢喖绠抽柟鎼幗閸嶉潧顪冮妶鍡楃瑨闁稿﹦鍏橀幖瑙勬償閵忋垻鐦堢紒鍓у鑿ら柛瀣崌閹瑩鎸婃径澶婂灊闂傚倷鑳堕、濠偽涢崟顖涘亯濠靛倻顭堥弸渚€鏌熼柇锕€骞栫紒鍓佸仱閹鏁愭惔鈥愁潻濡ょ姷鍋涢悧濠勬崲濠靛鍋ㄩ梻鍫熺▓閺嬪懎鈹戦悙鏉垮皟闁稿繒鍋撶粙鎴ｇ亙闂佸憡绮堥悞锕傚疾濠婂牊鈷戦柛锔诲弨濡炬悂鏌涢悩鎰佹疁闁诡喒鈧枼妲堥柕蹇ョ磿閸橀亶姊洪弬銉︽珔闁哥噥鍋呴幈銊╁炊椤掍胶鍘介棅顐㈡处濮婂宕ｉ埀顒勬⒑閸濆嫮鐏遍柛鐘崇墵閵嗕礁鈻庨幘鏉戜簵闁硅偐琛ラ幊锝夊煛娓氬洦瀵岄梺闈涚墕妤犳悂鐛幋锔界厱闁哄啠鍋撻柛銊ф暬閹箖鎮滈懞銉ヤ簵闁圭厧鐡ㄨ摫闁哄倵鍋撻梻鍌欑閹测剝绗熷Δ鍛偍濞寸姴顑嗛崐鐢告煙閹澘袚闁抽攱甯￠弻娑氫沪閸撗勫櫘闂佸憡鏌ㄧ粔褰掑蓟閻旂⒈鏁婇柤娴嬫櫅閻撶喖鎮楃憴鍕缂佽瀚伴崺鈧い鎺戯功缁夌敻鏌涢悩宕囧⒈婵炴垹鏁婚崺鈧い鎺戝閳锋垿鏌涘☉姗堝伐濠殿喖鏈妵鍕即閻旇櫣鐓夐梺纭呮珪閹瑰洤鐣疯ぐ鎺濇晩婵﹩鍓涚粔娲煛娴ｇ懓濮嶇€规洖鐖奸崺鐐存償閹惧厖澹曟繝鐢靛Т濞诧箓鎮″☉銏＄厓鐟滄粓宕滃杈╃焿闁圭儤鏌￠崑鎾绘晲鎼粹€茬敖闂佸憡顭堝Λ鍕煘閹达箑鐓￠柛鈩冾殘娴狀厼顪冮妶鍡楃仸闁荤啿鏅涢悾鐑芥偨绾版ê浜鹃柨婵嗛閸樻挳鏌涚€ｎ偅灏甸柟鍙夋尦瀹曠喖顢楅埀顒佺閳哄懏鈷戠紒瀣閸炲绱掗鈧粻鏍箖濮椻偓瀹曟﹢顢欑憴锝嗗缂傚倸鍊烽悞锕傚Υ閻愬搫绠ｉ柨鏃傜帛閺呪晠姊洪崫鍕枆闁告ü绮欏畷鎰板垂椤斻儲妫冮弫鎰板川椤撶喐顔夐梻浣虹帛閹告悂宕幘顔肩畺鐟滅増甯掗悙濠冦亜韫囨挸顏慨锝冨灲閹鈽夊▎鎴犵暤濡炪値浜滈崯瀛樹繆閸洖骞㈡俊顖濇椤ｆ煡姊绘笟鈧褔鎮ц箛娑掆偓锕傚醇閵夛箑浠奸梺缁樺灱婵倝鎮為懖鈹惧亾楠炲灝鍔氶柟铏姍閹潡鍩€椤掍椒绻嗛柣鎰典簻閳ь剚娲滈幑銏ゅ箛椤戔晜绋戦埥澶娢熼柨瀣簷闂備胶顫嬮崟鍨暦闂佹娊鏀辩敮锟犲蓟濞戞矮娌柛鎾楀嫬娅橀梻浣告啞閺屻劎绮旇ぐ鎺戠畺婵°倕鍟扮粻鏃€绻涢幋鐐嗘垿宕抽搹鍏夊亾鐟欏嫭绀冮柣鎿勭節瀵鈽夊Ο鍏兼畷闂侀€炲苯澧寸€规洘鍨甸埥澶婎潩椤掑顥￠柣鐔哥矌婢ф鏁Δ鍛亗闁绘柨鍚嬮悡鐔兼煛閸愩劌浜為柣鎺斿亾閵囧嫰濡烽敃鈧慨宥夋煛瀹€瀣？闁逞屽墾缂嶅棙绂嶉悙瀛樻珡闂傚倷绀侀幖顐﹀嫉椤掑嫭鍎庢い鏍ㄧ◥缁诲棝鏌ｉ幋鐘垫憘闁轰礁锕弻锝夊箻閸愯尙妲板┑鐐烘？缁瑥顫忕紒妯诲闁惧繒鎳撶粭锟犳⒑閸︻収鐒炬い顓犲厴楠炲啴鎮滈挊澶屽幐闂佸憡渚楅崣鈧柟鑺ユ礀閳规垿鎮欓弶鎴犱桓闁艰￥鍊濋弻锛勨偓锝庝邯閸欏嫰鏌ｉ幙鍐ㄤ喊鐎规洝鍩栭ˇ鐗堟償閿涒晜鍨块弻鈩冩媴閸濄儱鈪靛┑顔硷龚濞咃綁骞忛悩缁樺殤妞ゆ帒鍋嗛崬璺衡攽鎺抽崐妤佹叏鐎靛憡宕查柟瀵稿仧閳瑰秴鈹戦悩鍙夋悙闁绘劕锕ラ妵鍕箳閸℃ぞ澹曢梻浣虹帛鐢帡鎮樺璺何﹂柛鏇ㄥ灠缁犳娊鏌熺€涙绠栭柣蹇旀尭閳规垿鎮欓幓鎺撳€梺鍛婃⒐閻楁粓骞戦姀鐘闁靛繒濮烽悡鎴炵節閵忥絾纭鹃柤娲诲灦閸╂稓浠﹂崜褏鐦堢紒鍓у鑿ら柛瀣尭閻ｇ兘宕惰閸樿姤淇婇妶鍥ラ柛瀣仱閹兾旈崨顓狀唵闂佸憡渚楅崹浼村几閸喍绻嗘い鏍ㄧ箖閵嗗啫顭跨憴鍕缂佺粯绋掑蹇涘礈瑜忚ⅲ闂備胶顭堥敃锝囧垝濞嗗浚鍤曟い鏇楀亾鐎规洜鍘ч埞鎴﹀幢濞嗘垵鏄ユ繝纰夌磿閸嬫垿宕愰弽顓炵闁绘劦鍓欓崹鏃堟倵闂堟稒鍟炴い顐ｆ礋閺屾稑鈻庤箛锝喰ㄧ紓浣叉閸嬫捇姊绘担鍦菇闁搞劏妫勯…鍥槻闁烩槅鍙冨缁樻媴閻熼偊鍤嬪┑鐐村絻缁绘ê鐣烽幇顑╂棃宕ㄩ鐘插Е婵＄偑鍊栫敮濠囨嚄閸洖鐓濋柟鍓х帛閻撴盯鏌涘☉鍗炴灓婵炴彃顕埀顒侇問閸犳绻涙繝鍥ф槬闁跨喓濮寸粈鍐┿亜韫囨挻锛旂紒杈ㄥ▕濮婄粯鎷呮笟顖滃姼闂佸搫鐗滈崜鐔煎箠濠靛洢鍋呴柛鎰╁妿閸旓箑顪冮妶鍡楃瑐闁绘帪绠撳畷鎰板箛椤旂懓浜鹃悷娆忓缁€鍐煕閺冣偓閻熲晠鎮伴鈧浠嬪Ω閿曗偓椤庢捇姊洪崨濠勭細闁稿氦宕靛Σ鎰邦敋閳ь剙顫忕紒妯诲闁惧繒鎳撶粭锟犳⒑閹肩偛濡奸柛姘儔楠炲牓濡搁埡浣勓冾熆鐠虹尨鍔熼柨娑欑懇濮婅櫣绮旈崱妤€顏存繛鍫熸礋閺岋綁骞橀崡鐐插Е闂佸搫鐭夌换婵嗙暦閹烘埈娼╅柛娆愵焾濡炬悂姊绘担鐟扳枙闁衡偓閸楃倣娑㈠礃椤旇壈鎽曢梺鎸庣箓椤︿粙寮崶顒佺厽闁哄啫鍊搁崝瀣煕閿濆嫬宓嗘慨濠冩そ瀹曨偊宕熼鐘辩礃闂備礁鎽滄慨鎾煀閿濆鏄ラ柕蹇婂墲閸庣喖鏌曟繝蹇撶槣闁逞屽墯閸旀洟鈥旈崘顔嘉ч柛鈩冾焾閸嬩線姊虹粙璺ㄧ濠殿噣娼ч—鍐╃鐎ｃ劉鍋撴担鍓叉建闁逞屽墴閺佹劙鎮欐笟顖涙櫈闂佽姤锚椤︻喗绔熼弴鐔虹閺夊牆澧介幃鑲╃棯椤撯剝纭鹃崡閬嶆煕椤愩倕鏋戦柛娆忕箲娣囧﹪濡堕崒姘婵犵妲呴崑鍛存儎椤栨氨鏆﹂柨婵嗩槸楠炪垺绻涢幋鐐垫喛闁归攱妞藉娲川婵犲嫧妲堥梺鎸庢磸閸庡磭鍒掗崼鈶╁亾閿濆骸浜炵紒鐘冲劤闇夐柨婵嗘噹閺嗛亶鎮楀鐓庢珝闁靛棗鎳橀弻銊р偓锝庡墰椤旀洟鎮楅悷鏉款棌闁哥姵娲滈懞杈ㄧ附閸涘﹦鍘撻梻浣哥仢椤戝懐绮幒妤侇梿濠㈣埖鍔栭悡銉︾節闂堟稒顥犲褎绋掗妵鍕疀閺囩偐鏋呴梺璇″枙缁瑩銆佸☉妯锋婵﹫绲介～宀勬⒒娓氣偓閳ь剚绋撻埞鎺楁煕閺傝法肖闁瑰箍鍨归埞鎴犫偓锝庝簽閿涙粓姊洪棃娑氬婵炲眰鍊濋獮鍐箣閿旂晫鍘介柟鍏肩暘閸娿倕顭囬幇顓犵闁告瑥顥㈤鍡楀疾闂備胶绮Λ鍐疾閼碱剚宕查柛鈩冪⊕閻撶娀鏌熼梻瀵稿妽婵炴嚪鍛＜闁告挷绀佹禒婊堟煃鐟欏嫬鐏撮柟顔规櫊楠炴捇骞掑┑鍛濠电姷顣介埀顒冩珪閳绘洟鏌涢妸銊ゅ惈闁瑰箍鍨归埞鎴犫偓锝庡亜娴犳椽姊婚崒姘卞闁告巻鍋撻梺闈涚箞閸婃牠鎮￠妷鈺傜厸闁搞儺鐓侀鍫濈劦妞ゆ帊绶″▓婊堟煕閳规儳浜炬俊鐐€栫敮濠勭矆娓氣偓楠炴牠骞栨担鍦幈闂佸搫鍊藉▔鏇″€寸紓鍌欑贰閸犳牠顢栭崨鎼晣濠靛倻顭堝钘壝归敐鍛儓鐎殿喗濞婂缁樻媴缁嬫妫岄梺绋款儏濡繂鐣锋导鏉戠閻犲洦褰冮崑宥夋⒑瑜版帗锛熼柣鎺炵畵閸╂盯骞嬮敂鐣屽幈濠电偞鍨堕敃顐㈩啅閵夈儮鏀芥い鏂诲妼閹虫劗澹曟總鍛婄厾闁煎湱澧楃涵鍓ф偖濮樿京纾藉ù锝囶焾閳ь剙鎽滅划鏃囥亹閹烘柨绁﹂梺绯曞墲閸戠懓顬婇妸銉㈡斀闁绘劕鐡ㄧ亸顓熴亜椤撶姴鍘寸€殿喖顭峰鎾閻樿鏁规繝鐢靛█濞佳兠归崒姣兼盯鎮欓悜妯锋嫼闁荤姴娲﹂悡锟犳偘濠婂懐纾奸柣妯挎珪瀹曞瞼鈧鍠栭…鐑藉极閹邦厼绶為悗锝庝簷缁ㄥ姊绘担鍛婂暈闁荤喆鍎佃棟濞寸厧鐡ㄩ崑鐔兼煛閸愩劎澧涢柍?
                        m_pendingFrontFaceIndex = faceIndex;
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

    connect(m_strategyPanel, &StrategyPanel::generateRequested,
            this, [this](const HoleFeature &feature, const QString &stratId, const StrategyParams &params, const ToolEntry &tool) {
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
            for (int index : m_featurePanel->checkedFeatureIndices()) {
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
        if (!selectedHoles.isEmpty()) {
            selectedFeature = selectedHoles.first();
            m_strategyPanel->setFeature(selectedFeature);
        }

        if (selectedHoles.size() > 1)
            selectedHoles = sortHolesByNearestNeighbor(selectedHoles);

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


        const ToolpathResult result = batchHoles
            ? s->generate(selectedHoles, tool, params)
            : s->generate(selectedFeature, tool, params);
        if (!result.ok || result.gcode.isEmpty()) {
            onErrorOccurred(result.errorMsg.isEmpty() ? tr("G-code generation failed.") : result.errorMsg);
            return;
        }

        PostProcessorOptions opts;
        opts.programNumber = QStringLiteral("O0001");
        opts.addComments = true;
        opts.workOffset = currentWorkOffset();
        PostProcessorBase *pp = PostProcessorRegistry::instance().get(
            Settings::instance().postProcessorId());
        if (!pp) {
            pp = PostProcessorRegistry::instance().get(QStringLiteral("siemens"));
        }
        if (!pp) {
            onErrorOccurred(tr("No post-processor available."));
            return;
        }

        const QString allGCode = pp->wrapGCode(result.gcode.split('\n'), opts);
        m_gcodeEditor->setGCode(allGCode);
        m_simCtrl->loadGCode(allGCode);
        m_bottomBar->showProgress(true);
        m_bottomBar->setStatus(tr("Generated G-code."));

        if (batchHoles) {
            for (const HoleFeature &hole : selectedHoles) {
                m_operationPanel->addHoleOperation(hole, stratId, params, tool.id);
            }
        } else {
            m_operationPanel->addHoleOperation(selectedFeature, stratId, params, tool.id);
        }
    });

    connect(m_strategyPanel, &StrategyPanel::circleMillRequested,
            this, [this](const ContourFeature &feature, const StrategyParams &params, const ToolEntry &tool) {
        StrategyBase *s = StrategyFactory::instance().strategy(QStringLiteral("mill_circle")).get();
        if (!s) {
            onErrorOccurred(tr("Circle milling strategy not found."));
            return;
        }

        const QString slotError = slotSetupRestrictionMessage(feature, QStringLiteral("mill_circle"), tool, params, isChineseUi());
        if (!slotError.isEmpty()) {
            onErrorOccurred(slotError);
            return;
        }

        const ToolpathResult result = s->generate(feature, tool, params);
        if (!result.ok) {
            onErrorOccurred(result.errorMsg.isEmpty() ? tr("Circle milling failed.") : result.errorMsg);
            return;
        }

        PostProcessorBase *pp = PostProcessorRegistry::instance().get(
            Settings::instance().postProcessorId());
        if (!pp) pp = PostProcessorRegistry::instance().get(QStringLiteral("siemens"));
        PostProcessorOptions opts;
        opts.programNumber = QStringLiteral("O0001");
        opts.addComments = true;
        opts.workOffset = currentWorkOffset();
        const QString circleGCode = pp ? pp->wrapGCode(result.gcode.split('\n'), opts) : result.gcode;
        m_gcodeEditor->setGCode(circleGCode);
        m_simCtrl->loadGCode(circleGCode);
        m_bottomBar->showProgress(true);
        m_bottomBar->setStatus(tr("Generated circle milling G-code."));

        m_operationPanel->addContourOperation(feature, QStringLiteral("mill_circle"), params, tool.id);
    });

    connect(m_strategyPanel, &StrategyPanel::millingRequested,
            this, [this](const ContourFeature &feature, const QString &strategyId, const StrategyParams &params, const ToolEntry &tool) {
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
        const StrategyParams generationParams = isSlotMillingStrategy(resolvedStrategyId)
                                                    ? slotMachiningParamsWithoutGeometry(params)
                                                    : params;
        if (isSlotMillingStrategy(resolvedStrategyId)) {
            qDebug().noquote()
                << QStringLiteral("[slot-debug] strategy-selected id=%1 requestedId=%2 subtype=%3 openSide=%4")
                      .arg(resolvedStrategyId)
                      .arg(strategyId)
                      .arg(requestedFeature.subType)
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
                const QVector<int> checkedIndices = m_featurePanel->checkedFeatureIndices();
                for (int index : checkedIndices) {
                    if (index >= 0 && index < features.size() && isSlotFeature(features[index])) {
                        contourBatch.append(applySlotOverridesFromParams(
                            toContourFeature(features[index], &project->mesh()),
                            features[index],
                            params));
                    }
                }
            }
        }
        if (contourBatch.isEmpty()) {
            contourBatch.append(requestedFeature);
        }
        if (isSlotMillingStrategy(resolvedStrategyId)) {
            for (int batchIndex = 0; batchIndex < contourBatch.size(); ++batchIndex) {
                logSlotContourFeature(batchIndex == 0 ? "batch-feature-0" : "batch-feature-n",
                                      contourBatch[batchIndex],
                                      generationParams);
            }
        }

        QStringList blocks;
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
            const ToolpathResult result = s->generate(oneFeature, tool, generationParams);
            if (!result.ok || result.gcode.isEmpty()) {
                onErrorOccurred(result.errorMsg.isEmpty() ? tr("Milling failed.") : result.errorMsg);
                return;
            }
            blocks << result.gcode;
        }

        PostProcessorBase *pp = PostProcessorRegistry::instance().get(
            Settings::instance().postProcessorId());
        if (!pp) pp = PostProcessorRegistry::instance().get(QStringLiteral("siemens"));
        PostProcessorOptions opts;
        opts.programNumber = QStringLiteral("O0001");
        opts.addComments = true;
        opts.workOffset = currentWorkOffset();
        const QString mergedGCode = blocks.join(QLatin1Char('\n'));
        const QString millingGCode = pp ? pp->wrapGCode(mergedGCode.split('\n'), opts) : mergedGCode;
        m_gcodeEditor->setGCode(millingGCode);
        m_simCtrl->loadGCode(millingGCode);
        m_bottomBar->showProgress(true);
        m_bottomBar->setStatus(tr("Generated milling G-code."));

        for (const ContourFeature &oneFeature : contourBatch) {
            m_operationPanel->addContourOperation(oneFeature, resolvedStrategyId, params, tool.id);
        }
    });

    connect(m_operationPanel, &OperationListPanel::generateAllRequested,
            this, [this](const QList<MachiningOperation> &operations) {
        QStringList blocks;
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
                const QString slotError = slotSetupRestrictionMessage(op.contourFeature,
                                                                      op.strategyId,
                                                                      ToolLibrary::instance().tool(op.toolId),
                                                                      op.params,
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
        auto holeKey = [](const HoleFeature &hole) {
            return QStringLiteral("%1|%2|%3")
                .arg(hole.center.x(), 0, 'f', 3)
                .arg(hole.center.y(), 0, 'f', 3)
                .arg(hole.center.z(), 0, 'f', 3);
        };
        QSet<QString> holesWithDeepCycle;
        QSet<QString> holesWithCircularMill;
        for (const MachiningOperation &scanOp : operations) {
            if (scanOp.opType == OperationType::Hole) {
                if (scanOp.strategyId == QStringLiteral("hole_deephole")) {
                    holesWithDeepCycle.insert(holeKey(scanOp.holeFeature));
                } else if (scanOp.strategyId == QStringLiteral("hole_circular_mill")) {
                    holesWithCircularMill.insert(holeKey(scanOp.holeFeature));
                }
            }
        }

        auto operationSummary = [](const MachiningOperation &oneOp) {
            const QString strategyName = oneOp.strategyId;
            if (oneOp.opType == OperationType::Hole) {
                return QStringLiteral("%1 | D%2 Z-%3 | T%4")
                    .arg(strategyName)
                    .arg(oneOp.holeFeature.radius * 2.0, 0, 'f', 3)
                    .arg(oneOp.holeFeature.depth, 0, 'f', 3)
                    .arg(oneOp.toolId);
            }
            return QStringLiteral("%1 | Z-%2 | T%3")
                .arg(strategyName)
                .arg(oneOp.contourFeature.depth, 0, 'f', 3)
                .arg(oneOp.toolId);
        };

        auto appendOperationBlock = [&](int opNumber,
                                        const MachiningOperation &oneOp,
                                        const QString &gcode) {
            QStringList chunk;
            chunk << QStringLiteral("; ---- Operation %1 ----").arg(opNumber);
            chunk << QStringLiteral("; %1").arg(operationSummary(oneOp));
            chunk << gcode.trimmed();
            blocks << chunk.join(QLatin1Char('\n'));
        };

        QStringList generationErrors;
        QSet<QString> peckedHoles;
        // Batch consecutive hole operations sharing the same strategy+tool+params.
        int i = 0;
        while (i < operations.size()) {
            const MachiningOperation &op = operations[i];
            if (op.opType == OperationType::Hole &&
                op.strategyId == QStringLiteral("hole_peck") &&
                holesWithDeepCycle.contains(holeKey(op.holeFeature))) {
                ++i;
                continue;
            }
            if (op.opType == OperationType::Hole &&
                op.strategyId == QStringLiteral("hole_peck") &&
                holesWithCircularMill.contains(holeKey(op.holeFeature)) &&
                peckedHoles.contains(holeKey(op.holeFeature))) {
                ++i;
                continue;
            }
            StrategyBase *s = StrategyFactory::instance().strategy(op.strategyId).get();
            if (!s) { ++i; continue; }
            ToolEntry tool = ToolLibrary::instance().tool(op.toolId);
            if (op.opType == OperationType::Hole) {
                // Collect consecutive hole ops with same strategy/tool
                QVector<HoleFeature> batch;
                int j = i;
                while (j < operations.size()
                       && operations[j].opType == OperationType::Hole
                       && operations[j].strategyId == op.strategyId
                       && operations[j].toolId == op.toolId
                       && operations[j].params.values == op.params.values) {
                    batch.append(operations[j].holeFeature);
                    ++j;
                }
                const ToolpathResult result = s->generate(batch, tool, op.params);
                if (result.ok && !result.gcode.isEmpty()) {
                    appendOperationBlock(i + 1, op, result.gcode);
                } else if (!result.errorMsg.isEmpty()) {
                    generationErrors << result.errorMsg;
                }
                if (op.strategyId == QStringLiteral("hole_peck")) {
                    for (const HoleFeature &hf : batch) {
                        peckedHoles.insert(holeKey(hf));
                    }
                }
                i = j;
            } else {
                const ToolpathResult result = s->generate(op.contourFeature, tool, op.params);
                if (result.ok && !result.gcode.isEmpty()) {
                    appendOperationBlock(i + 1, op, result.gcode);
                } else if (!result.errorMsg.isEmpty()) {
                    generationErrors << result.errorMsg;
                }
                ++i;
            }
        }

        if (blocks.isEmpty()) {
            onErrorOccurred(tr("No G-code blocks were generated."));
            return;
        }

        PostProcessorOptions opts;
        opts.programNumber = QStringLiteral("O0001");
        opts.addComments = true;
        opts.workOffset = currentWorkOffset();
        const QString allGCode = pp->wrapGCode(blocks.join('\n').split('\n'), opts);
        m_gcodeEditor->setGCode(allGCode);
        m_simCtrl->loadGCode(allGCode);
        jumpToGeneratedOperation(m_operationPanel->currentOperationNumber());
        m_bottomBar->showProgress(true);
        m_bottomBar->setStatus(tr("Generated batch G-code."));
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
        AppController::instance().loadProject(path);
    }
}

void MainWindow::onExportGCode()
{
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
    CncSendDialog dlg(m_gcodeEditor->toPlainText(), this);
    dlg.exec();
}

void MainWindow::onResetCamera()
{
    m_viewport->resetCamera();
}

void MainWindow::onSimPlay()
{
    const QString gcode = m_gcodeEditor->toPlainText();
    if (m_simCtrl->toolPath().isEmpty() && !gcode.isEmpty()) {
        m_simCtrl->loadGCode(gcode);
        m_bottomBar->showProgress(true);
    }
    m_simCtrl->play();
    m_bottomBar->setStatus(tr("Simulation playing."));
}

void MainWindow::onSimPause()
{
    m_simCtrl->pause();
    m_bottomBar->setStatus(tr("Simulation paused."));
}

void MainWindow::onSimStop()
{
    m_simCtrl->stop();
    m_viewport->clearToolPath();
    m_bottomBar->showProgress(false);
    m_bottomBar->setStatus(tr("Simulation stopped."));
}

void MainWindow::onSetFrontFace()
{
    m_settingFrontFace = !m_settingFrontFace;
    m_actSetFrontFace->setChecked(m_settingFrontFace);
    if (m_settingFrontFace) {
        m_bottomBar->setStatus(tr("Setting front face..."));
    } else {
        m_bottomBar->setStatus(tr("Front face selection canceled."));
    }
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
    m_simCtrl->setMesh(project->mesh());
    m_featurePanel->setFeatures(project->features());
    m_bottomBar->setStatus(tr("Imported STEP: %1").arg(filePath));
}

void MainWindow::jumpToGeneratedOperation(int operationNumber)
{
    if (operationNumber <= 0 || !m_gcodeEditor) {
        return;
    }

    const QString marker = QStringLiteral("Operation %1").arg(operationNumber);
    const QString gcode = m_gcodeEditor->toPlainText();
    if (gcode.isEmpty()) {
        return;
    }

    const QStringList lines = gcode.split(QLatin1Char('\n'));
    for (int line = 0; line < lines.size(); ++line) {
        if (lines.at(line).contains(marker)) {
            if (m_simCtrl && !m_simCtrl->toolPath().isEmpty()) {
                m_simCtrl->seekToGCodeLine(line);
            }
            m_gcodeEditor->setCurrentExecutionLine(line);
            return;
        }
    }
}

void MainWindow::onGCodeReady(const QString &gcode)
{
    m_gcodeEditor->setGCode(gcode);
    m_simCtrl->loadGCode(gcode);
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

    if (m_actImportStep) m_actImportStep->setText(zh ? QStringLiteral("导入 STEP...")
                                                     : QStringLiteral("Import STEP..."));
    if (m_actOpenProject) m_actOpenProject->setText(zh ? QStringLiteral("打开项目...")
                                                       : QStringLiteral("Open Project..."));
    if (m_actSaveProject) m_actSaveProject->setText(zh ? QStringLiteral("保存项目")
                                                       : QStringLiteral("Save Project"));
    if (m_actExportGCode) m_actExportGCode->setText(zh ? QStringLiteral("导出 G 代码...")
                                                       : QStringLiteral("Export G-code..."));
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
    if (m_wcsCombo) {
        m_wcsCombo->setToolTip(zh ? QStringLiteral("工件坐标系 G54-G59")
                                  : QStringLiteral("Work coordinate system G54-G59"));
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
}
