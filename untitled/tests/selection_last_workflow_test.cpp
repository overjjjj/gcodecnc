#include <QApplication>
#include <QComboBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QFile>
#include <QMessageBox>
#include <QTimer>
#include <iostream>
#include <cstdlib>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#undef private
#include "../src/app/AppController.h"
#include "../src/strategies/StrategyFactory.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"

class HiddenMessages : public QObject {
    bool eventFilter(QObject *object, QEvent *event) override {
        return event->type() == QEvent::Show && qobject_cast<QMessageBox *>(object);
    }
};

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
    HiddenMessages messages;app.installEventFilter(&messages);
    QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,[](){
        for(auto *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox*>(widget)){
            std::cerr << box->text().toStdString() << std::endl;box->reject();
        }
    });dismiss.start(10);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());
    ToolEntry mill;mill.id=903;mill.type="end_mill";mill.diameter=4;mill.fluteLen=12;mill.totalLen=50;
    ToolLibrary::instance().addTool(mill);
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
    project->setActiveRegion(FaceRegion::Front);window->m_activeRegion=FaceRegion::Front;
    project->setSourceFileFingerprint("hover-fixture");
    SetupOrigin origin;origin.confirmed=true;project->setSetupOrigin(origin);
    StockDefinition stock;stock.confirmed=true;project->setStockDefinition(stock);
    MachiningFeature feature;feature.kind=FeatureKind::FlatSurface;feature.subType="flat_surface";feature.axis={0,0,1};feature.boundaryPoints=corners;feature.faceIndices={1};feature.planarBoundaryVerified=true;project->setFeatures({feature});
    feature.region=FaceRegion::Front;feature.length=40;feature.width=20;project->setFeatures({feature});
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
    auto establish=[&](){
        auto params=StrategyFactory::instance().strategy("mill_face")->defaultParams();
        params.set("depth",1);params.set("stepover",2);params.set("safeHeight",20);params.set("feedHeight",3);
        window->m_strategyPanel->restoreDraft(params,mill.id);
        window->m_strategyPanel->onConfirmOperation();
    };
    establish();CHECK(project->operations().size()==1);
    const auto first=project->operations().first();CHECK(first.selectionEvidence.explicitUserSelection);
    const QString path=settings.filePath("restored-project.json");CHECK(project->saveToFile(path));
    CHECK(project->loadFromFile(path));project->setLoadedMesh(mesh);
    CHECK(project->lastEstablishedSelection()==first.selectionEvidence);
    auto restore=[&](){
        window->beginOperationWorkflow("mill_face");
        auto *selection=dynamic_cast<SelectionChainDialog *>(window->m_geometryDialog.data());CHECK(selection);
        auto *last=selection->findChild<QPushButton *>("selectionLast");CHECK(last && last->isEnabled());last->click();
        CHECK(selection->selectedCandidateGeometryId()==first.selectionEvidence.candidateGeometryId);
        if(argc>2){QApplication::processEvents();CHECK(selection->grab().save(QString::fromLocal8Bit(argv[2])));}
        selection->accept();CHECK(window->m_parameterDialog->isVisible());
    };
    restore();window->m_parameterDialog->reject();
    CHECK(project->operations().size()==1 && project->lastEstablishedSelection()==first.selectionEvidence);
    restore();establish();CHECK(project->operations().size()==2);
    CHECK(project->operations().last().id!=first.id);
    CHECK(project->operations().last().selectionEvidence==first.selectionEvidence);
    ProgramGenerationService service([](const QString&id){return StrategyFactory::instance().strategy(id);},[](int id){return ToolLibrary::instance().tool(id);});
    Cq8PostProcessor post;PostProcessorOptions options;options.workOffset="G54";options.safeStartBlocks=QStringList{"G17 G40 G49 G80","G21","G90","G54","G94"};
    ProgramGenerationSnapshotOptions snapshot;
    snapshot.stockConfirmed=project->stockDefinition().confirmed;
    snapshot.stockBounds=project->stockDefinition().resolvedBounds(mesh.bbMin,mesh.bbMax);
    snapshot.sourceFingerprint=project->sourceFileFingerprint();snapshot.setupFingerprint=project->setupFingerprint();snapshot.sourceMesh=&mesh;
    const auto result=service.generate(project->operations(),post,options,snapshot);
    if(!result.ok)std::cerr<<result.errors.join(" | ").toStdString()<<std::endl;
    CHECK(result.ok);
    const QString finalNc=result.snapshot.expandedGcodeText.isEmpty()
        ? result.snapshot.gcodeText : result.snapshot.expandedGcodeText;
    CHECK(!finalNc.isEmpty());
    if(argc>1){QFile nc(QString::fromLocal8Bit(argv[1]));CHECK(nc.open(QIODevice::WriteOnly));nc.write(finalNc.toUtf8());nc.close();}
    SimulationController sim;sim.loadGCode(finalNc);CHECK(sim.motionError().isEmpty());
    bool depth=false;for(const auto &move:sim.moves())depth|=!move.isRapid && std::abs(move.to.z()+1)<0.0001;
    CHECK(depth);
    std::cout<<"PASS MainWindow established operation, reload, restore, parameter cancel, second operation, final NC and simulation"<<std::endl;std::_Exit(0);
}
