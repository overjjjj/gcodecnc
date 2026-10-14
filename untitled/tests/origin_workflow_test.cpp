#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QFile>
#include <QMessageBox>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QAction>
#include <iostream>
#include <cstdlib>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/workspaces/process_design/ui/ContourMachiningChoiceDialog.h"
#include "../src/workspaces/process_design/ui/SetupOriginDialog.h"
#undef private
#include "../src/app/AppController.h"
#include "../src/core/Settings.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include "../src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"
#include "../src/gcode/ProgramSnapshotStatus.h"
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
    Settings::instance().setPostProcessorId("cq8");
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
    MachiningFeature feature;feature.kind=FeatureKind::Boss;feature.subType="closed_contour";feature.axis={0,0,1};feature.boundaryPoints=corners;feature.faceIndices={1};feature.depth=1;
    feature.region=FaceRegion::Front;feature.length=40;feature.width=20;project->setFeatures({feature});
    window->setAttribute(Qt::WA_DontShowOnScreen);window->m_parameterDialog->setAttribute(Qt::WA_DontShowOnScreen);
    window->resize(1400,900);window->show();
    window->m_viewport->setMesh(mesh);
    bool originSeen=false;
    QTimer::singleShot(0,[&](){
        auto *setup=window->findChild<SetupOriginDialog*>();CHECK(setup);
        setup->m_anchorCombo->setCurrentIndex(setup->m_anchorCombo->findData(setupOriginAnchorId(SetupOriginAnchor::CustomPoint)));
        setup->m_customX->setValue(4);setup->m_customY->setValue(-2);setup->m_customZ->setValue(1);
        setup->m_offsetX->setValue(6);setup->m_offsetY->setValue(-3);setup->m_offsetZ->setValue(1);
        setup->setWorkOffset("G55");originSeen=true;static_cast<QDialog*>(setup)->accept();
    });
    window->onEditSetupOrigin();CHECK(originSeen);
    CHECK(project->setupOrigin().resolvedPoint(mesh.bbMin,mesh.bbMax)==QVector3D(10,-5,2));
    window->beginOperationWorkflow("mill_closed_contour");
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
    dialog->m_side->setCurrentIndex(int(ChainMachiningSide::Outside));
    QMetaObject::invokeMethod(dialog->m_side,"activated",Q_ARG(int,int(ChainMachiningSide::Outside)));
    dialog->accept();CHECK(window->m_parameterDialog->isVisible());
    CHECK(window->m_workflowFeatureIndex==0);
    auto establish=[&](){
        auto params=StrategyFactory::instance().strategy("mill_closed_contour")->defaultParams();
        params.set("depth",1);params.set("stepover",2);params.set("safeHeight",20);params.set("feedHeight",3);params.set("workOffset",55);
        window->m_strategyPanel->restoreDraft(params,mill.id);
        QTimer choose;QObject::connect(&choose,&QTimer::timeout,[&](){
            for(auto *widget:QApplication::topLevelWidgets())if(auto *choice=dynamic_cast<ContourMachiningChoiceDialog*>(widget)){
                choose.stop();
                choice->m_directionCombo->setCurrentIndex(choice->m_directionCombo->findData(int(ContourTraversalDirection::Reverse)));
                choice->m_startCombo->setCurrentIndex(choice->m_startCombo->findData(2));
                choice->m_compensationCombo->setCurrentIndex(choice->m_compensationCombo->findData(int(ContourCompensationChoice::CamOffsetG40)));
                choice->accept();return;
            }
        });choose.start(10);
        window->m_strategyPanel->onConfirmOperation();
    };
    establish();CHECK(project->operations().size()==1);
    const auto first=project->operations().first();CHECK(first.selectionEvidence.explicitUserSelection);
    const QVector<QVector3D> expected{{20,10,0},{20,-10,0},{-20,-10,0},{-20,10,0}};
    CHECK(first.contourFeature.points==expected);
    CHECK(first.selectionEvidence.machiningSide==ChainMachiningSide::Outside);
    const QString path=settings.filePath("contour-project.json");CHECK(project->saveToFile(path));
    CHECK(project->loadFromFile(path));project->setLoadedMesh(mesh);
    CHECK(project->lastEstablishedSelection()==first.selectionEvidence);
    CHECK(project->operations().first().contourFeature.points==expected);
    const auto confirmed=project->operations();
    window->m_operationPanel->generateProgramRequested(confirmed);
    CHECK(!project->programs().isEmpty());
    const auto actualProgram=project->programs().last();
    const auto actualNc=actualProgram.expandedGcodeText.isEmpty()?actualProgram.gcodeText:actualProgram.expandedGcodeText;
    CHECK(actualNc.contains("G55"));
    CHECK(actualNc.contains("X12.000 Y17.000"));
    CHECK(actualNc.contains("Z-3.000"));
    CHECK(actualNc.contains("G0 Z20.000"));
    CHECK(actualProgram.coordinateVersion==1 && actualProgram.modelOrigin==QVector3D(10,-5,2));
    CHECK(ProgramSnapshotStatus::evaluate(project->operations(),actualProgram.sourceOperationIds,
        actualProgram.sourceOperationFingerprint,project->setupFingerprint(),actualProgram.setupFingerprint,
        actualProgram.toolFingerprint,actualProgram.toolFingerprint,true).state==ProgramSnapshotState::Fresh);
    CHECK(project->saveToFile(path));CHECK(project->loadFromFile(path));project->setLoadedMesh(mesh);
    CHECK(project->programs().last().modelOrigin==QVector3D(10,-5,2));
    window->loadProgramById(actualProgram.id);
    CHECK(window->m_simCtrl->toolPath().contains(QVector3D(22,12,-1)));
    CHECK(window->m_viewport->m_toolPath==window->m_simViewport->m_toolPath);
    auto laterOrigin=project->setupOrigin();laterOrigin.offset={100,100,100};project->setSetupOrigin(laterOrigin);
    window->loadProgramById(actualProgram.id);
    CHECK(window->m_simCtrl->toolPath().contains(QVector3D(22,12,-1)));
    // External programs stay in their own NC coordinates, including after a CAM snapshot.
    const auto externalPath=settings.filePath("external.nc");
    {QFile external(externalPath);CHECK(external.open(QIODevice::WriteOnly));external.write("G90 G17\nG0 X1 Y2 Z10\nG1 Z9 F100\nM30\n");}
    QString error;CHECK(window->loadExternalGCodeFile(externalPath,&error));
    CHECK(window->m_simCtrl->toolPath().last()==QVector3D(1,2,9));
    window->loadProgramById(actualProgram.id);
    CHECK(window->m_simCtrl->toolPath().contains(QVector3D(22,12,-1)));
    // Legacy and malformed internal coordinate metadata cannot remain output-ready.
    QFile savedFile(path);CHECK(savedFile.open(QIODevice::ReadOnly));
    const auto originalJson=QJsonDocument::fromJson(savedFile.readAll()).object();savedFile.close();
    for(int invalid=0;invalid<4;++invalid){
        auto root=originalJson;auto programs=root["programs"].toArray();auto entry=programs[0].toObject();
        if(invalid==0){entry.remove("coordinateVersion");entry.remove("modelOrigin");}
        if(invalid==1)entry["coordinateVersion"]=99;
        if(invalid==2)entry["modelOrigin"]=QJsonArray{"bad",2,3};
        if(invalid==3)entry["modelOrigin"]=QJsonArray{999,2,3};
        programs[0]=entry;root["programs"]=programs;
        CHECK(savedFile.open(QIODevice::WriteOnly|QIODevice::Truncate));savedFile.write(QJsonDocument(root).toJson());savedFile.close();
        CHECK(project->loadFromFile(path));project->setLoadedMesh(mesh);
        CHECK(project->programs().first().coordinateVersion==0);
        window->loadProgramById(actualProgram.id);
        CHECK(!window->m_actExportGCode->isEnabled());
    }
    if(argc>1){QFile nc(QString::fromLocal8Bit(argv[1]));CHECK(nc.open(QIODevice::WriteOnly));nc.write(actualNc.toUtf8());}
    std::cout<<"PASS actual Setup XYZ offset and G55 change final cutting coordinates"<<std::endl;std::_Exit(0);
}
