#include "StepImporter.h"

#include "FeatureRecognizer.h"
#include "TopoAnalyzer.h"

#ifdef CNEXT_ENABLE_OCC
#include <BRepAdaptor_Curve.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
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
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <gp_Trsf.hxx>
#endif

#include <QMatrix3x3>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

// 函数说明：featureKindName负责当前模块的核心处理。
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

// 函数说明：faceRegionName负责当前模块的核心处理。
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
// 函数说明：occFailureText负责当前模块的核心处理。
static QString occFailureText(const Standard_Failure &failure, const QString &fallback)
{
    const char *msg = failure.GetMessageString();
    return (msg && *msg) ? QString::fromLocal8Bit(msg) : fallback;
}

// 函数说明：readStepWithEncodedPath负责当前模块的核心处理。
static IFSelect_ReturnStatus readStepWithEncodedPath(STEPControl_Reader &reader, const QString &path)
{
    const QByteArray encoded = QFile::encodeName(QDir::toNativeSeparators(path));
    return reader.ReadFile(encoded.constData());
}

// 函数说明：loadStepShape负责当前模块的核心处理。
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

// 函数说明：rotatedShape负责当前模块的核心处理。
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

// 函数说明：load负责当前模块的核心处理。
bool StepImporter::load(const QString &filePath)
{
    return loadWithRotation(filePath, QQuaternion());
}

