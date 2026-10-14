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
#include <QFileDialog>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include "../src/core/Settings.h"
#include "../src/core/SourceFileFingerprint.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x<<std::endl;std::_Exit(1);}}while(0)
class HiddenDialogs : public QObject {
public:
 QString destination;int chooserCount=0;QStringList warnings;
 bool eventFilter(QObject *obj,QEvent *event) override {
  if(event->type()!=QEvent::Polish)return false;
  if(auto *dialog=qobject_cast<QDialog *>(obj))dialog->setAttribute(Qt::WA_DontShowOnScreen);
  if(auto *file=qobject_cast<QFileDialog *>(obj)){
   ++chooserCount;CHECK(!destination.isEmpty());
   QTimer::singleShot(0,file,[this,file]{file->setDirectory(QFileInfo(destination).absolutePath());file->selectFile(destination);std::cout<<"EXPORT "<<destination.toStdString()<<std::endl;QMetaObject::invokeMethod(file,"accept");});
  }else if(auto *box=qobject_cast<QMessageBox *>(obj)){
   QTimer::singleShot(0,box,[this,box]{CHECK(!IsWindowVisible(reinterpret_cast<HWND>(box->winId())));
    if(auto *yes=box->button(QMessageBox::Yes))yes->click();else{warnings<<box->text();std::cout<<"WARNING "<<box->text().toStdString()<<std::endl;box->accept();}});
  }
  return false;
 }
};
int main(int argc,char **argv) {
 QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);CHECK(argc==3);
 QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());
 HiddenDialogs dialogs;app.installEventFilter(&dialogs);
 const QString sourcePath=QString::fromLocal8Bit(argv[1]),out=QString::fromLocal8Bit(argv[2]);CHECK(QDir().mkpath(out));
 StepImporter importer;CHECK(importer.loadWithRotation(sourcePath,QQuaternion()));
 ToolEntry tool;tool.id=964;tool.type="end_mill";tool.diameter=2;tool.fluteLen=30;tool.totalLen=60;ToolLibrary::instance().removeTool(tool.id);ToolLibrary::instance().addTool(tool);
 auto *w=new MainWindow;w->setAttribute(Qt::WA_DontShowOnScreen);w->resize(1500,950);w->show();
 auto *project=AppController::instance().projectManager();project->setLoadedMesh(importer.mesh());project->setFeatures(importer.features());
 project->setSourceFilePath(sourcePath);project->setSourceFileFingerprint(SourceFileFingerprint::calculate(sourcePath));project->setSetupRotation(QQuaternion());
 w->m_activeRegion=FaceRegion::Front;project->setActiveRegion(FaceRegion::Front);emit w->activeRegionChanged(FaceRegion::Front);
 SetupOrigin origin;origin.confirmed=true;project->setSetupOrigin(origin);StockDefinition stock;stock.confirmed=true;project->setStockDefinition(stock);
 Settings::instance().setPostProcessorId("cq8");w->m_viewport->setMesh(importer.mesh());
 SelectionChain obsolete;
 auto selectFresh=[&](const char *label) {
  QElapsedTimer timer;timer.start();w->beginOperationWorkflow("mill_pocket_rough");
  std::cout<<"SELECTION_OPEN_MS "<<label<<" "<<timer.elapsed()<<std::endl;
  auto *d=dynamic_cast<SelectionChainDialog *>(w->m_geometryDialog.data());CHECK(d);
  if(!obsolete.boundaryLoops.isEmpty()) {
   d->setPreviousSelection(obsolete);d->m_lastSelection->click();
   CHECK(d->m_controller.commit().boundaryLoops.isEmpty());CHECK(d->validatePreviousSelection(obsolete)<0);
  }
  int candidate=-1,loop=-1;
  for(int i=0;i<d->m_candidates.size();++i)if(d->m_candidates[i].geometryId=="topology:face:3")
   for(int j=0;j<d->m_candidates[i].boundaryLoops.size();++j)
    if(!d->m_candidates[i].boundaryLoops[j].outer && d->m_candidates[i].boundaryLoops[j].orderedEdgeIds.contains("edge:10")){candidate=i;loop=j;}
  CHECK(candidate>=0&&loop>=0);const auto ring=d->m_candidates[candidate].boundaryLoops[loop];
  d->m_mode->setCurrentIndex(int(ChainSelectionMode::Chain));QMetaObject::invokeMethod(d->m_mode,"activated",Qt::DirectConnection,Q_ARG(int,int(ChainSelectionMode::Chain)));
  const auto *edge=d->edgeForId(d->m_candidates[candidate],ring.orderedEdgeIds.first());CHECK(edge);
  d->selectTopology(edge->id,1,(edge->points.first()+edge->points.last())*.5f,0);
  if(!d->m_pendingTopologyPick.isEmpty()) {
   int match=-1;for(int k=0;k<d->m_pendingTopologyPick.matches.size();++k){const auto &m=d->m_pendingTopologyPick.matches[k];if(m.candidateIndex==candidate&&m.loopIndex==loop)match=k;}
   CHECK(match>=0);d->m_pendingTopologyPick.matchIndex=match;d->confirmPendingTopologyMatch();
  }
  d->useFullBoundary();d->accept();app.processEvents();CHECK(w->m_parameterDialog->isVisible());
  const auto &proof=w->m_workflowSelection.boundaryLoops.first();CHECK(openingEvidenceSupported(proof));
  CHECK(proof.maximumDepth>8.99 && proof.maximumDepth<9);CHECK(proof.candidateRule=="source-polygon-inset-candidate-v1");
  bool spline=false;for(const auto &curve:proof.curves)spline|=curve.type=="bspline";CHECK(spline);
  auto params=w->m_strategyPanel->parameterDraft();params.set("depth",2);params.set("entryMode",0);params.set("stockToLeave",.3);params.set("workOffset",54);
  w->m_strategyPanel->restoreDraft(params,tool.id);CHECK(w->m_strategyPanel->m_confirmOperationBtn->isEnabled());
  w->m_strategyPanel->m_confirmOperationBtn->click();CHECK(!w->m_parameterDialog->isVisible());
 };
 selectFresh("first");CHECK(project->operations().size()==1);
 CHECK(!IsWindowVisible(reinterpret_cast<HWND>(w->winId())));
 auto exportProgram=[&](const ProgramEntry &program,const QString &stem){
  const QString directory=out+"/"+stem;QDir().mkpath(directory);dialogs.destination=program.packageFiles.isEmpty()?directory+"/program.nc":directory;
  const int before=dialogs.chooserCount;w->onExportGCode();CHECK(dialogs.chooserCount==before+1);
  if(program.packageFiles.isEmpty()){QFile f(dialogs.destination);CHECK(f.open(QIODevice::ReadOnly|QIODevice::Text));CHECK(QString::fromUtf8(f.readAll())==program.gcodeText);}
  else for(const auto &part:program.packageFiles){QFile f(directory+"/"+part.fileName);CHECK(f.open(QIODevice::ReadOnly));CHECK(f.readAll()==part.content.toUtf8());}
  SimulationController simulation;simulation.loadGCode(program.expandedGcodeText.isEmpty()?program.gcodeText:program.expandedGcodeText,false);CHECK(simulation.motionError().isEmpty());CHECK(!simulation.moves().isEmpty());
  double lowest=std::numeric_limits<double>::infinity();for(const auto &move:simulation.moves())if(!move.isRapid&&move.toKnownAxes==7)lowest=std::min(lowest,double((move.to+program.modelOrigin).z()));
  const auto &op=project->operations().first();CHECK(std::abs(lowest-(op.contourFeature.center.z()-op.params.get("depth")))<.002);
 };
 w->m_operationPanel->onGenerateProgram();CHECK(!project->programs().isEmpty());const auto program=project->programById(w->m_currentProgramId);exportProgram(program,"initial");
 const QString savedPath=out+"/mixed-roundtrip.cnext";CHECK(project->saveToFile(savedPath));const auto selection=project->operations().first().selectionEvidence;
 CHECK(AppController::instance().loadProject(savedPath));CHECK(!project->mesh().sourceShape.IsNull());CHECK(project->operations().first().selectionEvidence==selection);
 w->m_operationPanel->setOperations(project->operations());w->loadProgramById(program.id);exportProgram(project->programById(program.id),"reloaded");
 emit w->m_operationPanel->editOperationRequested(project->operations().first().id);auto *edit=dynamic_cast<OperationEditDialog *>(w->m_operationEditDialog.data());CHECK(edit);
 for(int row=0;row<edit->m_table->rowCount();++row)if(edit->m_table->item(row,0)->data(Qt::UserRole).toString()=="depth")edit->m_table->item(row,1)->setText("3");
 edit->accept();CHECK(edit->result()==QDialog::Accepted);const int beforeStale=dialogs.chooserCount;w->onExportGCode();CHECK(dialogs.chooserCount==beforeStale);
 emit w->m_operationPanel->recalculateRequested(project->operations());w->m_operationPanel->onGenerateProgram();const auto changed=project->programById(w->m_currentProgramId);
 CHECK(changed.gcodeText!=program.gcodeText);exportProgram(changed,"edited");
 auto strategy=std::make_shared<PocketRoughingStrategy>();
 ProgramGenerationService service([&](const QString &){return strategy;},[&](int){return tool;});
 ProgramGenerationSnapshotOptions snapshot;snapshot.sourceFingerprint=project->sourceFileFingerprint();snapshot.setupFingerprint=project->setupFingerprint();snapshot.sourceMesh=&project->mesh();
 Cq8PostProcessor post;PostProcessorOptions options;options.workOffset="G54";
 const auto valid=project->operations();
 for(int mutation=0;mutation<3;++mutation) {
  auto tampered=valid;auto &proof=tampered[0].selectionEvidence.boundaryLoops[0];
  if(mutation==0)proof.volumeProofRule="unknown";
  if(mutation==1)proof.curves[0].approximationBound+=.000001;
  if(mutation==2)proof.machiningBoundary[0].setX(proof.machiningBoundary[0].x()+1);
  CHECK(!service.generate(tampered,post,options,snapshot).ok);
  OperationProposal proposal;proposal.kind=OperationProposalKind::Contour;proposal.strategyId=tampered[0].strategyId;proposal.toolId=tool.id;
  proposal.params=tampered[0].params;proposal.contourFeature=tampered[0].contourFeature;proposal.selectionEvidence=tampered[0].selectionEvidence;
  OperationConfirmationContext context{snapshot.sourceFingerprint,snapshot.setupFingerprint,"G54",&project->mesh()};
  CHECK(!OperationFactory::confirm(proposal,OperationConfirmationIntent::ExplicitUser,context).ok);
 }
 auto helical=valid;helical[0].params.set("entryMode",1);helical[0].params.set("helixRadius",1);helical[0].params.set("helixPitch",.5);
 const auto unsupportedEntry=service.generate(helical,post,options,snapshot);CHECK(!unsupportedEntry.ok);
 CHECK(unsupportedEntry.errors.join(";").contains("Irregular pocket clearing currently supports confirmed vertical entry only."));
 // Persist a genuinely generated Valid operation, then remove its new geometry contract.
 CHECK(project->saveToFile(savedPath));QFile sourceFile(savedPath);CHECK(sourceFile.open(QIODevice::ReadOnly));auto root=QJsonDocument::fromJson(sourceFile.readAll()).object();sourceFile.close();
 auto operations=root["operations"].toArray();auto operation=operations[0].toObject();auto evidence=operation["selectionEvidence"].toObject();auto loops=evidence["boundaryLoops"].toArray();
 for(int i=0;i<loops.size();++i){auto loop=loops[i].toObject();for(const auto &key:QStringList{"discretizationRule","candidateRule","volumeProofRule","sourceBoundaryBound","consumedBoundaryBound","volumeProofBudget","maximumEscapeBudget"})loop.remove(key);loops[i]=loop;}
 evidence["boundaryLoops"]=loops;operation["selectionEvidence"]=evidence;operations[0]=operation;root["operations"]=operations;
 const QString legacyPath=out+"/legacy-missing-evidence.cnext";QFile legacy(legacyPath);CHECK(legacy.open(QIODevice::WriteOnly));legacy.write(QJsonDocument(root).toJson());legacy.close();
 CHECK(AppController::instance().loadProject(legacyPath));w->m_operationPanel->setOperations(project->operations());w->loadProgramById(changed.id);
 CHECK(project->operations().first().toolpathState==ToolpathState::Valid);const auto old=project->operations().first();obsolete=old.selectionEvidence;
 CHECK(!operationEditError(old,old.params,old.toolId).isEmpty());const int beforeLegacy=dialogs.chooserCount;w->onExportGCode();CHECK(dialogs.chooserCount==beforeLegacy);
 const int programCount=project->programs().size();w->m_operationPanel->onGenerateProgram();CHECK(project->programs().size()==programCount);
 // Explicitly replace the obsolete operation with a newly selected and confirmed operation.
 project->setOperations({});w->m_operationPanel->setOperations({});selectFresh("reselected");CHECK(project->operations().size()==1);
 w->m_operationPanel->onGenerateProgram();exportProgram(project->programById(w->m_currentProgramId),"recovered");
 w->grab().save(out+"/mixed-workflow.png");std::cout<<"FAILURES=0"<<std::endl;std::_Exit(0);
}
