#pragma once
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QQuaternion>
#include <QMap>
#include <QPair>

#ifdef CNEXT_ENABLE_OCC
#include <GeomAbs_SurfaceType.hxx>
#include <gp_Ax1.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#endif

struct Triangle {
    QVector3D v0, v1, v2;
    QVector3D normal;
    int       faceIndex = 0;
};

struct MeshData {
    QVector<Triangle> triangles;
    QVector3D         bbMin, bbMax;
    bool              isEmpty() const { return triangles.isEmpty(); }
};

enum class FeatureKind {
    Hole,
    Slot,
    Pocket,
    Boss,
    Chamfer,
    Fillet,
    FlatSurface,
    Thread
};

enum class FaceRegion {
    Unknown,
    Front,
    Side,
    Back
};

QString featureKindName(FeatureKind kind);
QString faceRegionName(FaceRegion region);

struct MachiningFeature {
    FeatureKind kind = FeatureKind::Hole;
    QString   subType;     // through_hole, blind_hole, countersunk_*, tapped_hole, etc.
    double    radius  = 0.0;
    double    depth   = 0.0;
    double    width   = 0.0;
    double    length  = 0.0;
    double    secondaryRadius = 0.0; // countersink/counterbore or top radius
    double    pitch = 0.0;           // thread pitch
    QVector3D center;
    QVector3D axis    = {0, 0, 1};
    double    coneAngle = 0.0;
    double    angle = 0.0;       // slot/feature orientation in XY plane (degrees, 0=X axis)
    FaceRegion region = FaceRegion::Unknown;
    QVector<int> faceIndices;
};

using HoleFeature = MachiningFeature;

struct ContourFeature {
    QString   subType;       // "circle", "closed_contour", "open_contour"
    QVector3D center;        // circle center (or contour origin)
    double    radius  = 0.0; // for "circle" subtype
    double    depth   = 0.0; // total machining depth (positive = downward)
    double    width   = 0.0; // slot/pocket width when applicable
    double    length  = 0.0; // slot/pocket length when applicable
    double    angle   = 0.0; // XY orientation in degrees for slot-like features
    double    openSide = 0.0; // open-slot mouth side along local length axis: +1 or -1
    double    slopeStartLength = 0.0; // ramp/chamfer length at local -U end of a slot
    double    slopeEndLength = 0.0;   // ramp/chamfer length at local +U end of a slot
    double    slopeMinWidth = 0.0;     // side slope/chamfer length at local -V side
    double    slopeMaxWidth = 0.0;     // side slope/chamfer length at local +V side
    QVector3D axis    = {0, 0, 1}; // normal of the contour plane
    QVector<QVector3D> points;     // for arbitrary contour subtypes
};

#ifdef CNEXT_ENABLE_OCC
struct FaceNode {
    int index = 0;
    GeomAbs_SurfaceType surfType = GeomAbs_OtherSurface;
    gp_Ax1 axis;
    double radius = 0.0;
    QVector3D normal;
    FaceRegion region = FaceRegion::Unknown;
};

struct EdgeAttr {
    bool isConcave = false;
    bool isSmooth = false;
};

using AAG = QMap<int, QVector<QPair<int, EdgeAttr>>>;

struct TopoGraph {
    QVector<FaceNode> nodes;
    AAG aag;
    TopTools_IndexedMapOfShape faceMap;
};
#endif

class StepImporter
{
public:
    bool load(const QString &filePath);
    bool loadWithRotation(const QString &filePath, const QQuaternion &rotation);
    const MeshData           &mesh()     const { return m_mesh; }
    const QVector<MachiningFeature> &features() const { return m_features; }
    QString                    lastError() const { return m_lastError; }
#ifdef CNEXT_ENABLE_OCC
    const TopoGraph           &topoGraph() const { return m_topoGraph; }
    TopoGraph                 &topoGraph()       { return m_topoGraph; }
#endif

private:
#ifdef CNEXT_ENABLE_OCC
    void buildMesh(const class TopoDS_Shape &shape);
    void recognizeFeatures(const class TopoDS_Shape &shape);
    TopoGraph          m_topoGraph;
#endif
    MeshData           m_mesh;
    QVector<MachiningFeature> m_features;
    QString            m_lastError;
    double             m_meshZShift = 0.0;
};
