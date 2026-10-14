#include <QApplication>
#include <QDir>
#include <QImage>
#include <iostream>
#include "../src/workspaces/shared/ui/ViewportWidget.h"

int main(int argc,char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc,argv);
    ViewportWidget view;
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.resize(800,600);
    MeshData mesh;mesh.bbMin={-50,-50,0};mesh.bbMax={50,50,0};
    Triangle tri;tri.v0={-50,-50,0};tri.v1={50,-50,0};tri.v2={50,50,0};tri.normal={0,0,1};tri.faceIndex=1;
    mesh.triangles.append(tri);tri.v1={50,50,0};tri.v2={-50,50,0};mesh.triangles.append(tri);
    view.setMesh(mesh);view.setStandardView(ViewportWidget::StandardView::Top);
    view.setToolPath({{-5,3,1},{5,3,1}},{false,false});view.setToolPosition({5,3,1});
    view.show();QCoreApplication::processEvents();
    const auto shown=view.grabFramebuffer();
    view.setModelVisible(false);QCoreApplication::processEvents();
    const auto hidden=view.grabFramebuffer();
    view.setModelVisible(true);QCoreApplication::processEvents();
    const auto restored=view.grabFramebuffer();
    const bool ok=!shown.isNull()&&!hidden.isNull()&&shown!=hidden&&shown==restored;
    std::cout<<(ok?"PASS ":"FAIL ")<<"native hidden OpenGL model render changes and restores exactly"<<std::endl;
    if(argc>1){const QString out=QString::fromLocal8Bit(argv[1]);QDir().mkpath(out);shown.save(out+"/model-visible.png");hidden.save(out+"/model-hidden.png");restored.save(out+"/model-restored.png");}
    return ok?0:1;
}
