#include <QApplication>
#include <QOpenGLWidget>
#include <QOpenGLFunctions_1_1>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QMatrix4x4>
#include <QSet>
#include <QPainter>
#include <QTemporaryDir>
#include <QSettings>
#include <QDir>
#include <QTimer>
#include <QEventLoop>
#include "../src/import/StepImporter.h"
#include "../src/core/Settings.h"
#define private public
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#undef private
#include <iostream>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class Probe : public ViewportWidget {
public:
    int mode=0, frames=0;
    QString evidence;
    void state(const char *where) {
        GLint program=0, texture=0, active=0, src=0, dst=0; GLfloat color[4]={}; GLboolean depthWrite=false;
        glGetIntegerv(GL_CURRENT_PROGRAM,&program); glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glGetIntegerv(GL_BLEND_SRC,&src); glGetIntegerv(GL_BLEND_DST,&dst);
        glGetFloatv(GL_CURRENT_COLOR,color);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depthWrite);
        std::cout<<"FRAME "<<frames<<" "<<where<<" program="<<program<<" texture="<<texture<<" active="<<active
                 <<" tex2d="<<int(glIsEnabled(GL_TEXTURE_2D))<<" depth="<<int(glIsEnabled(GL_DEPTH_TEST))
                 <<" depthWrite="<<int(depthWrite)<<" lighting="<<int(glIsEnabled(GL_LIGHTING))<<" fog="<<int(glIsEnabled(GL_FOG))
                 <<" blend="<<int(glIsEnabled(GL_BLEND))<<" func="<<src<<","<<dst<<" color="<<color[0]<<","<<color[1]<<","<<color[2]<<","<<color[3]
                 <<" error="<<glGetError()<<std::endl;
        if(frames==1) {
            GLint equation=0,logic=0; glGetIntegerv(GL_BLEND_EQUATION_RGB,&equation); glGetIntegerv(GL_LOGIC_OP_MODE,&logic);
            std::cout<<"EXTRA equation="<<equation<<" logic="<<int(glIsEnabled(GL_COLOR_LOGIC_OP))<<","<<logic
                <<" alphaTest="<<int(glIsEnabled(GL_ALPHA_TEST))<<" colorSum="<<int(glIsEnabled(GL_COLOR_SUM))<<std::endl;
        }
    }
