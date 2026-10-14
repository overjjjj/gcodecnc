#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QElapsedTimer>
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
#include "../src/core/MachiningCoordinates.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/workspaces/process_design/selection/PocketOpening.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"
#include "../src/strategies/mill/PocketRoughingStrategy.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/workspaces/program_verification/ui/GCodeEditor.h"
#include "../src/gcode/ProgramPackageExporter.h"
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
class EscapingPocket : public PocketRoughingStrategy {
public:
 ToolpathResult generate(const ContourFeature &f,const ToolEntry &tool,const StrategyParams &params) const override {
  auto result=PocketRoughingStrategy::generate(f,tool,params);result.parametricProgram={};
  result.gcode+=QString("\nG0 Z50\nG0 X%1 Y%2\nG1 Z-1 F100\nG1 X%3 F100\nG0 Z50\n").arg(f.center.x()).arg(f.center.y()).arg(f.center.x()+1000);
  return result;
 }
};
class HiddenOutputWarning : public QObject {
public:
 QString lastMessage;
 bool eventFilter(QObject *obj,QEvent *event) override {
  auto *box=qobject_cast<QMessageBox *>(obj);
  if(box && event->type()==QEvent::Polish) {
   lastMessage=box->text();
   box->setAttribute(Qt::WA_DontShowOnScreen);
   QTimer::singleShot(0,box,[box]{std::cout<<"OUTPUT WARNING "<<box->text().toStdString()<<std::endl;box->accept();});
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
 StepImporter importer;check(importer.loadWithRotation(QString::fromLocal8Bit(argv[1]),QQuaternion()),"real STEP load with front-facing Setup rotation");
 const QString out=QString::fromLocal8Bit(argv[2]);QDir().mkpath(out);
 ToolEntry tool;tool.id=964;tool.type="end_mill";tool.diameter=6;tool.fluteLen=30;tool.totalLen=60;
 auto *w=new MainWindow;w->setAttribute(Qt::WA_DontShowOnScreen);w->m_parameterDialog->setAttribute(Qt::WA_DontShowOnScreen);w->resize(1500,950);w->show();
 ToolLibrary::instance().addTool(tool);
 auto *project=AppController::instance().projectManager();project->setLoadedMesh(importer.mesh());project->setFeatures(importer.features());project->setSourceFileFingerprint("real-feature-board-rotated-X180");
 w->m_activeRegion=FaceRegion::Front;project->setActiveRegion(FaceRegion::Front);emit w->activeRegionChanged(FaceRegion::Front);
 SetupOrigin origin;origin.confirmed=true;project->setSetupOrigin(origin);StockDefinition stock;stock.confirmed=true;project->setStockDefinition(stock);
 w->m_viewport->setMesh(importer.mesh());w->beginOperationWorkflow("mill_pocket_rough");
 auto *d=dynamic_cast<SelectionChainDialog *>(w->m_geometryDialog.data());check(d,"selection dialog");
 int candidate=-1,loop=-1;for(int i=0;i<d->m_candidates.size();++i)if(d->m_candidates[i].geometryId=="topology:face:113"){candidate=i;for(int j=0;j<d->m_candidates[i].boundaryLoops.size();++j){const auto &l=d->m_candidates[i].boundaryLoops[j];if(!l.outer&&l.orderedEdgeIds.contains("edge:452"))loop=j;}}
 check(candidate>=0&&loop>=0,"real face113 four-edge inner opening");
 auto mode=[&](ChainSelectionMode value){d->m_mode->setCurrentIndex(int(value));QMetaObject::invokeMethod(d->m_mode,"activated",Qt::DirectConnection,Q_ARG(int,int(value)));};
 mode(ChainSelectionMode::Edge);
 const auto ring=d->m_candidates[candidate].boundaryLoops[loop];
 auto pick=[&](const QString &id,int kind,int wantedLoop){
  const auto *edge=d->edgeForId(d->m_candidates[candidate],id);
  d->selectTopology(id,kind,edge?(edge->points.first()+edge->points.last())*.5f:QVector3D(),0);
  if(!d->m_pendingTopologyPick.isEmpty()){
   int match=-1;for(int k=0;k<d->m_pendingTopologyPick.matches.size();++k){const auto &m=d->m_pendingTopologyPick.matches[k];if(m.candidateIndex==candidate && m.loopIndex==wantedLoop)match=k;}
   check(match>=0,"actual topology ambiguity contains selected loop");d->m_pendingTopologyPick.matchIndex=match;d->confirmPendingTopologyMatch();
  }
 };
 pick(ring.orderedEdgeIds.first(),1,loop);pick(ring.orderedEdgeIds.first(),1,loop);
 check(d->m_controller.commit().orderedTopologyRefs.isEmpty() && d->m_controller.commit().boundaryLoops.isEmpty(),"removing last edge clears region proof");
 mode(ChainSelectionMode::Face);pick("face:113",0,-1);
 check(!d->acceptanceError(d->m_controller.commit(),d->selectedCandidateGeometryId()).isEmpty(),"whole multi-loop face cannot choose an arbitrary pocket");
 mode(ChainSelectionMode::Chain);pick(ring.orderedEdgeIds.first(),1,loop);d->useFullBoundary();
 check(d->m_controller.commit().orderedTopologyRefs.size()==ring.orderedEdgeIds.size(),"chain mode adopts exactly selected loop");
 const auto chained=d->m_controller.commit();mode(ChainSelectionMode::PartialChain);
 check(d->m_controller.commit().orderedTopologyRefs.isEmpty() && d->m_controller.commit().boundaryLoops.isEmpty(),"mode change clears old region");
 d->restoreHistory(false);check(d->m_controller.commit().boundaryLoops==chained.boundaryLoops,"undo mode change restores same region evidence");
 mode(ChainSelectionMode::Edge);for(const auto &id:ring.orderedEdgeIds)pick(id,1,loop);

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
 check((proof.machiningRole == MachiningBoundaryRole::Outer)&&proof.maximumDepth>20&&proof.maximumDepth<20.35,"opposite Setup has its own common transition depth");
 check(w->m_strategyPanel->parameterDraft().get("depth",-1)==0,"depth defaults to zero rather than an inferred machining depth");
 w->m_parameterDialog->grab().save(out+"/parameters-depth-required.png");
 auto params=w->m_strategyPanel->parameterDraft();params.set("depth",5);params.set("stepDown",2);params.set("entryMode",0);params.set("stockToLeave",.3);params.set("workOffset",54);
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
 const auto unshifted=generated;
 snapshot.modelOrigin={10,-5,2};
 generated=service.generate(project->operations(),post,options,snapshot);check(generated.ok,qPrintable(generated.errors.join("; ")));
 check(!generated.snapshot.expandedGcodeText.isEmpty() && generated.snapshot.gcodeText.contains("M98 P")
       && !generated.snapshot.parametricPrograms.isEmpty(),"real rounded pocket uses compact macro calls plus expanded final NC");
 const auto &oldCalls=unshifted.snapshot.parametricPrograms.first().calls;
 const auto &newCalls=generated.snapshot.parametricPrograms.first().calls;
 check(!oldCalls.isEmpty() && oldCalls.size()==newCalls.size(),"same pocket layers under translation");
 for(int i=0;i<oldCalls.size();++i)check(std::abs(oldCalls[i].arguments.value("DEPTH_Z").toDouble()
     -newCalls[i].arguments.value("DEPTH_Z").toDouble()-2)<1e-6,"macro depth arguments translate by origin Z");
 for(const auto &file:generated.snapshot.packageFiles){QFile saved(out+"/"+file.fileName);check(saved.open(QIODevice::WriteOnly),"save actual macro package");saved.write(file.content.toUtf8());}
 check(generated.snapshot.sourceOperationFingerprint==unshifted.snapshot.sourceOperationFingerprint,"origin does not replace source geometry fingerprint");
 SimulationController before,after;
 before.loadGCode(unshifted.snapshot.expandedGcodeText.isEmpty()?unshifted.snapshot.gcodeText:unshifted.snapshot.expandedGcodeText,false);
 after.loadGCode(generated.snapshot.expandedGcodeText.isEmpty()?generated.snapshot.gcodeText:generated.snapshot.expandedGcodeText,false);
 check(before.motionError().isEmpty()&&after.motionError().isEmpty()&&before.moves().size()==after.moves().size(),"rounded region final motion parses in both origins");
 bool translatedCut=false;
 for(int i=0;i<before.moves().size();++i)if(!before.moves()[i].isRapid && before.moves()[i].toKnownAxes==7){
  check((before.moves()[i].to-after.moves()[i].to-snapshot.modelOrigin).length()<.002,"every rounded region cutting endpoint translates exactly once");translatedCut=true;
 }
 check(translatedCut,"rounded region has actual cutting motion");
 HiddenOutputWarning outputWarning;app.installEventFilter(&outputWarning);
 QElapsedTimer screeningTimer;screeningTimer.start();
 const QString programId=project->upsertProgram(generated.snapshot);w->loadProgramById(programId);
 std::cout<<"ORIGINAL_18_RETRACT_SCREEN_MS="<<screeningTimer.elapsed()<<std::endl;
 std::cout<<"SCREEN "<<int(w->m_simCtrl->checkResult().state)<<" "<<w->m_simCtrl->checkResult().message.toStdString()<<std::endl;
 check(w->validateCurrentGCodeForOutput("export"),"real CQ8 feature-board macro passes output gate");
 const auto program=project->programById(programId);
 int retractCount=0;
 for(const auto &move:w->m_simCtrl->moves())if(move.fromKnownAxes==7&&move.toKnownAxes==7&&move.isRapid&&move.from.x()==move.to.x()&&move.from.y()==move.to.y()&&move.to.z()>move.from.z())++retractCount;
 std::cout<<"KNOWN_UPWARD_RETRACT_COUNT="<<retractCount<<std::endl;
 check(retractCount==18,"original three-layer fixture has exactly eighteen upward retracts");
 check(!w->m_simCtrl->checkResult().blocksOutput(),"all eighteen original retracts pass the actual final motion check");
 QTemporaryDir packageDir;
 const auto exported=ProgramPackageExporter::exportFiles(packageDir.path(),program.mainProgramFileName,program.packageFiles);
 check(exported.ok,qPrintable(exported.error));check(exported.writtenFiles.size()==program.packageFiles.size(),"actual macro package exported");
 for(const auto &file:program.packageFiles){QFile saved(packageDir.filePath(file.fileName));check(saved.open(QIODevice::ReadOnly)&&saved.readAll()==file.content.toUtf8(),"exported package bytes match verified files");}
 w->grab().save(out+"/cq8-output-screening.png");
 w->m_gcodeEditor->setGCode(program.gcodeText+"\n; edited compact program\n");w->syncCurrentProgramSnapshot();
 const auto changed=project->programById(programId);
 check(changed.gcodeText.contains("M98 P")&&changed.expandedGcodeText.isEmpty()&&changed.packageFiles.isEmpty(),"edited compact program drops expansion and package");
 check(!w->validateCurrentGCodeForOutput("export"),"edited compact macro blocks output");
 for(const QString &axis:{QString("A90"),QString("U10")}) {
  project->upsertProgram(program);w->loadProgramById(programId);
  QString editedAxes=program.expandedGcodeText;const int end=editedAxes.lastIndexOf("M30");check(end>=0,"program has end marker");editedAxes.insert(end,"G0 "+axis+"\n");
  w->m_gcodeEditor->setGCode(editedAxes);w->syncCurrentProgramSnapshot();
  SimulationController axisParser;axisParser.loadGCode(editedAxes);check(axisParser.motionError().contains("additional-axis"),"extra-axis rejection identifies unsupported motion");
  check(!w->validateCurrentGCodeForOutput("export"),"edited additional axis blocks output");
 }
 project->upsertProgram(program);w->loadProgramById(programId);
 app.removeEventFilter(&outputWarning);
 app.installEventFilter(&entryConfirmation);
 emit w->m_operationPanel->editOperationRequested(project->operations().first().id);
 auto *edit=dynamic_cast<OperationEditDialog *>(w->m_operationEditDialog.data());check(edit,"actual operation edit dialog");
 for(int row=0;row<edit->m_table->rowCount();++row)if(edit->m_table->item(row,0)->data(Qt::UserRole).toString()=="depth")edit->m_table->item(row,1)->setText("7");
 edit->accept();app.removeEventFilter(&entryConfirmation);
 check(project->operations().first().params.get("depth")==7,"edited parameter saved");
 check(project->operations().first().contourFeature.depth==7,"edited depth updates machining geometry");
 const auto recalculate=project->operations();emit w->m_operationPanel->recalculateRequested(recalculate);
 auto edited=service.generate(w->m_operationPanel->operations(),post,options,snapshot);check(edited.ok,qPrintable(edited.errors.join("; ")));
 check(edited.snapshot.gcodeText!=generated.snapshot.gcodeText,"legal edited depth changes final NC");
 const auto validOperations=w->m_operationPanel->operations();
 check(!service.generate({validOperations.first(),validOperations.first()},post,options,snapshot).ok,"duplicate operation IDs cannot bind two paths to one region");
 ProjectManager saved;saved.setSourceFileFingerprint(context.sourceFingerprint);saved.setOperations(validOperations);check(saved.saveToFile(out+"/rounded-roundtrip.cnext"),"save rounded operation");
 ProjectManager loaded;check(loaded.loadFromFile(out+"/rounded-roundtrip.cnext"),"reload rounded operation");
 check(loaded.operations().first().selectionEvidence==validOperations.first().selectionEvidence,"all original curves and region proof survive persistence");
 check(service.generate(loaded.operations(),post,options,snapshot).ok,"reloaded operation revalidates exact source and generates");
 auto stale=snapshot;stale.setupFingerprint+="changed";check(!service.generate(validOperations,post,options,stale).ok,"Setup change rejects rounded operation");
 stale=snapshot;stale.sourceFingerprint+="changed";check(!service.generate(validOperations,post,options,stale).ok,"source change rejects rounded operation");
 auto tampered=validOperations;tampered[0].selectionEvidence.boundaryLoops[0].curves[0].sweep+=.1;check(!service.generate(tampered,post,options,snapshot).ok,"curve evidence cannot be changed independently");
 auto wrongLoop=validOperations;for(const auto &other:importer.mesh().boundaryLoops)if(other.faceIndex==113 && other.orderedEdgeIds!=ring.orderedEdgeIds && !other.outer){wrongLoop[0].selectionEvidence.boundaryLoops[0].orderedEdgeIds=other.orderedEdgeIds;break;}
 check(!service.generate(wrongLoop,post,options,snapshot).ok,"same-face different loop cannot reuse selected region");
 OperationEditDialog excessive(validOperations.first());excessive.setAttribute(Qt::WA_DontShowOnScreen);
 for(int row=0;row<excessive.m_table->rowCount();++row)if(excessive.m_table->item(row,0)->data(Qt::UserRole).toString()=="depth")excessive.m_table->item(row,1)->setText("30");
 excessive.accept();check(excessive.result()!=QDialog::Accepted && !excessive.m_error->text().isEmpty(),"over-depth edit rejected before committing mismatched geometry");
 check(project->operations().first().params.get("depth")==7,"rejected edit preserves valid operation");
 QFile nc(out+"/rounded-depth7-expanded.nc");check(nc.open(QIODevice::WriteOnly),"write rounded final NC");nc.write((edited.snapshot.expandedGcodeText.isEmpty()?edited.snapshot.gcodeText:edited.snapshot.expandedGcodeText).toUtf8());nc.close();
 auto escaping=std::make_shared<EscapingPocket>();
 ProgramGenerationService badService([&](const QString &){return escaping;},[&](int){return tool;});
 check(!badService.generate(w->m_operationPanel->operations(),post,options,snapshot).ok,"whole final path rejects cutting outside region despite valid proof");
 std::cout<<"PASS rounded region maxDepth="<<proof.maximumDepth<<" curves="<<proof.curves.size()<<" unified depth edit"<<std::endl;std::_Exit(0);
}
