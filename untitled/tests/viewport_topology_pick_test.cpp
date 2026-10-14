#include <QApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cstdlib>
#include <iostream>
#include "../src/import/StepImporter.h"

#define private public
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#undef private

static QPoint projectPoint(ViewportWidget &viewport, const QVector3D &point)
{
    const QVector3D ndc = (viewport.m_proj * viewport.currentViewMatrix()).map(point);
    return QPoint(qRound((ndc.x() + 1.0f) * viewport.width() * 0.5f),
                  qRound((1.0f - ndc.y()) * viewport.height() * 0.5f));
}

static void click(ViewportWidget &viewport, const QPoint &position)
{
    QMouseEvent press(QEvent::MouseButtonPress, position, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, position, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &release);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    auto *viewport = new ViewportWidget;
    viewport->resize(800, 600);

    MeshData mesh;
    mesh.bbMin = QVector3D(-10, -10, 0);
    mesh.bbMax = QVector3D(10, 10, 0);
    Triangle first;
    first.v0 = QVector3D(-10, -10, 0);
    first.v1 = QVector3D(10, -10, 0);
    first.v2 = QVector3D(10, 10, 0);
    first.normal = QVector3D(0, 0, 1);
    first.faceIndex = 1;
    Triangle second = first;
    second.v1 = QVector3D(10, 10, 0);
    second.v2 = QVector3D(-10, 10, 0);
    mesh.triangles = {first, second};

    MeshPickVertex leftVertex;
    leftVertex.id = QStringLiteral("vertex:1");
    leftVertex.point = QVector3D(-8, 0, 0);
    leftVertex.incidentEdgeIds = QStringList{QStringLiteral("edge:1")};
    MeshPickVertex rightVertex;
    rightVertex.id = QStringLiteral("vertex:2");
    rightVertex.point = QVector3D(8, 0, 0);
    rightVertex.incidentEdgeIds = QStringList{QStringLiteral("edge:1")};
    mesh.pickVertices = {leftVertex, rightVertex};

    MeshPickEdge edge;
    edge.id = QStringLiteral("edge:1");
    edge.firstVertexId = leftVertex.id;
    edge.lastVertexId = rightVertex.id;
    edge.adjacentFaceIndices = {1};
    edge.points = {leftVertex.point, rightVertex.point};
    edge.parameters = {2.0, 6.0};
    MeshPickEdge sideEdge = edge;
    sideEdge.id = QStringLiteral("edge:side");
    sideEdge.firstVertexId = QStringLiteral("vertex:3");
    sideEdge.lastVertexId = QStringLiteral("vertex:4");
    sideEdge.points = {QVector3D(-8, 6, 0), QVector3D(8, 6, 0)};
    mesh.pickEdges = {edge, sideEdge};

    viewport->setMesh(mesh);
    viewport->setStandardView(ViewportWidget::StandardView::Top);
    viewport->fitAll();
    viewport->m_view = viewport->currentViewMatrix();

    QString pickedId;
    int pickedKind = -1;
    QVector3D pickedPoint;
    double pickedParameter = -1.0;
    QObject::connect(viewport, &ViewportWidget::topologyPicked,
                     [&](const QString &id, int kind, const QVector3D &point, double parameter) {
        pickedId = id;
        pickedKind = kind;
        pickedPoint = point;
        pickedParameter = parameter;
    });

    QString hoveredId;
    QObject::connect(viewport, &ViewportWidget::topologyHovered,
                     [&](const QString &id, int, const QVector3D &, double) {
        hoveredId = id;
    });

    viewport->setTopologyPickMode(static_cast<int>(ViewportWidget::TopologyPickMode::Edge));
    click(*viewport, projectPoint(*viewport, QVector3D(0, 0, 0)));
    if (pickedId != edge.id || pickedKind != 1 ||
        std::abs(pickedParameter - 4.0) > 0.05 || pickedPoint.length() > 0.05f) {
        std::cerr << "real mouse click must return the stable edge and curve parameter\n";
        std::_Exit(1);
    }

    // A machining-plane filter must reach the hit test itself.  Hiding an
    // overlay while leaving a side-only edge pickable is not sufficient.
    viewport->setTopologyPickFilter(QStringList{edge.id},
                                    QStringList{leftVertex.id, rightVertex.id},
                                    QVector<int>{1});
    pickedId.clear();
    click(*viewport, projectPoint(*viewport, QVector3D(0, -6, 0)));
    if (!pickedId.isEmpty()) {
        std::cerr << "edge-selection mode must not fall back to picking a face center\n";
        std::_Exit(1);
    }
    QMouseEvent hoverSide(QEvent::MouseMove,
                          projectPoint(*viewport, QVector3D(0, 6, 0)),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &hoverSide);
    if (!hoveredId.isEmpty()) {
        std::cerr << "a filtered side-only edge must not receive hover highlight\n";
        std::_Exit(1);
    }
    click(*viewport, projectPoint(*viewport, QVector3D(0, 6, 0)));
    if (!pickedId.isEmpty()) {
        std::cerr << "RED: a side-only edge outside the machining plane remained pickable\n";
        std::_Exit(1);
    }
    viewport->setStandardView(ViewportWidget::StandardView::Isometric);
    if (!viewport->m_topologyPickFilterActive ||
        !viewport->m_selectableTopologyEdges.contains(edge.id) ||
        viewport->m_selectableTopologyEdges.contains(sideEdge.id)) {
        std::cerr << "RED: camera rotation changed the machining-plane edge set\n";
        std::_Exit(1);
    }
    viewport->setStandardView(ViewportWidget::StandardView::Top);
    viewport->clearTopologyPickFilter();
    pickedId.clear();
    click(*viewport, projectPoint(*viewport, QVector3D(0, 6, 0)));
    if (pickedId != sideEdge.id) {
        std::cerr << "RED: clearing the machining-plane filter must restore normal picking\n";
        std::_Exit(1);
    }

    pickedId.clear();
    viewport->setTopologyPickMode(static_cast<int>(ViewportWidget::TopologyPickMode::Vertex));
    click(*viewport, projectPoint(*viewport, leftVertex.point));
    if (pickedId != leftVertex.id || pickedKind != 2 ||
        (pickedPoint - leftVertex.point).length() > 0.05f) {
        std::cerr << "real mouse click must return the stable topology vertex\n";
        std::_Exit(1);
    }

    pickedId.clear();
    const QPoint dragStart = projectPoint(*viewport, QVector3D(0, 0, 0));
    QMouseEvent press(QEvent::MouseButtonPress, dragStart, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &press);
    QMouseEvent move(QEvent::MouseMove, dragStart + QPoint(20, 10), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, dragStart + QPoint(20, 10),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &release);
    if (!pickedId.isEmpty()) {
        std::cerr << "camera drag must not emit a topology pick\n";
        std::_Exit(1);
    }

    // A drag remains a drag after returning to its press position.
    viewport->setStandardView(ViewportWidget::StandardView::Top);
    viewport->setTopologyPickMode(1);
    pickedId.clear();
    const QPoint center = projectPoint(*viewport, QVector3D(0, 0, 0));
    QMouseEvent down(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent out(QEvent::MouseMove, center + QPoint(40, 0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent back(QEvent::MouseMove, center, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent up(QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &down);
    QApplication::sendEvent(viewport, &out);
    QApplication::sendEvent(viewport, &back);
    QApplication::sendEvent(viewport, &up);
    bool failed = false;
    if (!pickedId.isEmpty()) { std::cerr << "FAIL out-and-back drag emitted pick\n"; failed = true; }

    // A 0.1 mm plate must hide its rear edge and vertex at the same projected point.
    mesh.bbMin.setZ(-0.1f);
    edge.points = {{-8, 0, -0.1f}, {8, 0, -0.1f}};
    leftVertex.point = {0, 0, -0.1f};
    mesh.pickEdges = {edge}; mesh.pickVertices = {leftVertex};
    viewport->setMesh(mesh); viewport->setStandardView(ViewportWidget::StandardView::Top);
    viewport->fitAll();
    if (viewport->pickFaceAt({400,300}) != 1) { std::cerr << "FAIL plate fixture\n"; std::_Exit(1); }
    if (viewport->pickEdgeAt({400,300},nullptr,nullptr,nullptr)) {
        std::cerr << "FAIL rear edge visible through 0.1 mm plate\n"; failed = true;
    }
    if (viewport->pickVertexAt({400,300},nullptr,nullptr)) {
        std::cerr << "FAIL rear vertex visible through 0.1 mm plate\n"; failed = true;
    }
    mesh.pickEdges[0].points = {{-8,0,0},{8,0,0}};
    mesh.pickVertices[0].point.setZ(0);
    viewport->setMesh(mesh);
    if (!viewport->pickEdgeAt({400,300},nullptr,nullptr,nullptr) ||
        !viewport->pickVertexAt({400,300},nullptr,nullptr)) {
        std::cerr << "FAIL coplanar visible topology rejected\n"; failed = true;
    }
    viewport->setStandardView(ViewportWidget::StandardView::Isometric);
    viewport->fitAll();
    const QPoint visible = projectPoint(*viewport, {0,0,0});
    if (!viewport->pickEdgeAt(visible + QPoint(0,5),nullptr,nullptr,nullptr)) {
        std::cerr << "FAIL near-edge pick on tilted surface rejected\n"; failed = true;
    }
    QWheelEvent wheel(QPointF(400,300),QPointF(400,300),QPoint(),QPoint(0,120),
                      Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(viewport,&wheel);
    QMouseEvent panDown(QEvent::MouseButtonPress,QPoint(400,300),Qt::RightButton,Qt::RightButton,Qt::NoModifier);
    QMouseEvent panMove(QEvent::MouseMove,QPoint(420,310),Qt::NoButton,Qt::RightButton,Qt::NoModifier);
    QMouseEvent panUp(QEvent::MouseButtonRelease,QPoint(420,310),Qt::RightButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(viewport,&panDown);QApplication::sendEvent(viewport,&panMove);QApplication::sendEvent(viewport,&panUp);
    if (!viewport->pickEdgeAt(projectPoint(*viewport,{0,0,0}),nullptr,nullptr,nullptr)) {
        std::cerr << "FAIL immediate zoom/pan picking rejected\n"; failed = true;
    }
    if (failed) { std::cerr.flush(); std::_Exit(1); }

    std::cout << "PASS real mouse topology edge/vertex pick and drag protocol\n";
    std::cout.flush();
    std::_Exit(0);
}
