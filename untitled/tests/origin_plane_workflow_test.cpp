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
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QTabWidget>
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
#include "../src/workspaces/process_design/ui/OperationEditDialog.h"
#include "../src/workspaces/process_design/ui/OperationBatchEditDialog.h"
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
        if(event->type()==QEvent::Polish && qobject_cast<QDialog*>(object))
            static_cast<QWidget*>(object)->setAttribute(Qt::WA_DontShowOnScreen);
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
    MeshData mesh; mesh.bbMin={-20,-10,90};mesh.bbMax={20,10,100};
    QVector<QVector3D> corners={{-20,-10,100},{20,-10,100},{20,10,100},{-20,10,100}};
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
    MachiningFeature feature;feature.kind=FeatureKind::FlatSurface;feature.subType="flat_surface";feature.axis={0,0,1};feature.boundaryPoints=corners;feature.faceIndices={1};feature.depth=1;feature.center={0,0,100};feature.planarBoundaryVerified=true;
    feature.region=FaceRegion::Front;feature.length=40;feature.width=20;project->setFeatures({feature});
    window->setAttribute(Qt::WA_DontShowOnScreen);window->m_parameterDialog->setAttribute(Qt::WA_DontShowOnScreen);
    window->resize(1400,900);window->show();
    window->m_viewport->setMesh(mesh);
    bool originSeen=false;
    QTimer::singleShot(0,[&](){
        auto *setup=window->findChild<SetupOriginDialog*>();CHECK(setup);
        setup->m_anchorCombo->setCurrentIndex(setup->m_anchorCombo->findData(setupOriginAnchorId(SetupOriginAnchor::CustomPoint)));
        setup->m_customX->setValue(4);setup->m_customY->setValue(-2);setup->m_customZ->setValue(99);
        setup->m_offsetX->setValue(6);setup->m_offsetY->setValue(-3);setup->m_offsetZ->setValue(1);
        setup->setWorkOffset("G55");originSeen=true;
        if(argc>1){app.processEvents();CHECK(setup->grab().save(QString::fromLocal8Bit(argv[1])+".origin.png"));}
        static_cast<QDialog*>(setup)->accept();
    });
    window->onEditSetupOrigin();CHECK(originSeen);
    CHECK(project->setupOrigin().resolvedPoint(mesh.bbMin,mesh.bbMax)==QVector3D(10,-5,100));
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
    dialog->m_side->setCurrentIndex(int(ChainMachiningSide::Outside));
    QMetaObject::invokeMethod(dialog->m_side,"activated",Q_ARG(int,int(ChainMachiningSide::Outside)));
    dialog->accept();CHECK(window->m_parameterDialog->isVisible());
    CHECK(window->m_workflowFeatureIndex==0);
    auto establish=[&](){
        auto params=StrategyFactory::instance().strategy("mill_face")->defaultParams();
        params.set("depth",1);params.set("stepover",2);params.set("safeHeight",50);params.set("feedHeight",5);params.set("workOffset",55);
        window->m_strategyPanel->restoreDraft(params,mill.id);
        window->m_strategyPanel->onConfirmOperation();
    };
    establish();CHECK(project->operations().size()==1);
    const auto first=project->operations().first();CHECK(first.selectionEvidence.explicitUserSelection);
    const auto expected=first.contourFeature.points;
    CHECK(first.contourFeature.points==expected);
    CHECK(first.selectionEvidence.machiningSide==ChainMachiningSide::Outside);
    const QString path=settings.filePath("contour-project.json");CHECK(project->saveToFile(path));
    CHECK(project->loadFromFile(path));project->setLoadedMesh(mesh);
    CHECK(project->lastEstablishedSelection()==first.selectionEvidence);
    CHECK(project->operations().first().contourFeature.points==expected);
    const auto confirmed=project->operations();
    window->m_operationPanel->generateProgramRequested(confirmed);
    CHECK(!project->programs().isEmpty());
    CHECK(first.contourFeature.center.z()==100);
    const auto program=project->programs().last();
    CHECK(program.gcodeText.contains("Z-1.000"));
    // Single and batch editors must validate absolute Z against work top, not model top.
    window->m_operationPanel->editOperationRequested(first.id);
    auto *edit=dynamic_cast<OperationEditDialog*>(window->m_operationEditDialog.data());CHECK(edit);
    if(argc>1){edit->findChild<QTabWidget*>()->setCurrentIndex(3);app.processEvents();CHECK(edit->grab().save(QString::fromLocal8Bit(argv[1])));}
    edit->findChild<QDoubleSpinBox*>("operationParameter_safeHeight")->setValue(4);edit->accept();
    CHECK(edit->result()!=QDialog::Accepted && !edit->m_error->text().isEmpty());
    edit->findChild<QDoubleSpinBox*>("operationParameter_safeHeight")->setValue(60);edit->accept();
    CHECK(project->operations().first().params.get("safeHeight")==60);
    OperationBatchEditDialog batch(project->operations());batch.setAttribute(Qt::WA_DontShowOnScreen);
    batch.findChild<QCheckBox*>("batchParameterSelect_safeHeight")->setChecked(true);
    batch.findChild<QLineEdit*>("batchParameterValue_safeHeight")->setText("4");batch.accept();
    CHECK(batch.result()!=QDialog::Accepted);
    batch.findChild<QLineEdit*>("batchParameterValue_safeHeight")->setText("60");batch.accept();
    CHECK(batch.result()==QDialog::Accepted);
    auto invalid=project->operations();invalid[0].params.set("safeHeight",4);project->setOperations(invalid);
    window->m_operationPanel->recalculateRequested(invalid);
    CHECK(project->operations().first().toolpathState!=ToolpathState::Valid);
    const auto valid=batch.operations();project->setOperations(valid);
    window->m_operationPanel->recalculateRequested(valid);
    CHECK(project->operations().first().toolpathState==ToolpathState::Valid);
    std::cout<<"PASS nonzero top planar UI confirmation, single/batch editing and actual recalculation"<<std::endl;
    std::_Exit(0);
}
