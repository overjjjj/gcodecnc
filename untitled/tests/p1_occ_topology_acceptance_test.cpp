#include "../src/import/StepImporter.h"
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"

#define private public
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#undef private

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <STEPControl_Writer.hxx>

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>

#include <cstdlib>
#include <iostream>

static void settle()
{
    QEventLoop loop;
    QTimer::singleShot(150, &loop, &QEventLoop::quit);
    loop.exec();
}

static QPoint projectPoint(ViewportWidget &viewport, const QVector3D &point)
{
    const QVector3D ndc = (viewport.m_proj * viewport.currentViewMatrix()).map(point);
    return QPoint(qRound((ndc.x() + 1.0f) * viewport.width() * 0.5f),
                  qRound((1.0f - ndc.y()) * viewport.height() * 0.5f));
}

static void clickViewport(ViewportWidget &viewport, const QPoint &screen)
{
    QMouseEvent press(QEvent::MouseButtonPress, screen, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, screen, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &release);
}

static const MeshPickEdge *edgeForId(const MeshData &mesh, const QString &id)
{
    for (const MeshPickEdge &edge : mesh.pickEdges)
        if (edge.id == id) return &edge;
    return nullptr;
}

static QVector<QVector3D> loopPoints(const MeshData &mesh, const MeshBoundaryLoop &loop)
{
    QVector<QVector3D> points;
    for (int index = 0; index < loop.orderedEdgeIds.size(); ++index) {
        const MeshPickEdge *edge = edgeForId(mesh, loop.orderedEdgeIds.at(index));
        if (!edge) return {};
        const bool forward = loop.orderedForward.value(index, true);
        if (forward) {
            for (const QVector3D &point : edge->points)
                if (points.isEmpty() || points.last() != point) points.append(point);
        } else {
            for (auto it = edge->points.crbegin(); it != edge->points.crend(); ++it)
                if (points.isEmpty() || points.last() != *it) points.append(*it);
        }
    }
    if (points.size() > 1 && points.first() == points.last()) points.removeLast();
    return points;
}

static SelectionCandidate candidateForFace(const MeshData &mesh, int faceIndex,
                                           const QString &geometryId)
{
    SelectionCandidate candidate;
    candidate.geometryId = geometryId;
    candidate.label = geometryId;
    candidate.faceIds = QStringList{QStringLiteral("face:%1").arg(faceIndex)};
    candidate.surfaceNormal = QVector3D(0, 0, 1);
    QSet<QString> edgeIds;
    QSet<QString> vertexIds;
    for (const MeshBoundaryLoop &loop : mesh.boundaryLoops) {
        if (loop.faceIndex != faceIndex) continue;
        candidate.boundaryLoops.append(loop);
        if (loop.outer) candidate.points = loopPoints(mesh, loop);
        else ++candidate.islandCount;
        for (const QString &edgeId : loop.orderedEdgeIds) edgeIds.insert(edgeId);
    }
    for (const MeshPickEdge &edge : mesh.pickEdges) {
        if (!edgeIds.contains(edge.id)) continue;
        candidate.topologyEdges.append(edge);
        vertexIds.insert(edge.firstVertexId);
        vertexIds.insert(edge.lastVertexId);
    }
    for (const MeshPickVertex &vertex : mesh.pickVertices)
        if (vertexIds.contains(vertex.id)) candidate.topologyVertices.append(vertex);
    for (const Triangle &triangle : mesh.triangles)
        if (triangle.faceIndex == faceIndex) { candidate.surfaceNormal = triangle.normal; break; }
    candidate.closed = true;
    candidate.outerLoopPointCount = candidate.points.size();
    return candidate;
}

static void setMode(SelectionChainDialog &dialog, ChainSelectionMode mode)
{
    auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("selectionMode"));
    combo->setCurrentIndex(static_cast<int>(mode));
    QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, static_cast<int>(mode)));
}