// 函数说明：loadWithRotation负责当前模块的核心处理。
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
// 函数说明：buildMesh负责当前模块的核心处理。
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

    TopTools_IndexedMapOfShape edgeMap;
    TopTools_IndexedMapOfShape vertexMap;
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertexMap);
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaceMap;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeFaceMap);

    m_mesh.pickVertices.reserve(vertexMap.Extent());
    for (int vertexIndex = 1; vertexIndex <= vertexMap.Extent(); ++vertexIndex) {
        const TopoDS_Vertex vertex = TopoDS::Vertex(vertexMap(vertexIndex));
        gp_Pnt point = BRep_Tool::Pnt(vertex);
        point.Transform(vertex.Location().Transformation());
        MeshPickVertex pickVertex;
        pickVertex.id = QStringLiteral("vertex:%1").arg(vertexIndex);
        pickVertex.point = QVector3D(float(point.X()), float(point.Y()), float(point.Z()));
        m_mesh.pickVertices.append(pickVertex);
    }

    m_mesh.pickEdges.reserve(edgeMap.Extent());
    for (int edgeIndex = 1; edgeIndex <= edgeMap.Extent(); ++edgeIndex) {
        const TopoDS_Edge edge = TopoDS::Edge(edgeMap(edgeIndex));
        MeshPickEdge pickEdge;
        pickEdge.id = QStringLiteral("edge:%1").arg(edgeIndex);
        pickEdge.degenerated = BRep_Tool::Degenerated(edge);

        TopoDS_Vertex firstVertex;
        TopoDS_Vertex lastVertex;
        TopExp::Vertices(edge, firstVertex, lastVertex, true);
        const int firstVertexIndex = vertexMap.FindIndex(firstVertex);
        const int lastVertexIndex = vertexMap.FindIndex(lastVertex);
        if (firstVertexIndex > 0) {
            pickEdge.firstVertexId = QStringLiteral("vertex:%1").arg(firstVertexIndex);
            m_mesh.pickVertices[firstVertexIndex - 1].incidentEdgeIds.append(pickEdge.id);
        }
        if (lastVertexIndex > 0) {
            pickEdge.lastVertexId = QStringLiteral("vertex:%1").arg(lastVertexIndex);
            if (lastVertexIndex != firstVertexIndex) {
                m_mesh.pickVertices[lastVertexIndex - 1].incidentEdgeIds.append(pickEdge.id);
            }
        }
        pickEdge.closed = firstVertexIndex > 0 && firstVertexIndex == lastVertexIndex;

        if (edgeFaceMap.Contains(edge)) {
            const TopTools_ListOfShape &faces = edgeFaceMap.FindFromKey(edge);
            for (TopTools_ListIteratorOfListOfShape it(faces); it.More(); it.Next()) {
                const int adjacentFace = faceMap.FindIndex(it.Value());
                if (adjacentFace > 0 && !pickEdge.adjacentFaceIndices.contains(adjacentFace)) {
                    pickEdge.adjacentFaceIndices.append(adjacentFace);
                }
            }
        }

        if (!pickEdge.degenerated) {
            BRepAdaptor_Curve curve(edge);
            const double firstParameter = curve.FirstParameter();
            const double lastParameter = curve.LastParameter();
            int sampleCount = 25;
            if (curve.GetType() == GeomAbs_Line) {
                sampleCount = 2;
            } else if (curve.GetType() == GeomAbs_Circle ||
                       curve.GetType() == GeomAbs_Ellipse) {
                sampleCount = 33;
            }
            if (std::isfinite(firstParameter) && std::isfinite(lastParameter)) {
                pickEdge.points.reserve(sampleCount);
                pickEdge.parameters.reserve(sampleCount);
                for (int sample = 0; sample < sampleCount; ++sample) {
                    const double ratio = sampleCount == 1 ? 0.0
                        : double(sample) / double(sampleCount - 1);
                    const double parameter = firstParameter
                        + (lastParameter - firstParameter) * ratio;
                    const gp_Pnt point = curve.Value(parameter);
                    pickEdge.points.append(QVector3D(float(point.X()), float(point.Y()), float(point.Z())));
                    pickEdge.parameters.append(parameter);
                }
                // Match the oriented vertex IDs returned by TopExp::Vertices above.
                if (edge.Orientation() == TopAbs_REVERSED) {
                    std::reverse(pickEdge.points.begin(), pickEdge.points.end());
                    std::reverse(pickEdge.parameters.begin(), pickEdge.parameters.end());
                }
            }
        }
        m_mesh.pickEdges.append(pickEdge);
    }

    for (int faceIndex = 1; faceIndex <= faceMap.Extent(); ++faceIndex) {
        const TopoDS_Face face = TopoDS::Face(faceMap(faceIndex));
        const TopoDS_Wire outerWire = BRepTools::OuterWire(face);
        for (TopExp_Explorer wireExplorer(face, TopAbs_WIRE);
             wireExplorer.More(); wireExplorer.Next()) {
            const TopoDS_Wire wire = TopoDS::Wire(wireExplorer.Current());
            MeshBoundaryLoop loop;
            loop.faceId = QStringLiteral("face:%1").arg(faceIndex);
            loop.faceIndex = faceIndex;
            loop.outer = !outerWire.IsNull() && wire.IsSame(outerWire);
            for (BRepTools_WireExplorer edgeExplorer(wire, face);
                 edgeExplorer.More(); edgeExplorer.Next()) {
                const TopoDS_Edge orientedEdge = edgeExplorer.Current();
                const int edgeIndex = edgeMap.FindIndex(orientedEdge);
                if (edgeIndex <= 0) {
                    continue;
                }
                loop.orderedEdgeIds.append(QStringLiteral("edge:%1").arg(edgeIndex));
                const TopoDS_Vertex orientedFirst = TopExp::FirstVertex(orientedEdge, true);
                loop.orderedForward.append(vertexMap.FindIndex(orientedFirst) ==
                                           vertexMap.FindIndex(TopoDS::Vertex(
                                               TopExp::FirstVertex(TopoDS::Edge(edgeMap(edgeIndex)), true))));
            }
            if (!loop.orderedEdgeIds.isEmpty()) {
                m_mesh.boundaryLoops.append(loop);
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
    gp_Trsf normalized;
    normalized.SetTranslation(gp_Vec(0,0,zShift));
    m_mesh.sourceShape = shape.Moved(TopLoc_Location(normalized));
    for (Triangle &t : m_mesh.triangles) {
        t.v0.setZ(t.v0.z() + zShift);
        t.v1.setZ(t.v1.z() + zShift);
        t.v2.setZ(t.v2.z() + zShift);
    }
    for (MeshPickVertex &vertex : m_mesh.pickVertices) {
        vertex.point.setZ(vertex.point.z() + zShift);
    }
    for (MeshPickEdge &edge : m_mesh.pickEdges) {
        for (QVector3D &point : edge.points) {
            point.setZ(point.z() + zShift);
        }
    }

    m_mesh.bbMin = QVector3D(bbMin.x(), bbMin.y(), bbMin.z() + zShift);
    m_mesh.bbMax = QVector3D(bbMax.x(), bbMax.y(), 0.0f);
}

// 函数说明：recognizeFeatures负责当前模块的核心处理。
void StepImporter::recognizeFeatures(const TopoDS_Shape &shape)
{
    m_topoGraph = TopoAnalyzer().analyze(shape);
    m_features = FeatureRecognizer().recognize(m_topoGraph);
    if (std::abs(m_meshZShift) > 1.0e-9) {
        for (MachiningFeature &feature : m_features) {
            feature.center.setZ(feature.center.z() + m_meshZShift);
            if (feature.kind==FeatureKind::FlatSurface && feature.planarBoundaryVerified)
                for(auto &point:feature.boundaryPoints) point.setZ(point.z()+float(m_meshZShift));
        }
    }
}
#endif
