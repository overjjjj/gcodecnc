#include <QApplication>
#include <QComboBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>
#include <cstdlib>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#undef private
#include "../src/app/AppController.h"

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x << std::endl; std::_Exit(1); } } while(0)
static QPoint projectPoint(ViewportWidget &v,QVector3D p) {
    auto n=(v.m_proj*v.currentViewMatrix()).map(p);
    return {qRound((n.x()+1)*v.width()/2),qRound((1-n.y())*v.height()/2)};
}
static void hover(ViewportWidget &v,QVector3D p) {
    QMouseEvent event(QEvent::MouseMove,projectPoint(v,p),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&v,&event);
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());
    auto *window=new MainWindow;
    MeshData mesh; mesh.bbMin={-20,-10,-2};mesh.bbMax={20,10,0};
    QVector<QVector3D> corners={{-20,-10,0},{20,-10,0},{20,10,0},{-20,10,0}};
    MeshBoundaryLoop loop;loop.faceId="face:1";loop.faceIndex=1;loop.outer=true;
    for(int i=0;i<4;++i) {
        MeshPickEdge e;e.id=QString("edge:%1").arg(i+1);e.firstVertexId=QString("vertex:%1").arg(i+1);
        e.lastVertexId=QString("vertex:%1").arg((i+1)%4+1);e.points={corners[i],corners[(i+1)%4]};e.parameters={0,1};e.adjacentFaceIndices={1};
        mesh.pickEdges.append(e);loop.orderedEdgeIds.append(e.id);loop.orderedForward.append(true);
        MeshPickVertex v;v.id=e.firstVertexId;v.point=corners[i];mesh.pickVertices.append(v);
    }
    mesh.boundaryLoops={loop};mesh.triangles={{corners[0],corners[1],corners[2],{0,0,1},1},{corners[0],corners[2],corners[3],{0,0,1},1}};
    auto *project=AppController::instance().projectManager();project->setLoadedMesh(mesh);
    project->setSourceFileFingerprint("hover-fixture");
    SetupOrigin origin;origin.confirmed=true;project->setSetupOrigin(origin);
    StockDefinition stock;stock.confirmed=true;project->setStockDefinition(stock);
    window->resize(1400,900);window->show();
    window->m_viewport->setMesh(mesh);
    window->beginOperationWorkflow("mill_open_contour");
    auto *dialog=dynamic_cast<SelectionChainDialog *>(window->m_geometryDialog.data());CHECK(dialog && dialog->isVisible());
    auto &view=*window->m_viewport;view.setStandardView(ViewportWidget::StandardView::Top);view.fitAll();
    CHECK(view.m_confirmedContourPoints.isEmpty() && view.m_contourChoicePoints.isEmpty());
    hover(view,{0,-10,0});
    CHECK(view.m_topologyHoverPoints==mesh.pickEdges[0].points);
    CHECK(view.m_confirmedContourPoints.isEmpty() && view.m_contourChoicePoints.isEmpty());
    QEvent initialLeave(QEvent::Leave);QApplication::sendEvent(&view,&initialLeave);
    CHECK(view.m_topologyHoverPoints.isEmpty());
    dialog->selectGeometry("topology:face:1");
    CHECK(dialog->hasCandidatePreview());
    const auto candidate=view.m_contourChoicePoints;CHECK(candidate.size()>=4);
    hover(view,{20,0,0});
    CHECK(view.m_contourChoicePoints==candidate); // hover must not overwrite a pending whole loop
    CHECK(view.m_topologyHoverPoints==mesh.pickEdges[1].points);
    QEvent leave(QEvent::Leave);QApplication::sendEvent(&view,&leave);
    CHECK(view.m_contourChoicePoints==candidate && view.m_confirmedContourPoints.isEmpty());
    CHECK(view.m_topologyHoverPoints.isEmpty());
    auto *mode=dialog->findChild<QComboBox *>("selectionMode");
    mode->setCurrentIndex(0);QMetaObject::invokeMethod(mode,"activated",Q_ARG(int,0));
    CHECK(view.m_topologyHoverPoints.isEmpty() && view.m_contourChoicePoints.isEmpty());
    dialog->selectTopology("edge:1",1,{-19,-10,0},.01);
    const auto selected=view.m_confirmedContourPoints;
    hover(view,{20,0,0});CHECK(view.m_confirmedContourPoints==selected);
    QApplication::sendEvent(&view,&leave);CHECK(view.m_confirmedContourPoints==selected);
    dialog->selectTopology("edge:2",1,{20,-9,0},.01);
    dialog->selectTopology("edge:1",1,{-19,-10,0},.01);
    CHECK(view.m_confirmedContourPoints==mesh.pickEdges[1].points);
    dialog->findChild<QPushButton *>("selectionUndo")->click();
    CHECK(view.m_confirmedContourPoints.size()==3);
    dialog->reject();CHECK(view.m_contourChoicePoints.isEmpty() && view.m_confirmedContourPoints.isEmpty() && view.m_topologyHoverPoints.isEmpty());
    std::cout<<"PASS real MainWindow hover preserves pending boundary and cancel clears overlays"<<std::endl;
    std::_Exit(0);
}