static QByteArray writeStep(const TopoDS_Shape &shape, const QString &path)
{
    STEPControl_Writer writer;
    writer.Transfer(shape, STEPControl_AsIs);
    if (writer.Write(path.toLocal8Bit().constData()) != IFSelect_RetDone) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}

static TopoDS_Shape curvedPrism()
{
    const gp_Pnt left(-10, 0, 0), right(10, 0, 0), crown(0, 10, 0);
    const TopoDS_Edge line = BRepBuilderAPI_MakeEdge(left, right);
    const Handle(Geom_TrimmedCurve) arcCurve =
        GC_MakeArcOfCircle(right, crown, left).Value();
    const TopoDS_Edge arc = BRepBuilderAPI_MakeEdge(arcCurve);
    BRepBuilderAPI_MakeWire wire;
    wire.Add(line);
    wire.Add(arc);
    return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()).Face(), gp_Vec(0, 0, 8));
}

static bool visibleEdgePoint(ViewportWidget &viewport, const MeshPickEdge &edge,
                             bool nearFirst, QPoint *screen)
{
    if (edge.points.size() < 2) return false;
    const int index = nearFirst ? qMin(1, edge.points.size() - 1)
                                : qMax(0, edge.points.size() - 2);
    const QPoint candidate = projectPoint(viewport, edge.points.at(index));
    QString id;
    QVector3D point;
    double parameter = 0.0;
    if (!viewport.pickEdgeAt(candidate, &id, &point, &parameter) || id != edge.id) return false;
    *screen = candidate;
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc, argv);
    if (argc < 2) return 1;
    const QString output = QString::fromLocal8Bit(argv[1]);
    QDir().mkpath(output + QStringLiteral("/fixtures"));

    const TopoDS_Shape holed = BRepAlgoAPI_Cut(
        BRepPrimAPI_MakeBox(40, 30, 10).Shape(),
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20, 15, 0), gp_Dir(0, 0, 1)), 5, 10).Shape());
    const QString holedPath = output + QStringLiteral("/fixtures/holed-box.step");
    const QString curvedPath = output + QStringLiteral("/fixtures/curved-prism.step");
    const QString boxPath = output + QStringLiteral("/fixtures/shared-edge-box.step");
    const QByteArray holedHash = writeStep(holed, holedPath);
    const QByteArray curvedHash = writeStep(curvedPrism(), curvedPath);
    const QByteArray boxHash = writeStep(BRepPrimAPI_MakeBox(40, 30, 10).Shape(), boxPath);
    if (holedHash.isEmpty() || curvedHash.isEmpty() || boxHash.isEmpty()) return 2;
    std::cout << "FIXTURE holed box=40x30x10 hole=(20,15) r=5 sha256="
              << holedHash.constData() << '\n';
    std::cout << "FIXTURE curved D-prism chord=20 crown=10 height=8 sha256="
              << curvedHash.constData() << '\n';
    std::cout << "FIXTURE shared-edge box=40x30x10 sha256="
              << boxHash.constData() << '\n';

    StepImporter holedImporter;
    if (!holedImporter.load(holedPath)) return 3;
    const MeshData holedMesh = holedImporter.mesh();
    int holedFace = -1;
    for (const MeshBoundaryLoop &loop : holedMesh.boundaryLoops) {
        if (!loop.outer) continue;
        int loopCount = 0;
        bool hasInner = false;
        for (const MeshBoundaryLoop &other : holedMesh.boundaryLoops) {
            if (other.faceIndex != loop.faceIndex) continue;
            ++loopCount;
            hasInner = hasInner || !other.outer;
        }
        if (loopCount >= 2 && hasInner) { holedFace = loop.faceIndex; break; }
    }
    if (holedFace < 0) { std::cerr << "holed OCC face has no outer/inner loops\n"; return 4; }
    const SelectionCandidate holedCandidate = candidateForFace(holedMesh, holedFace, "occ:holed-face");
    ViewportWidget holedViewport;
    holedViewport.resize(900, 700);
    holedViewport.setMesh(holedMesh);
    holedViewport.setStandardView(ViewportWidget::StandardView::Top);
    holedViewport.fitAll();
    holedViewport.show();
    settle();
    SelectionChainDialog faceDialog;
    faceDialog.setContext(QString::fromLatin1(holedHash), "setup:occ", "G54");
    faceDialog.setCandidates({holedCandidate});
    setMode(faceDialog, ChainSelectionMode::Face);
    faceDialog.show();
    QObject::connect(&holedViewport, &ViewportWidget::topologyPicked,
                     [&](const QString &id, int kind, const QVector3D &point, double parameter) {
        faceDialog.selectTopology(id, kind, point, parameter);
    });
    holedViewport.setTopologyPickMode(static_cast<int>(ViewportWidget::TopologyPickMode::Face));
    QVector3D facePoint;
    for (const Triangle &triangle : holedMesh.triangles) {
        if (triangle.faceIndex == holedFace) {
            facePoint = (triangle.v0 + triangle.v1 + triangle.v2) / 3.0f;
            break;
        }
    }
    clickViewport(holedViewport, projectPoint(holedViewport, facePoint));
    settle();
    auto *loopRows = faceDialog.findChild<QListWidget *>(QStringLiteral("selectionEdges"));
    if (!loopRows || loopRows->count() != holedCandidate.boundaryLoops.size()) {
        std::cerr << "real face pick must show outer and inner loop identities separately\n";
        return 5;
    }
    bool visibleOuter = false, visibleInner = false;
    for (int row = 0; row < loopRows->count(); ++row) {
        visibleOuter = visibleOuter || loopRows->item(row)->text().contains(QStringLiteral("外环"));
        visibleInner = visibleInner || loopRows->item(row)->text().contains(QStringLiteral("内环"));
    }
    if (!visibleOuter || !visibleInner) return 6;
    faceDialog.grab().save(output + QStringLiteral("/holed-face-dialog.png"));
    holedViewport.grabFramebuffer().save(output + QStringLiteral("/holed-face-viewport.png"));
    faceDialog.accept();
    const SelectionChain faceSelection = faceDialog.selection();
    int outerCount = 0, innerCount = 0;
    for (const SelectionBoundaryLoop &loop : faceSelection.boundaryLoops) {
        outerCount += loop.outer ? 1 : 0;
        innerCount += loop.outer ? 0 : 1;
        const MeshBoundaryLoop *meshLoop = nullptr;
        for (const MeshBoundaryLoop &candidate : holedMesh.boundaryLoops)
            if (candidate.faceId == loop.faceId && candidate.outer == loop.outer &&
                candidate.orderedEdgeIds == loop.orderedEdgeIds) { meshLoop = &candidate; break; }
        const QVector<QVector3D> points = meshLoop ? loopPoints(holedMesh, *meshLoop)
                                                   : QVector<QVector3D>();
        if (points.size() < 2) return 7;
    }
    if (outerCount != 1 || innerCount < 1) return 8;

    StepImporter curvedImporter;
    if (!curvedImporter.load(curvedPath)) return 9;
    const MeshData curvedMesh = curvedImporter.mesh();
    const MeshPickEdge *curvedEdge = nullptr;
    for (const MeshPickEdge &edge : curvedMesh.pickEdges) {
        if (edge.points.size() < 4 || edge.firstVertexId == edge.lastVertexId) continue;
        const QVector3D chord = edge.points.last() - edge.points.first();
        bool nonlinear = false;
        for (const QVector3D &point : edge.points) {
            if (QVector3D::crossProduct(chord, point - edge.points.first()).length() > 0.1f) {
                nonlinear = true;
                break;
            }
        }
        if (nonlinear) { curvedEdge = &edge; break; }
    }
    if (!curvedEdge) { std::cerr << "curved OCC fixture has no sampled arc edge\n"; return 10; }
    int curvedFace = -1;
    for (const MeshBoundaryLoop &loop : curvedMesh.boundaryLoops) {
        if (loop.orderedEdgeIds.contains(curvedEdge->id)) {
            curvedFace = loop.faceIndex;
            break;
        }
    }
    if (curvedFace < 0) return 10;
    SelectionCandidate curvedCandidate = candidateForFace(curvedMesh, curvedFace, "occ:curved-face");
    ViewportWidget curvedViewport;
    curvedViewport.resize(900, 700);
    curvedViewport.setMesh(curvedMesh);
    curvedViewport.setStandardView(ViewportWidget::StandardView::Top);
    curvedViewport.fitAll();
    curvedViewport.show();
    settle();
    bool firstDirection = false;
    for (int endpoint = 0; endpoint < 2; ++endpoint) {
        SelectionChainDialog edgeDialog;
        edgeDialog.setContext(QString::fromLatin1(curvedHash), "setup:occ", "G54",
                              curvedCandidate.surfaceNormal.normalized());
        edgeDialog.setCandidates({curvedCandidate});
        setMode(edgeDialog, ChainSelectionMode::Edge);
        edgeDialog.show();
        double emittedParameter = 0.0;
        const QMetaObject::Connection connection = QObject::connect(
            &curvedViewport, &ViewportWidget::topologyPicked,
            [&](const QString &id, int kind, const QVector3D &point, double parameter) {
                emittedParameter = parameter;
                edgeDialog.selectTopology(id, kind, point, parameter);
            });
        curvedViewport.setTopologyPickMode(static_cast<int>(ViewportWidget::TopologyPickMode::Edge));
        QPoint screen;
        if (!visibleEdgePoint(curvedViewport, *curvedEdge, endpoint == 0, &screen)) return 11;
        clickViewport(curvedViewport, screen);
        settle();
        auto *curvedContexts = edgeDialog.findChild<QComboBox *>(
            QStringLiteral("selectionTopologyContext"));
        auto *confirmCurvedContext = edgeDialog.findChild<QPushButton *>(
            QStringLiteral("selectionConfirmTopologyContext"));
        if (curvedContexts && confirmCurvedContext && curvedContexts->isVisible()) {
            curvedContexts->setCurrentIndex(0);
            QMetaObject::invokeMethod(curvedContexts, "activated", Q_ARG(int, 0));
            confirmCurvedContext->click();
            settle();
        }
        edgeDialog.accept();
        QObject::disconnect(connection);
        const SelectionChain selection = edgeDialog.selection();
        if (selection.orderedTopologyRefs.size() != 1 ||
            selection.orderedTopologyRefs.first().id != curvedEdge->id) {
            std::cerr << "curved endpoint click did not commit the expected edge; endpoint="
                      << endpoint << " refs=" << selection.orderedTopologyRefs.size();
            if (!selection.orderedTopologyRefs.isEmpty())
                std::cerr << " id=" << selection.orderedTopologyRefs.first().id.toStdString();
            std::cerr << " expected=" << curvedEdge->id.toStdString()
                      << " parameter=" << emittedParameter << '\n';
            for (QLabel *label : edgeDialog.findChildren<QLabel *>())
                if (!label->text().isEmpty())
                    std::cerr << "LABEL " << label->text().toStdString() << '\n';
            return 12;
        }
        const bool forward = selection.orderedTopologyRefs.first().forward;
        if (endpoint == 0) firstDirection = forward;
        else if (forward == firstDirection) {
            std::cerr << "opposite curved endpoints produced the same direction; parameter="
                      << emittedParameter << " first=" << curvedEdge->parameters.value(0)
                      << " last=" << curvedEdge->parameters.value(curvedEdge->parameters.size() - 1)
                      << " screen=" << screen.x() << ',' << screen.y() << '\n';
            return 12;
        }
        const QVector<QVector3D> samples = edgeDialog.selectedPoints();
        if (samples.size() != curvedEdge->points.size() ||
            (samples.first() - (forward ? curvedEdge->points.first()
                                        : curvedEdge->points.last())).length() > 1.0e-4f ||
            (samples.last() - (forward ? curvedEdge->points.last()
                                       : curvedEdge->points.first())).length() > 1.0e-4f) return 13;
    }
    curvedViewport.grabFramebuffer().save(output + QStringLiteral("/curved-edge-viewport.png"));

    StepImporter boxImporter;
    if (!boxImporter.load(boxPath)) return 14;
    const MeshData boxMesh = boxImporter.mesh();
    ViewportWidget sharedViewport;
    sharedViewport.resize(900, 700);
    sharedViewport.setMesh(boxMesh);
    sharedViewport.show();
    settle();
    const MeshPickEdge *sharedEdge = nullptr;
    QPoint sharedScreen;
    for (ViewportWidget::StandardView view : {ViewportWidget::StandardView::Isometric,
                                              ViewportWidget::StandardView::Top,
                                              ViewportWidget::StandardView::Front}) {
        sharedViewport.setStandardView(view);
        sharedViewport.fitAll();
        settle();
        for (const MeshPickEdge &edge : boxMesh.pickEdges) {
            if (edge.adjacentFaceIndices.size() < 2) continue;
            if (visibleEdgePoint(sharedViewport, edge, true, &sharedScreen)) {
                sharedEdge = &edge;
                break;
            }
        }
        if (sharedEdge) break;
    }
    if (!sharedEdge) { std::cerr << "no visible imported shared OCC edge\n"; return 15; }
    const int firstFace = sharedEdge->adjacentFaceIndices.at(0);
    const int secondFace = sharedEdge->adjacentFaceIndices.at(1);
    const SelectionCandidate firstSharedCandidate =
        candidateForFace(boxMesh, firstFace, "occ:face-a");
    const SelectionCandidate secondSharedCandidate =
        candidateForFace(boxMesh, secondFace, "occ:face-b");
    SelectionChainDialog sharedDialog;
    sharedDialog.setContext(QString::fromLatin1(boxHash), "setup:occ", "G54",
                            secondSharedCandidate.surfaceNormal.normalized());
    sharedDialog.setCandidates({firstSharedCandidate, secondSharedCandidate});
    setMode(sharedDialog, ChainSelectionMode::Edge);
    sharedDialog.show();
    QObject::connect(&sharedViewport, &ViewportWidget::topologyPicked,
                     [&](const QString &id, int kind, const QVector3D &point, double parameter) {
        sharedDialog.selectTopology(id, kind, point, parameter);
    });
    sharedViewport.setTopologyPickMode(static_cast<int>(ViewportWidget::TopologyPickMode::Edge));
    clickViewport(sharedViewport, sharedScreen);
    settle();
    auto *contexts = sharedDialog.findChild<QComboBox *>(QStringLiteral("selectionTopologyContext"));
    auto *confirm = sharedDialog.findChild<QPushButton *>(QStringLiteral("selectionConfirmTopologyContext"));
    sharedDialog.accept();
    if (sharedDialog.result() == QDialog::Accepted || !contexts || contexts->count() != 2 ||
        !confirm || contexts->isHidden() || confirm->isHidden()) return 16;
    sharedDialog.grab().save(output + QStringLiteral("/shared-edge-pending.png"));
    sharedViewport.grabFramebuffer().save(output + QStringLiteral("/shared-edge-viewport.png"));
    contexts->setCurrentIndex(1);
    QMetaObject::invokeMethod(contexts, "activated", Q_ARG(int, 1));
    const QString chosenContext = contexts->currentText();
    confirm->click();
    sharedDialog.accept();
    const SelectionChain sharedSelection = sharedDialog.selection();
    if (sharedSelection.boundaryLoops.size() != 1 ||
        !chosenContext.contains(sharedSelection.boundaryLoops.first().faceId) ||
        sharedSelection.orderedTopologyRefs.size() != 1 ||
        sharedSelection.orderedTopologyRefs.first().id != sharedEdge->id) return 17;

    std::cout << "PASS real OCC mouse face outer/inner loops, curved edge endpoints/samples, "
                 "and shared-edge explicit context confirmation\n";
    std::cout.flush();
    std::_Exit(0);
}
