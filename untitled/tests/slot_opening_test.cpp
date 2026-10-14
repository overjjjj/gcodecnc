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
  auto *box=qobject_cast<QMessageBox *>(obj);
  if(box && event->type()==QEvent::Polish) {
   box->setAttribute(Qt::WA_DontShowOnScreen);
   QTimer::singleShot(0,box,[box]{check(!IsWindowVisible(reinterpret_cast<HWND>(box->winId())),"entry confirmation stays hidden");check(box->standardButtons().testFlag(QMessageBox::Yes),qPrintable(box->text()));box->button(QMessageBox::Yes)->click();});
  }
  return false;
 }
};
class HiddenRegressionDialogs : public QObject {
 bool eventFilter(QObject *object,QEvent *event) override {
  if(event->type()==QEvent::Polish)if(auto *dialog=qobject_cast<QDialog *>(object))dialog->setAttribute(Qt::WA_DontShowOnScreen);
  return false;
 }
};
int main(int argc,char **argv){
 QApplication app(argc,argv);HiddenRegressionDialogs hiddenDialogs;app.installEventFilter(&hiddenDialogs);check(argc==3,"model/output required");QTemporaryDir settings;
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
 QString code=generated.snapshot.expandedGcodeText.isEmpty()?generated.snapshot.gcodeText:generated.snapshot.expandedGcodeText;
 QFile nc(out+"/real-slot-depth5-expanded.nc");check(nc.open(QIODevice::WriteOnly),"NC evidence");nc.write(code.toUtf8());nc.close();
 SimulationController sim;sim.loadGCode(code);int cuts=0;
 float minX=1e9,maxX=-1e9,minY=1e9,maxY=-1e9;for(const auto &p:proof.machiningBoundary){minX=qMin(minX,p.x());maxX=qMax(maxX,p.x());minY=qMin(minY,p.y());maxY=qMax(maxY,p.y());}
 for(const auto &move:sim.moves())if(!move.isRapid){++cuts;for(const auto &p:{move.from,move.to}){
  check(p.x()>=minX+3.3-.002&&p.x()<=maxX-3.3+.002&&p.y()>=minY+3.3-.002&&p.y()<=maxY-3.3+.002,"cutter plus allowance remains inside selected opening");
  check(p.z()>=proposal.contourFeature.center.z()-5-.002,"final NC respects explicit depth");
 }}check(cuts>0,"actual generated cutting moves");
 auto expectReject=[&](OperationProposal bad,const char *label){check(!OperationFactory::confirm(bad,OperationConfirmationIntent::ExplicitUser,context).ok,label);auto op=confirmed.operation;op.params=bad.params;op.contourFeature=bad.contourFeature;op.selectionEvidence=bad.selectionEvidence;check(!service.generate({op},post,options,snapshot).ok,label);};
 auto bad=proposal;bad.params.set("depth",20);bad.contourFeature.depth=20;expectReject(bad,"deeper slope cannot be cut as a flat prism");
 bad=proposal;bad.selectionEvidence.boundaryLoops[0].maximumDepth=100;bad.params.set("depth",20);bad.contourFeature.depth=20;expectReject(bad,"persisted depth proof rechecked against source");
 bad=proposal;bad.selectionEvidence.boundaryLoops[0].supportFaceIds[0]="face:9999";expectReject(bad,"persisted support proof rechecked against source");
 bad=proposal;bad.params.set("depth",0);bad.contourFeature.depth=0;expectReject(bad,"explicit positive depth required");
 bad=proposal;bad.contourFeature.center.setZ(-10);expectReject(bad,"changed base plane cannot bypass depth bound");
 bad=proposal;bad.params.set("stockToLeave",-1);expectReject(bad,"negative allowance cannot enlarge region");
 bad=proposal;bad.contourFeature.points[0].setX(minX-20);expectReject(bad,"expanded region rejected");
 bad=proposal;bad.selectionEvidence.sourceFingerprint="stale";expectReject(bad,"stale model rejected");
 bad=proposal;bad.selectionEvidence.orderedTopologyRefs.removeLast();expectReject(bad,"open or incomplete chain rejected despite closed flag");
 bad=proposal;std::swap(bad.selectionEvidence.orderedTopologyRefs[1],bad.selectionEvidence.orderedTopologyRefs[2]);expectReject(bad,"crossed order rejected");
 bad=proposal;bad.selectionEvidence.selectedSurfaceNormal={1,0,0};expectReject(bad,"wrong machining plane rejected");
 ProjectManager saved;saved.setSourceFileFingerprint(context.sourceFingerprint);saved.setOperations({confirmed.operation});check(saved.saveToFile(out+"/roundtrip.cnext"),"save evidence");
 ProjectManager loaded;check(loaded.loadFromFile(out+"/roundtrip.cnext"),"reload evidence");check(loaded.operations().size()==1&&loaded.operations().first().selectionEvidence==confirmed.operation.selectionEvidence,"role source loops bounds and support survive save/load");
 auto modified=confirmed.operation;modified.selectionEvidence.boundaryLoops[0].maximumDepth+=1;
 check(ProgramSnapshotFingerprint::calculate({modified},{modified.id})!=ProgramSnapshotFingerprint::calculate({confirmed.operation},{confirmed.operation.id}),"depth proof changes snapshot fingerprint");
 SelectionBoundaryLoop ignored;MachiningFeature ignoredFeature;
 auto wrongRing=ring;std::swap(wrongRing.orderedEdgeIds[1],wrongRing.orderedEdgeIds[2]);check(!verifyPocketOpening(importer.mesh(),wrongRing,&ignored,&ignoredFeature),"self-crossing source order not certified");
 auto wrongMesh=importer.mesh();for(auto &t:wrongMesh.triangles)if(t.faceIndex==98)t.normal={1,0,0};check(!verifyPocketOpening(wrongMesh,ring,&ignored,&ignoredFeature),"wrong source plane not certified");
 const auto box=BRepPrimAPI_MakeBox(40,30,12).Shape();const auto cavity=BRepAlgoAPI_Cut(box,BRepPrimAPI_MakeBox(gp_Pnt(5,5,4),30,20,10).Shape()).Shape();
 const auto island=BRepAlgoAPI_Fuse(cavity,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20,15,4),gp_Dir(0,0,1)),3,8).Shape()).Shape();
 int fixtureIndex=0;for(const auto &shape:{cavity,island}){const QString file=out+QString("/occ-pocket-%1.step").arg(fixtureIndex);STEPControl_Writer writer;writer.Transfer(shape,STEPControl_AsIs);check(writer.Write(file.toLocal8Bit().constData())==IFSelect_RetDone,"write independent OCC fixture");StepImporter fixture;check(fixture.load(file),"read OCC fixture");bool accepted=false;for(const auto &l:fixture.mesh().boundaryLoops)accepted|=verifyPocketOpening(fixture.mesh(),l,&ignored,&ignoredFeature);check(accepted==(fixtureIndex==0),"plain cavity accepted but actual OCC island retained and rejected");++fixtureIndex;}
 w->m_parameterDialog->grab().save(out+"/parameters-depth5.png");
 std::cout<<"PASS real face98 selection -> parameters -> explicit operation -> final NC; maxDepth="<<proof.maximumDepth<<" cuts="<<cuts<<"; reverse/undo, persistence, island/plane/chain/stale/depth/allowance guards"<<std::endl;std::_Exit(0);
}

