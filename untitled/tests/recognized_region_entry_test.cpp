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
    MachiningFeature feature;feature.kind=FeatureKind::FlatSurface;feature.subType="flat_surface";feature.axis={0,0,1};feature.boundaryPoints=corners;feature.faceIndices={1};feature.planarBoundaryVerified=true;project->setFeatures({feature});
    window->setAttribute(Qt::WA_DontShowOnScreen);window->m_parameterDialog->setAttribute(Qt::WA_DontShowOnScreen);
    window->resize(1400,900);window->show();
    window->m_viewport->setMesh(mesh);
    window->beginOperationWorkflow("mill_face");
    auto *dialog=dynamic_cast<SelectionChainDialog *>(window->m_geometryDialog.data());CHECK(dialog && dialog->isVisible());
    SelectionChain selection;selection.selectionMode=ChainSelectionMode::Face;selection.orderedTopologyRefs={{"face:1",SelectionTopologyKind::Face,true}};
    SelectionBoundaryLoop evidence;evidence.faceId=loop.faceId;evidence.outer=true;evidence.orderedEdgeIds=loop.orderedEdgeIds;evidence.orderedForward=loop.orderedForward;selection.boundaryLoops={evidence};selection.closed=true;
    CHECK(dialog->acceptanceError(selection,"topology:face:1").isEmpty());
    dialog->m_mode->setCurrentIndex(int(ChainSelectionMode::Face));QMetaObject::invokeMethod(dialog->m_mode,"activated",Q_ARG(int,int(ChainSelectionMode::Face)));
    dialog->selectTopology("face:1",0,{0,0,0},0);
    if(!dialog->m_pendingTopologyPick.isEmpty()){
        int target=-1;for(int i=0;i<dialog->m_pendingTopologyPick.matches.size();++i)if(dialog->m_candidates[dialog->m_pendingTopologyPick.matches[i].candidateIndex].geometryId=="topology:face:1")target=i;
        CHECK(target>=0);dialog->m_pendingTopologyPick.matchIndex=target;dialog->confirmPendingTopologyMatch();
    }
    dialog->accept();CHECK(window->m_parameterDialog->isVisible());
    CHECK(window->m_workflowFeatureIndex==0);
    std::cout<<"PASS generic face binds matching recognized planar region and opens parameters"<<std::endl;std::_Exit(0);
}
