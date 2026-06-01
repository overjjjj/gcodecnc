#include "StepImporter.h"

#include "FeatureRecognizer.h"
#include "TopoAnalyzer.h"

#ifdef CNEXT_ENABLE_OCC
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Tool.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>
#endif

#include <QMatrix3x3>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>

QString featureKindName(FeatureKind kind)
{
    switch (kind) {
    case FeatureKind::Hole:        return QStringLiteral("Hole");
    case FeatureKind::Slot:        return QStringLiteral("Slot");
    case FeatureKind::Pocket:      return QStringLiteral("Pocket");
    case FeatureKind::Boss:        return QStringLiteral("Boss");
    case FeatureKind::Chamfer:     return QStringLiteral("Chamfer");
    case FeatureKind::Fillet:      return QStringLiteral("Fillet");
    case FeatureKind::FlatSurface: return QStringLiteral("FlatSurface");
    case FeatureKind::Thread:      return QStringLiteral("Thread");
    }
    return QStringLiteral("Unknown");
}

QString faceRegionName(FaceRegion region)
{
    switch (region) {
    case FaceRegion::Front: return QStringLiteral("front");
    case FaceRegion::Side:  return QStringLiteral("side");
    case FaceRegion::Back:  return QStringLiteral("back");
    case FaceRegion::Unknown:
    default:
        return QStringLiteral("unknown");
    }
}

#ifdef CNEXT_ENABLE_OCC
static QString occFailureText(const Standard_Failure &failure, const QString &fallback)
{
    const char *msg = failure.GetMessageString();
    return (msg && *msg) ? QString::fromLocal8Bit(msg) : fallback;
}

static IFSelect_ReturnStatus readStepWithEncodedPath(STEPControl_Reader &reader, const QString &path)
{
    const QByteArray encoded = QFile::encodeName(QDir::toNativeSeparators(path));
    return reader.ReadFile(encoded.constData());
}

static bool loadStepShape(const QString &filePath, TopoDS_Shape &outShape, QString &outError)
{
    outShape.Nullify();

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        outError = QStringLiteral("STEP 文件不存在：%1").arg(fileInfo.absoluteFilePath());
        return false;
    }

    STEPControl_Reader reader;
    IFSelect_ReturnStatus status = readStepWithEncodedPath(reader, fileInfo.absoluteFilePath());

    QTemporaryDir tempDir;
    if (status != IFSelect_RetDone && tempDir.isValid()) {
        const QString tempCopyPath = tempDir.filePath(QStringLiteral("step_input.step"));
        QFile::remove(tempCopyPath);
        if (QFile::copy(fileInfo.absoluteFilePath(), tempCopyPath)) {
            STEPControl_Reader tempReader;
            const IFSelect_ReturnStatus tempStatus = readStepWithEncodedPath(tempReader, tempCopyPath);
            if (tempStatus == IFSelect_RetDone) {
                reader = tempReader;
                status = tempStatus;
            }
        }
    }

    if (status != IFSelect_RetDone) {
        outError = QStringLiteral("STEP 读取失败，请尝试将文件移动到仅包含英文路径的位置：%1")
                       .arg(fileInfo.absoluteFilePath());
        return false;
    }
    if (reader.TransferRoots() <= 0) {
        outError = QStringLiteral("STEP 几何转换失败。");
        return false;
    }

    outShape = reader.OneShape();
    if (outShape.IsNull()) {
        outError = QStringLiteral("STEP 文件中没有可用几何体。");
        return false;
    }
    return true;
}

static TopoDS_Shape rotatedShape(const TopoDS_Shape &shape, const QQuaternion &rotation)
{
    if (rotation.isIdentity()) {
        return shape;
    }

    const QMatrix3x3 m = rotation.toRotationMatrix();
    gp_Trsf trsf;
    trsf.SetValues(m(0, 0), m(0, 1), m(0, 2), 0.0,
                   m(1, 0), m(1, 1), m(1, 2), 0.0,
                   m(2, 0), m(2, 1), m(2, 2), 0.0);
    return BRepBuilderAPI_Transform(shape, trsf, true).Shape();
}
#endif

bool StepImporter::load(const QString &filePath)
{
    return loadWithRotation(filePath, QQuaternion());
}

