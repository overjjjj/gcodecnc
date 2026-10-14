#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QEvent>
#include <iostream>
#include <cstdlib>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/workspaces/process_design/ui/OperationEditDialog.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#undef private
#include "../src/app/AppController.h"
#include "../src/workspaces/process_design/selection/PocketOpening.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"
#include "../src/strategies/mill/PocketRoughingStrategy.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/tool/ToolLibrary.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <STEPControl_Writer.hxx>
static void check(bool ok,const char *message){if(!ok){std::cerr<<"FAIL "<<message<<std::endl;std::_Exit(1);}}
class HiddenEntryConfirmation : public QObject {
 bool eventFilter(QObject *obj,QEvent *event) override {
  if(qobject_cast<QDialog *>(obj) && event->type()==QEvent::Polish) static_cast<QWidget *>(obj)->setAttribute(Qt::WA_DontShowOnScreen);
  auto *box=qobject_cast<QMessageBox *>(obj);
  if(box && event->type()==QEvent::Polish) {
   box->setAttribute(Qt::WA_DontShowOnScreen);
   QTimer::singleShot(0,box,[box]{check(!IsWindowVisible(reinterpret_cast<HWND>(box->winId())),"entry confirmation stays hidden");check(box->standardButtons().testFlag(QMessageBox::Yes),qPrintable(box->text()));box->button(QMessageBox::Yes)->click();});
  }
  return false;
 }
};
int main(int argc,char **argv){
 QApplication app(argc,argv);check(argc==3,"model/output required");QTemporaryDir settings;
 QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());
 StepImporter importer;check(importer.loadWithRotation(QString::fromLocal8Bit(argv[1]),QQuaternion::fromAxisAndAngle(1,0,0,180)),"real STEP load with front-facing Setup rotation");
 const QString out=QString::fromLocal8Bit(argv[2]);QDir().mkpath(out);
 ToolEntry tool;tool.id=964;tool.type="end_mill";tool.diameter=6;tool.fluteLen=30;tool.totalLen=60;ToolLibrary::instance().addTool(tool);
 auto *w=new MainWindow;w->setAttribute(Qt::WA_DontShowOnScreen);w->m_parameterDialog->setAttribute(Qt::WA_DontShowOnScreen);w->resize(1500,950);w->show();
 auto *project=AppController::instance().projectManager();project->setLoadedMesh(importer.mesh());project->setFeatures(importer.features());project->setSourceFileFingerprint("real-feature-board-rotated-X180");
 w->m_activeRegion=FaceRegion::Front;project->setActiveRegion(FaceRegion::Front);emit w->activeRegionChanged(FaceRegion::Front);
 SetupOrigin origin;origin.confirmed=true;project->setSetupOrigin(origin);StockDefinition stock;stock.confirmed=true;project->setStockDefinition(stock);
 w->m_viewport->setMesh(importer.mesh());w->beginOperationWorkflow("mill_pocket_rough");
 auto *d=dynamic_cast<SelectionChainDialog *>(w->m_geometryDialog.data());check(d,"selection dialog");
 int candidate=-1,loop=-1;for(int i=0;i<d->m_candidates.size();++i)if(d->m_candidates[i].geometryId=="topology:face:98"){candidate=i;for(int j=0;j<d->m_candidates[i].boundaryLoops.size();++j){const auto &l=d->m_candidates[i].boundaryLoops[j];if(!l.outer&&l.orderedEdgeIds.size()==4)loop=j;}}
 check(candidate>=0&&loop>=0,"real face98 four-edge inner opening");
 d->m_mode->setCurrentIndex(int(ChainSelectionMode::Edge));
 const auto ring=d->m_candidates[candidate].boundaryLoops[loop];
 for(const auto &id:ring.orderedEdgeIds){const auto *edge=d->edgeForId(d->m_candidates[candidate],id);check(edge,"real edge");d->selectTopologyMatch(id,1,(edge->points.first()+edge->points.last())*.5f,0,{candidate,loop});}
 const auto draft=d->m_controller.commit();check(draft.closed&&draft.freeEndCount==0,"four original edges form closed chain");check(draft.boundaryLoops.size()==1&&!draft.boundaryLoops.first().outer,"source inner identity retained");
 check(d->m_controller.reverse(),"reverse selected opening");
 SelectionChainController reversed=d->m_controller;
 check(evaluateSelectionAccess(reversed.commit(),project->sourceFileFingerprint(),project->setupFingerprint(),"G54",true).ok(),"reverse retains machining role and complete topology");
 check(d->m_controller.undo(),"undo reverse");
 d->accept();app.processEvents();
 check(!IsWindowVisible(reinterpret_cast<HWND>(w->winId()))&&!IsWindowVisible(reinterpret_cast<HWND>(w->m_parameterDialog->winId())),"no visible window or input use");
 w->grab().save(out+"/selection.png");std::cout<<"STATUS "<<d->m_status->text().toStdString()<<std::endl;
 check(w->m_parameterDialog->isVisible(),"verified closed slot opening reaches machining parameters");
 check(w->m_workflowSelection.explicitUserSelection&&!w->m_workflowSelection.boundaryLoops.first().outer,"accepted source topology preserved");
 const auto &proof=w->m_workflowSelection.boundaryLoops.first();
 check((proof.machiningRole == MachiningBoundaryRole::Outer)&&proof.maximumDepth>16&&proof.maximumDepth<16.27,"sloping floor certifies only common shallow depth");
 check(proof.supportFaceIds.contains("face:376")&&proof.supportFaceIds.contains("face:372"),"real common floor and sloping wall provenance");
 check(w->m_strategyPanel->parameterDraft().get("depth",-1)==0,"depth defaults to zero rather than an inferred machining depth");
 w->m_parameterDialog->grab().save(out+"/parameters-depth-required.png");
 auto params=w->m_strategyPanel->parameterDraft();params.set("depth",5);params.set("entryMode",0);params.set("stockToLeave",.3);params.set("workOffset",54);
 w->m_strategyPanel->restoreDraft(params,tool.id);
 OperationProposal proposal;proposal.kind=OperationProposalKind::Contour;proposal.strategyId="mill_pocket_rough";proposal.toolId=tool.id;
 proposal.params=w->m_strategyPanel->parameterDraft();proposal.contourFeature=w->m_strategyPanel->currentContourFeature();proposal.selectionEvidence=w->m_workflowSelection;
 HiddenEntryConfirmation entryConfirmation;app.installEventFilter(&entryConfirmation);
 check(w->m_strategyPanel->m_confirmOperationBtn->isEnabled(),"actual confirm button enabled after explicit parameters and Setup");
 w->m_strategyPanel->m_confirmOperationBtn->click();app.removeEventFilter(&entryConfirmation);
 check(project->operations().size()==1&&!w->m_parameterDialog->isVisible(),"actual confirmation adds one operation and closes parameters");
 check(project->operations().first().contourFeature.points==proof.machiningBoundary,"actual UI operation consumes selected boundary");
 w->grab().save(out+"/confirmed-operation.png");
 OperationConfirmationContext context{project->sourceFileFingerprint(),project->setupFingerprint(),"G54",&project->mesh()};
 auto confirmed=OperationFactory::confirm(proposal,OperationConfirmationIntent::ExplicitUser,context);check(confirmed.ok,qPrintable(confirmed.error));
 auto strategy=std::make_shared<PocketRoughingStrategy>();
 ProgramGenerationService service([&](const QString &){return strategy;},[&](int){return tool;});
 ProgramGenerationSnapshotOptions snapshot;snapshot.sourceFingerprint=context.sourceFingerprint;snapshot.setupFingerprint=context.setupFingerprint;snapshot.sourceMesh=&project->mesh();
 Cq8PostProcessor post;PostProcessorOptions options;options.workOffset="G54";options.safeStartBlocks=QStringList{"G17 G40 G49 G80","G21","G90","G54","G94"};
 auto generated=service.generate(project->operations(),post,options,snapshot);check(generated.ok,qPrintable(generated.errors.join("; ")));
 app.installEventFilter(&entryConfirmation);
 emit w->m_operationPanel->editOperationRequested(project->operations().first().id);
 auto *edit=dynamic_cast<OperationEditDialog *>(w->m_operationEditDialog.data());check(edit,"actual operation edit dialog");
 for(int row=0;row<edit->m_table->rowCount();++row)if(edit->m_table->item(row,0)->data(Qt::UserRole).toString()=="depth")edit->m_table->item(row,1)->setText("7");
 edit->accept();app.removeEventFilter(&entryConfirmation);
 check(project->operations().first().params.get("depth")==7,"edited parameter saved");
 check(project->operations().first().contourFeature.depth==7,"edited depth updates machining geometry");
 emit w->m_operationPanel->recalculateRequested(project->operations());
 auto edited=service.generate(w->m_operationPanel->operations(),post,options,snapshot);check(edited.ok,qPrintable(edited.errors.join("; ")));
 check(edited.snapshot.gcodeText!=generated.snapshot.gcodeText,"legal edited depth changes final NC");
 std::cout<<"PASS unified depth edit"<<std::endl;std::_Exit(0);
}
