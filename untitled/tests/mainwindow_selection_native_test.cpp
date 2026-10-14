#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QPushButton>
#include <QListWidget>
#include <iostream>
#include <cstdlib>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#undef private
#include "../src/app/AppController.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

static void wait(int ms) { QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec(); }
static void check(bool ok,const char *what) { if(!ok){std::cerr<<"FAIL "<<what<<std::endl;std::_Exit(1);} }
static void foreground() { DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);check(pid==GetCurrentProcessId(),"foreground changed; native input stopped"); }
static void activate(HWND window) {
    if(SetForegroundWindow(window))return;
    const DWORD foregroundThread=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
    const DWORD currentThread=GetCurrentThreadId();
    const bool attached=foregroundThread!=currentThread && AttachThreadInput(currentThread,foregroundThread,TRUE);
    BringWindowToTop(window);SetForegroundWindow(window);
    if(attached)AttachThreadInput(currentThread,foregroundThread,FALSE);
}
static void move(QWidget *w,QPoint p) { foreground();auto g=w->mapToGlobal(p);SetCursorPos(g.x(),g.y());wait(180); }
static void click(QWidget *w,QPoint p) {move(w,p);mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0);mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0);wait(220);}
static void key(BYTE code) {foreground();keybd_event(code,0,0,0);keybd_event(code,0,KEYEVENTF_KEYUP,0);wait(120);}
static QPoint projectPoint(ViewportWidget &v,QVector3D p) {auto n=(v.m_proj*v.currentViewMatrix()).map(p);return {qRound((n.x()+1)*v.width()/2),qRound((1-n.y())*v.height()/2)};}
int main(int argc,char **argv) {
    check(argc==3||argc==4,"model and evidence directory required");
    const bool probe=argc==4;
    QApplication app(argc,argv);QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());
    StepImporter importer;check(importer.load(QString::fromLocal8Bit(argv[1])),"STEP load");
    const QString output=QString::fromLocal8Bit(argv[2]);QDir().mkpath(output);
    QFile log(output+"/steps.txt");check(log.open(QIODevice::WriteOnly|QIODevice::Text),"evidence log");
    auto record=[&](QString s){log.write((s+"\n").toUtf8());log.flush();};
    auto hash=[](QString p){QFile f(p);if(!f.open(QIODevice::ReadOnly))return QByteArray();return QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex();};
    record("Test SHA256="+hash(QCoreApplication::applicationFilePath()));
    record("Application SHA256="+hash(QCoreApplication::applicationDirPath()+"/CNEXT-CAM.exe"));
    record("STEP SHA256="+hash(QString::fromLocal8Bit(argv[1])));
    auto *window=new MainWindow;auto *project=AppController::instance().projectManager();
    project->setLoadedMesh(importer.mesh());project->setFeatures(importer.features());project->setSourceFileFingerprint("native-real-step");
    SetupOrigin origin;origin.confirmed=true;project->setSetupOrigin(origin);
    StockDefinition stock;stock.confirmed=true;project->setStockDefinition(stock);
    window->m_viewport->setMesh(importer.mesh());window->resize(1500,950);
    window->move(app.primaryScreen()->availableGeometry().topLeft());
    if(probe){window->resize(1856,1080);window->show();}else window->showMaximized();
    window->raise();window->activateWindow();
    if(!probe)activate(reinterpret_cast<HWND>(window->winId()));wait(1500);if(!probe)foreground();
    record(QString("DPI=%1 DPR=%2").arg(window->screen()->logicalDotsPerInch()).arg(window->devicePixelRatioF()));
    record(probe?"Programmatic offscreen geometry probe; no native input.":"Fixture initialization and Top camera are programmatic; all following selection actions use Win32 mouse/keyboard.");
    auto capture=[&](QString name){if(probe)return;foreground();check(window->screen()->grabWindow(window->winId()).save(output+"/"+name+".png"),"screen capture");};
    QToolButton *entry=nullptr;for(auto *b:window->findChildren<QToolButton *>())if(b->text()==QString::fromUtf8("挖槽"))entry=b;
    check(entry&&entry->isVisible(),"native machining command");capture("00-main-window");if(probe)entry->click();else click(entry,entry->rect().center());
    auto *dialog=dynamic_cast<SelectionChainDialog *>(window->m_geometryDialog.data());check(dialog&&dialog->isVisible(),"embedded selection opened");
    auto &view=*window->m_viewport;view.setStandardView(ViewportWidget::StandardView::Top);view.fitAll();wait(300);
    auto *details=dialog->findChild<QPushButton *>("selectionMoreDetails");if(probe)details->click();else click(details,details->rect().center());
    auto *mode=dialog->findChild<QComboBox *>("selectionMode");check(mode->isVisible(),"mode control visible");
    if(probe){mode->setCurrentIndex(0);QMetaObject::invokeMethod(mode,"activated",Q_ARG(int,0));}
    else {click(mode,mode->rect().center());key(VK_HOME);key(VK_RETURN);}
    if(probe){view.m_autoFit=false;view.m_zoom*=2.3579477f;view.updateProjection();}
    else {move(&view,view.rect().center());for(int i=0;i<9;++i){mouse_event(MOUSEEVENTF_WHEEL,0,0,120,0);wait(150);}}
    check(view.m_confirmedContourPoints.isEmpty()&&view.m_contourChoicePoints.isEmpty(),"empty entry");capture("01-empty-entry");
    struct Target {QString id;QPoint pixel;double length;bool straight;};QVector<Target> targets;
    const auto &mesh=importer.mesh();
    for(const auto &loop:mesh.boundaryLoops) {
        for(int index=0;index<loop.orderedEdgeIds.size();++index) {
            // Verified feature-board slot: 132 mm straight edge and its actual
            // adjacent 20.4183 mm arc. Do not substitute nearby or disjoint edges.
            if(loop.orderedEdgeIds[index]!="edge:454" ||
               loop.orderedEdgeIds[(index+1)%loop.orderedEdgeIds.size()]!="edge:455")continue;
            QVector<Target> pair;
            for(int k=0;k<2;++k) {
                int n=(index+k)%loop.orderedEdgeIds.size();const MeshPickEdge *edge=nullptr;
                for(const auto &e:mesh.pickEdges)if(e.id==loop.orderedEdgeIds[n])edge=&e;
                if(!edge||edge->points.size()<2||!view.m_selectableTopologyEdges.contains(edge->id))break;
                double length=0,screenLength=0;
                for(int j=1;j<edge->points.size();++j){length+=(edge->points[j]-edge->points[j-1]).length();screenLength+=(projectPoint(view,edge->points[j])-projectPoint(view,edge->points[j-1])).manhattanLength();}
                if(probe)record(QString("probe face=%1 edge=%2 length=%3 screen=%4").arg(loop.faceIndex).arg(edge->id).arg(length).arg(screenLength));
                if(length<15||screenLength<50)break;
                double remaining=length*(loop.orderedForward.value(n,true)?.3:.7);QVector3D sample=edge->points.last();
                for(int j=1;j<edge->points.size();++j){double part=(edge->points[j]-edge->points[j-1]).length();if(remaining<=part&&part>0){sample=edge->points[j-1]+(edge->points[j]-edge->points[j-1])*float(remaining/part);break;}remaining-=part;}
                auto point=projectPoint(view,sample);
                QString hit;QVector3D hp;double parameter=0;
                bool inside=view.rect().adjusted(15,15,-15,-15).contains(point);
                bool picked=view.pickEdgeAt(point,&hit,&hp,&parameter);
                if(probe)record(QString("probe pick wanted=%1 hit=%2 inside=%3 at=%4,%5").arg(edge->id).arg(hit).arg(inside).arg(point.x()).arg(point.y()));
                if(!inside||!picked||hit!=edge->id)break;
                pair.append({edge->id,point,length,(edge->points.last()-edge->points.first()).length()>=length-.05});
                if(pair.size()==2)break;
            }
            if(pair.size()==2&&(pair[0].straight||pair[1].straight)){targets=pair;break;}
        }
        if(targets.size()==2)break;
    }
    check(targets.size()==2,"two visible adjacent long edges on one real loop");
    for(int i=0;i<2;++i){auto g=view.mapToGlobal(targets[i].pixel);record(QString("%1 %2 length=%3 local=%4,%5 screen=%6,%7").arg(i?'B':'A').arg(targets[i].id).arg(targets[i].length).arg(targets[i].pixel.x()).arg(targets[i].pixel.y()).arg(g.x()).arg(g.y()));}
    if(probe){
        for(const auto &target:targets){QString hit;QVector3D hp;double parameter=0;view.pickEdgeAt(target.pixel,&hit,&hp,&parameter);dialog->selectTopology(hit,1,hp,parameter);}
        auto draft=dialog->m_controller.commit();QString reason;
        bool continuous=dialog->topologyRefsContinuous(dialog->m_candidates[dialog->m_candidateIndex],draft,&reason);
        record(QString("A/B continuous=%1 reason=%2 renderedPoints=%3").arg(continuous).arg(reason).arg(view.m_confirmedContourPoints.size()));
        check(continuous&&!view.m_confirmedContourPoints.isEmpty(),"real STEP A/B must retain visible confirmed geometry");
        record("PASS geometry-only offscreen probe; no native input or screenshots.");std::cout<<"PASS geometry probe"<<std::endl;std::_Exit(0);
    }
    auto *rows=dialog->findChild<QListWidget *>("selectionEdges");
    move(&view,targets[0].pixel);check(!view.m_topologyHoverPoints.isEmpty(),"hover A displayed");capture("02-hover-A");
    move(&view,{10,10});check(view.m_topologyHoverPoints.isEmpty(),"leave clears hover");capture("03-leave-A");
    click(&view,targets[0].pixel);move(&view,{10,10});check(rows->count()==1,"selected A");capture("04-selected-A");
    auto selected=view.m_confirmedContourPoints;
    move(&view,targets[1].pixel);check(view.m_confirmedContourPoints==selected&&!view.m_topologyHoverPoints.isEmpty(),"hover B retains A");capture("05-hover-B-retains-A");
    auto contains=[&](QString id){for(int i=0;i<rows->count();++i)if(rows->item(i)->text().startsWith(id+" "))return true;return false;};
    click(&view,targets[1].pixel);move(&view,{10,10});
    const int selectedCount=rows->count();check(selectedCount==2&&contains(targets[0].id)&&contains(targets[1].id),"selected adjacent A B");
    check(view.m_confirmedContourPoints.size()>selected.size(),"A B persistent geometry highlight includes both edges");
    const auto selectedAB=view.m_confirmedContourPoints;
    capture("06-selected-A-B");
    click(&view,targets[0].pixel);move(&view,{10,10});check(rows->count()==selectedCount-1&&!contains(targets[0].id)&&contains(targets[1].id),"cancel A retains B");
    check(!view.m_confirmedContourPoints.isEmpty()&&view.m_confirmedContourPoints!=selectedAB,"cancel A retains B geometry");capture("07-cancel-A");
    foreground();keybd_event(VK_CONTROL,0,0,0);key('Z');keybd_event(VK_CONTROL,0,KEYEVENTF_KEYUP,0);wait(200);
    check(rows->count()==selectedCount&&contains(targets[0].id)&&contains(targets[1].id),"undo restores A B");capture("08-undo");
    check(view.m_confirmedContourPoints==selectedAB,"undo restores the complete A B geometry");
    check(project->operations().isEmpty(),"selection never creates operation");
    record("PASS native main-window entry, empty, hover/leave, A/B, cancel A, Ctrl+Z. No operation or machine action.");
    std::cout<<"PASS native real STEP long-edge selection"<<std::endl;std::_Exit(0);
}