protected:
    void paintGL() override {
        ++frames; if(frames<=4) state("entry");
        if(mode==4 || mode==6) {
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); glDisable(GL_DEPTH_TEST);
            glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,100,0,100,-1,1);
            glMatrixMode(GL_MODELVIEW); glLoadIdentity();
            for(int row=0;row<7;++row) {
                glDisable(GL_BLEND);
                if(row==3 || row==4) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); }
                if(mode==6 && (row==3 || row==4)) context()->functions()->glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
                glColor3f(1,0,0);
                const GLfloat rgba[4]={.56f,.60f,.65f,.35f};
                if(row==6) glColor4f(.56f,.60f,.65f,.35f);
                glBegin(GL_QUADS);
                if(row==0) glColor3f(.56f,.60f,.65f);
                if(row==1) glColor4f(.56f,.60f,.65f,1);
                if(row==2 || row==3) glColor4f(.56f,.60f,.65f,.35f);
                if(row==4 || row==5) glColor4fv(rgba);
                glVertex2f(20,5+row*13); glVertex2f(80,5+row*13); glVertex2f(80,14+row*13); glVertex2f(20,14+row*13);
                glEnd();
                if(frames==1) { unsigned char pixel[4]={}; glReadPixels(width()/2,int((9+row*13)*height()/100),1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                    std::cout<<"BAR "<<row<<" pixel="<<int(pixel[0])<<","<<int(pixel[1])<<","<<int(pixel[2])<<","<<int(pixel[3])<<std::endl; state("bar"); }
            }
            return;
        }
        if(frames==1) {
            glColor4f(.56f,.60f,.65f,.35f); state("direct-color4f");
            glBegin(GL_POINTS); glColor4f(.56f,.60f,.65f,.35f); glEnd(); state("begin-color4f");
            glColor3f(.56f,.60f,.65f); state("direct-color3f");
        }
        if(mode==2) context()->functions()->glUseProgram(0);
        if(mode==3 || mode==5) { glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); }
        if(mode==5) context()->functions()->glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        if(mode==1) {
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            m_view=currentViewMatrix(); glMatrixMode(GL_PROJECTION); glLoadMatrixf(m_proj.constData());
            glMatrixMode(GL_MODELVIEW); glLoadMatrixf(m_view.constData());
            drawGrid(); if(frames==1) state("grid"); drawAxes(); drawMesh(); drawSelectableTopology(); drawToolPath(); if(frames==1) state("path"); drawToolMarker();
        } else ViewportWidget::paintGL();
        if(frames<=4) state("exit");
    }
};
int main(int argc,char **argv) {
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc,argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    app.setOrganizationName("CNEXT-D09-Test"); app.setApplicationName("Render");
    Settings::instance().setShowGrid(true);
    Probe view; view.mode=argc>2?QString::fromLocal8Bit(argv[2]).toInt():0;
    view.setAttribute(Qt::WA_DontShowOnScreen); view.resize(700,700);
    MeshData wire; wire.bbMin={-40,-30,0}; wire.bbMax={40,30,0};
    MeshPickEdge edge; edge.id="edge:test"; edge.points={{-40,-30,0},{40,-30,0},{40,30,0},{-40,30,0},{-40,-30,0}};
    wire.pickEdges.append(edge); view.setMesh(wire); view.setStandardView(ViewportWidget::StandardView::Top);
    view.setToolPath({{-44,-34,1},{44,-34,1},{44,34,1},{-44,34,1},{-44,-34,1}}, {false,false,false,false,false});
    view.setToolDiameter(8); view.setToolPosition({20,15,2}); view.show();
    std::cout<<"SHOWN"<<std::endl;
    for(int i=0;i<4;++i) { view.update(); app.processEvents(); std::cout<<"SETTLED "<<i<<std::endl; }
    std::cout<<"GRAB_BEGIN"<<std::endl;
    const QImage picture=view.grab().toImage();
    std::cout<<"GRAB_END"<<std::endl;
    const QString folder=argc>1?QString::fromLocal8Bit(argv[1]):settings.path(); QDir().mkpath(folder);
    picture.save(folder+"/frame.png");
    view.grabFramebuffer().save(folder+"/framebuffer.png");
    int yellow=0,green=0;
    for(int y=picture.height()/5;y<picture.height()*4/5;++y) for(int x=0;x<picture.width();++x) {
        const auto c=picture.pixelColor(x,y);
        if(c.red()>230&&c.green()>230&&c.blue()<60) ++yellow;
        if(c.green()>100 && c.green()>c.red()*1.5 && c.green()>c.blue()*1.3) ++green;
    }
    std::cout<<"IMAGE yellow="<<yellow<<" green="<<green<<" frames="<<view.frames<<std::endl;
    const bool hidden=!IsWindowVisible(reinterpret_cast<HWND>(view.winId()));
    std::cout<<(hidden?"PASS ":"FAIL ")<<"native window hidden"<<std::endl;
    auto nearPoint = [&](const QVector3D &p, bool cutting) {
        const auto clip=view.m_proj*view.currentViewMatrix()*QVector4D(p,1);
        const auto ndc=clip.toVector3DAffine();
        const int x=int((ndc.x()+1)*picture.width()/2), y=int((1-ndc.y())*picture.height()/2);
        int matching=0;
        for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
            const QColor c=picture.pixelColor(x+dx,y+dy);
            if(cutting ? (c.green()>200 && c.red()<100 && c.blue()<160)
                       : (c.red()>150 && c.red()<205 && c.green()>c.red() && c.blue()>c.green() && c.blue()-c.red()<30)) ++matching;
        }
        std::cout<<(cutting?"CUT_ROI ":"GRID_ROI ")<<x<<","<<y<<" matching="<<matching<<std::endl;
        return matching>0;
    };
    const bool good=nearPoint({0,-34,1},true) && nearPoint({-20,-10,0},false) && hidden && !picture.isNull();
    std::cout<<(good?"PASS ":"FAIL ")<<"grid retains neutral color and cutting path remains green over repeated frames"<<std::endl;
    return good?0:1;
}
