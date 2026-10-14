#include <QApplication>
#include <QOpenGLWidget>
#include <QMatrix4x4>
#include <QSet>
#include <QMouseEvent>
#include <QWheelEvent>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#undef private
#include <iostream>
#include <cmath>
static QPointF projected(ViewportWidget &v, const QVector3D &point) {
 QMatrix4x4 camera;
 camera.translate(0,0,-200.0f/v.m_zoom);
 camera.rotate(v.m_rotX,1,0,0); camera.rotate(v.m_rotZ,0,0,1); camera.translate(v.m_pan);
 const auto p=(v.m_proj*camera).map(point);
 return {(p.x()+1)*v.width()/2.0,(1-p.y())*v.height()/2.0};
}
int main(int argc,char **argv) {
 QApplication app(argc,argv); ViewportWidget v;
 MeshData mesh;mesh.bbMin={20,-30,-5};mesh.bbMax={80,10,15};Triangle t;t.v0=mesh.bbMin;t.v1=mesh.bbMax;t.v2={80,-30,-5};mesh.triangles.append(t);
 for (auto size : {QSize(800,600),QSize(480,900)}) for (int view=0;view<7;++view) for (auto button : {Qt::RightButton,Qt::MiddleButton}) {
 v.resize(size);v.setMesh(mesh);v.setStandardView(static_cast<ViewportWidget::StandardView>(view));
 for (float zoom : {0.5f,2.0f}) {
 v.m_zoom*=zoom;v.updateProjection();
 const QVector3D center=(mesh.bbMin+mesh.bbMax)*0.5f;
 const auto before=projected(v,center);
 QMouseEvent press(QEvent::MouseButtonPress,{200,200},button,button,Qt::NoModifier);QApplication::sendEvent(&v,&press);
 QMouseEvent move(QEvent::MouseMove,{240,225},Qt::NoButton,button,Qt::NoModifier);QApplication::sendEvent(&v,&move);
 QMouseEvent release(QEvent::MouseButtonRelease,{240,225},button,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&v,&release);
 const auto delta=projected(v,center)-before;
 if (std::abs(delta.x()-40)>.1 || std::abs(delta.y()-25)>.1) {std::cerr<<"FAIL view "<<view<<" drag 40,25 moved "<<delta.x()<<","<<delta.y()<<"\n";return 1;}
 }
 }
 v.resize(800,600);v.setMesh(mesh);v.setStandardView(ViewportWidget::StandardView::Isometric);
 const float beforeRotX=v.m_rotX,beforeRotZ=v.m_rotZ;
 QMouseEvent rotatePress(QEvent::MouseButtonPress,{200,200},Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&v,&rotatePress);
 QMouseEvent rotateMove(QEvent::MouseMove,{240,225},Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&v,&rotateMove);
 QMouseEvent rotateRelease(QEvent::MouseButtonRelease,{240,225},Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&v,&rotateRelease);
 if (std::abs(v.m_rotZ-beforeRotZ-20.0f)>.001f || std::abs(v.m_rotX-beforeRotX-12.5f)>.001f) {std::cerr<<"FAIL left drag rotation delta "<<v.m_rotZ-beforeRotZ<<","<<v.m_rotX-beforeRotX<<"\n";return 2;}
 const float beforeZoom=v.m_zoom;
 QWheelEvent wheelIn(QPointF(200,200),QPointF(200,200),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&v,&wheelIn);
 if (std::abs(v.m_zoom-beforeZoom*1.1f)>.0001f) {std::cerr<<"FAIL wheel zoom factor "<<v.m_zoom/beforeZoom<<"\n";return 3;}
 std::cout<<"PASS right/middle pan, left-drag rotation and wheel zoom across viewport regression\n";
 return 0;
}
