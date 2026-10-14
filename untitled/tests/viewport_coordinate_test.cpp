#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QEventLoop>
#include <QTimer>
#include <iostream>
static void settle() { QEventLoop loop; QTimer::singleShot(150, &loop, &QEventLoop::quit); loop.exec(); }
int main(int argc, char **argv) {
 QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
 QApplication app(argc, argv);
 ViewportWidget v; v.resize(600,400); v.show();
 MeshData mesh; mesh.bbMin={-10,-10,0}; mesh.bbMax={10,10,0};
 Triangle t; t.v0={-10,-10,0};t.v1={10,-10,0};t.v2={0,10,0};t.normal={0,0,1};t.faceIndex=1;mesh.triangles.append(t);
 v.setMesh(mesh); v.setStandardView(ViewportWidget::StandardView::Top); settle();
 auto *label=v.findChild<QLabel *>("mouseCoordinates");
 if (!label || !v.hasMouseTracking()) { std::cerr << "FAIL coordinate box and hover tracking required\n"; return 1; }
 QMouseEvent move(QEvent::MouseMove,QPointF(300,200),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
 QApplication::sendEvent(&v,&move); settle();
 if (!label->text().contains("0.000") || label->text().contains(QStringLiteral("—"))) return 2;
 mesh.triangles[0].v0.setZ(-5); mesh.triangles[0].v1.setZ(-5); mesh.triangles[0].v2.setZ(5);
 mesh.bbMin.setZ(-5); mesh.bbMax.setZ(5);
 v.setMesh(mesh); v.setStandardView(ViewportWidget::StandardView::Top);
 QApplication::sendEvent(&v,&move); settle();
 if (!label->text().contains("Y: 0.000") && !label->text().contains("Y: -0.000")) { std::cerr << "FAIL perspective surface coordinate: " << label->text().toStdString(); return 5; }
 QEvent leave(QEvent::Leave); QApplication::sendEvent(&v,&leave); settle();
 if (!label->text().contains(QStringLiteral("—"))) return 3;
 v.setMesh({}); QApplication::sendEvent(&v,&move); settle();
 if (!label->text().contains(QStringLiteral("—"))) return 4;
 std::cout << "PASS surface XY hover, leave and empty model\n";
}
