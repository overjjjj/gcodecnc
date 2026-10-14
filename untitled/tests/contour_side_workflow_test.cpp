#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QFile>
#include <QMessageBox>
#include <QTimer>
#include <iostream>
#include <cstdlib>
#include "OutputWorkflowTestSupport.h"
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/workspaces/process_design/ui/ContourMachiningChoiceDialog.h"
#undef private
#include "../src/app/AppController.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include "../src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"
#include "../src/gcode/ProgramSnapshotStatus.h"
#include "../src/strategies/StrategyFactory.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x << std::endl; std::_Exit(1); } } while(0)
int main(int argc,char **argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc,argv);
    OutputWorkflowDialogs dialogs;app.installEventFilter(&dialogs);
    QTimer::singleShot(240000,&app,[]{std::cerr<<"FAIL workflow timeout"<<std::endl;std::_Exit(99);});
    QTemporaryDir settings;
    isolateOutputWorkflowSettings(settings.path());
    const QString out=argc>1?QFileInfo(QString::fromLocal8Bit(argv[1])).absolutePath():settings.path();
    QDir().mkpath(out);dialogs.evidenceDirectory=out;
    ToolEntry mill;mill.id=903;mill.type="end_mill";mill.diameter=4;mill.fluteLen=12;mill.totalLen=50;
    auto *window=new MainWindow;
    ToolLibrary::instance().removeTool(mill.id);CHECK(ToolLibrary::instance().addTool(mill));
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
        params.set("depth",1);params.set("stepover",2);params.set("safeHeight",20);params.set("feedHeight",3);
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
    const int previousProgramCount=project->programs().size();
    CHECK(QMetaObject::invokeMethod(window->m_operationPanel,"onGenerateProgram",Qt::DirectConnection));
    CHECK(project->programs().size()==previousProgramCount+1);
    const auto program=project->programs().last();
    const QString finalNc=program.expandedGcodeText.isEmpty()?program.gcodeText:program.expandedGcodeText;
    CHECK(!finalNc.isEmpty());
    window->grab().save(out+"/before-export.png");
    CHECK(dialogs.exportProgram(window,program,out+"/initial-export"));
    SimulationController sim;sim.loadGCode(finalNc);CHECK(sim.motionError().isEmpty());
    bool depth=false;for(const auto &move:sim.moves())depth|=!move.isRapid && std::abs(move.to.z()+1)<0.0001;
    CHECK(depth);
    CHECK(finalNc.contains("X22.000 Y12.000"));
    CHECK(finalNc.contains("X22.000 Y-12.000"));
    auto changed=first;
    changed.selectionEvidence.machiningSide=ChainMachiningSide::Inside;
    CHECK(ProgramSnapshotFingerprint::calculate({first},{first.id}) !=
          ProgramSnapshotFingerprint::calculate({changed},{changed.id}));
    project->setOperations({changed});
    CHECK(dialogs.rejectExport(window,project,program,out+"/stale-export"));
    CHECK(project->saveToFile(path));CHECK(project->loadFromFile(path));
    const auto saved=project->programs().first();
    CHECK(ProgramSnapshotStatus::evaluate(project->operations(),saved.sourceOperationIds,
        saved.sourceOperationFingerprint).state==ProgramSnapshotState::Changed);
    project->setLoadedMesh(mesh);
    // Recalculation must enforce the same side contract as final generation.
    auto recalculated=first;
    recalculated.params.set("compensation",-1); // Wrong for this clockwise outside contour.
    project->setOperations({recalculated});
    const auto invalidOperations=project->operations();
    window->m_operationPanel->recalculateRequested(invalidOperations);
    CHECK(project->operations().first().toolpathState!=ToolpathState::Valid);
    recalculated.params.set("compensation",1);
    project->setOperations({recalculated});
    const auto validOperations=project->operations();
    window->m_operationPanel->recalculateRequested(validOperations);
    CHECK(project->operations().first().toolpathState==ToolpathState::Valid);
    CHECK(dialogs.warnings.size()==dialogs.expectedWarnings);
    std::cout<<"PASS synthetic contour UI confirmation, generation, actual export, snapshot persistence and stale-output refusal"<<std::endl;
    delete window;return 0;
}