bool StepImporter::loadWithRotation(const QString &filePath, const QQuaternion &rotation)
{
    m_mesh = MeshData{};
    m_features.clear();
    m_lastError.clear();
    m_meshZShift = 0.0;

#ifdef CNEXT_ENABLE_OCC
    try {
        TopoDS_Shape shape;
        if (!loadStepShape(filePath, shape, m_lastError)) {
            return false;
        }
        shape = rotatedShape(shape, rotation);

        buildMesh(shape);
        if (m_mesh.isEmpty()) {
            m_lastError = QStringLiteral("STEP 三角化后未生成网格。");
            return false;
        }

        recognizeFeatures(shape);
        return true;
    } catch (const Standard_Failure &e) {
        m_lastError = QStringLiteral("OpenCascade 异常：%1")
                          .arg(occFailureText(e, QStringLiteral("未知错误")));
        return false;
    } catch (...) {
        m_lastError = QStringLiteral("导入 STEP 时发生未知异常。");
        return false;
    }
#else
    Q_UNUSED(rotation)
    Q_UNUSED(filePath)
    m_lastError = QStringLiteral(
        "当前程序未启用 OpenCascade（CNEXT_ENABLE_OCC）。"
        "请使用 64 位 Qt/MinGW kit 重新构建，或安装与当前 32 位 Qt 匹配的 32 位 OpenCascade。");
    return false;
#endif
}

#ifdef CNEXT_ENABLE_OCC
void StepImporter::buildMesh(const TopoDS_Shape &shape)
{
    BRepMesh_IncrementalMesh mesher(shape, 0.5, false, 0.5, true);
    mesher.Perform();

    QVector3D bbMin(1e30f, 1e30f, 1e30f);
    QVector3D bbMax(-1e30f, -1e30f, -1e30f);

    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(shape, TopAbs_FACE, faceMap);

    for (int faceIndex = 1; faceIndex <= faceMap.Extent(); ++faceIndex) {
        const TopoDS_Face face = TopoDS::Face(faceMap(faceIndex));
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) {
            continue;
        }

        const bool reversed = (face.Orientation() == TopAbs_REVERSED);
        const gp_Trsf trsf = loc.Transformation();

        for (Standard_Integer i = 1; i <= tri->NbTriangles(); ++i) {
            Standard_Integer n1 = 0;
            Standard_Integer n2 = 0;
            Standard_Integer n3 = 0;
            tri->Triangle(i).Get(n1, n2, n3);
            if (reversed) {
                std::swap(n2, n3);
            }

            auto toV3 = [&](Standard_Integer idx) -> QVector3D {
                const gp_Pnt p = tri->Node(idx).Transformed(trsf);
                return QVector3D(float(p.X()), float(p.Y()), float(p.Z()));
            };

            Triangle t;
            t.v0 = toV3(n1);
            t.v1 = toV3(n2);
            t.v2 = toV3(n3);
            t.normal = QVector3D::crossProduct(t.v1 - t.v0, t.v2 - t.v0).normalized();
            t.faceIndex = faceIndex;
            m_mesh.triangles.append(t);

            for (const QVector3D &v : {t.v0, t.v1, t.v2}) {
                bbMin = QVector3D(qMin(bbMin.x(), v.x()), qMin(bbMin.y(), v.y()), qMin(bbMin.z(), v.z()));
                bbMax = QVector3D(qMax(bbMax.x(), v.x()), qMax(bbMax.y(), v.y()), qMax(bbMax.z(), v.z()));
            }
        }
    }

    if (m_mesh.triangles.isEmpty()) {
        m_mesh.bbMin = {};
        m_mesh.bbMax = {};
        m_meshZShift = 0.0;
        return;
    }

    const float zShift = -bbMax.z();
    m_meshZShift = zShift;
    for (Triangle &t : m_mesh.triangles) {
        t.v0.setZ(t.v0.z() + zShift);
        t.v1.setZ(t.v1.z() + zShift);
        t.v2.setZ(t.v2.z() + zShift);
    }

    m_mesh.bbMin = QVector3D(bbMin.x(), bbMin.y(), bbMin.z() + zShift);
    m_mesh.bbMax = QVector3D(bbMax.x(), bbMax.y(), 0.0f);
}

void StepImporter::recognizeFeatures(const TopoDS_Shape &shape)
{
    m_topoGraph = TopoAnalyzer().analyze(shape);
    m_features = FeatureRecognizer().recognize(m_topoGraph);
    if (std::abs(m_meshZShift) > 1.0e-9) {
        for (MachiningFeature &feature : m_features) {
            feature.center.setZ(feature.center.z() + float(m_meshZShift));
        }
    }
}
#endif
