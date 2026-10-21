#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QLabel>
#include <QSet>
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
#include "../src/tool/ToolLibrary.h"
#include "../src/workspaces/program_verification/ui/GCodeEditor.h"
#include "../src/gcode/ProgramPackageExporter.h"
static void check(bool ok,const char *message){if(!ok){std::cerr<<"FAIL "<<message<<std::endl;std::_Exit(1);}}
class HiddenRegressionDialogs : public QObject {
 bool eventFilter(QObject *object,QEvent *event) override {
  if(event->type()==QEvent::Polish)if(auto *dialog=qobject_cast<QDialog *>(object))dialog->setAttribute(Qt::WA_DontShowOnScreen);
  return false;
 }
};
int main(int argc,char **argv){
 QApplication app(argc,argv);HiddenRegressionDialogs hiddenDialogs;app.installEventFilter(&hiddenDialogs);check(argc==3,"model/output required");QTemporaryDir settings;
 QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());
 StepImporter importer;check(importer.loadWithRotation(QString::fromLocal8Bit(argv[1]),QQuaternion()),"real STEP load without rotation");
 const QString out=QString::fromLocal8Bit(argv[2]);QDir().mkpath(out);
 ToolEntry tool;tool.id=964;tool.type="end_mill";tool.diameter=6;tool.fluteLen=30;tool.totalLen=60;
 auto *w=new MainWindow;w->setAttribute(Qt::WA_DontShowOnScreen);w->m_parameterDialog->setAttribute(Qt::WA_DontShowOnScreen);w->resize(1500,950);w->show();
 ToolLibrary::instance().addTool(tool);
 auto *project=AppController::instance().projectManager();project->setLoadedMesh(importer.mesh());project->setFeatures(importer.features());project->setSourceFileFingerprint("real-feature-board-identity-selection");
 w->m_activeRegion=FaceRegion::Front;project->setActiveRegion(FaceRegion::Front);emit w->activeRegionChanged(FaceRegion::Front);
 SetupOrigin origin;origin.confirmed=true;project->setSetupOrigin(origin);StockDefinition stock;stock.confirmed=true;project->setStockDefinition(stock);
 w->m_viewport->setMesh(importer.mesh());
 for(int face:{340,344,366}) {
  w->beginOperationWorkflow("mill_sloped_floor");
  auto *slope=dynamic_cast<SelectionChainDialog *>(w->m_geometryDialog.data());
  check(slope && slope->m_mode->currentIndex()==int(ChainSelectionMode::Face),"sloped floor starts in Face mode");
  check(w->m_viewport->m_topologyPickMode==ViewportWidget::TopologyPickMode::Face,"viewport also picks faces for sloped floor");
  emit w->m_viewport->topologyPicked(QString("face:%1").arg(face),0,{},0);
  if(!slope->m_pendingTopologyPick.isEmpty()) {
   int found=-1;for(int k=0;k<slope->m_pendingTopologyPick.matches.size();++k)
    if(slope->m_candidates[slope->m_pendingTopologyPick.matches[k].candidateIndex].geometryId==QString("topology:face:%1").arg(face))found=k;
   check(found>=0,"real face choice present");slope->m_pendingTopologyPick.matchIndex=found;slope->confirmPendingTopologyMatch();
  }
  const auto chosen=slope->m_controller.commit();
  check(chosen.orderedTopologyRefs.size()==1 && chosen.orderedTopologyRefs.first().kind==SelectionTopologyKind::Face &&
        chosen.orderedTopologyRefs.first().id==QString("face:%1").arg(face),"actual viewport signal binds exactly selected face");
  const auto error=slope->acceptanceError(chosen,slope->selectedCandidateGeometryId());
  check(face==366 ? error.contains("SLOPED_FLOOR_PLANE_REQUIRED") : error.isEmpty(),"real valid floors accepted and nonplanar face rejected");
  slope->reject();QCoreApplication::processEvents();
 }
 w->beginOperationWorkflow("mill_pocket_rough");
 check(w->m_viewport->m_topologyPickMode==ViewportWidget::TopologyPickMode::Edge,"roughing still starts with edge picking");
 auto *d=dynamic_cast<SelectionChainDialog *>(w->m_geometryDialog.data());check(d,"selection dialog");
 int candidate=-1,loop=-1;for(int i=0;i<d->m_candidates.size();++i)if(d->m_candidates[i].geometryId=="topology:face:113"){candidate=i;for(int j=0;j<d->m_candidates[i].boundaryLoops.size();++j){const auto &l=d->m_candidates[i].boundaryLoops[j];if(!l.outer&&l.orderedEdgeIds.contains("edge:452"))loop=j;}}
 check(candidate>=0&&loop>=0,"real face113 inner opening");
 auto mode=[&](ChainSelectionMode value){d->m_mode->setCurrentIndex(int(value));QMetaObject::invokeMethod(d->m_mode,"activated",Qt::DirectConnection,Q_ARG(int,int(value)));};

 d->selectGeometry("topology:face:113");
 mode(ChainSelectionMode::Chain);
 int checksFailed=0;
 auto verify=[&](bool ok,const char *msg){std::cout<<(ok?"PASS ":"FAIL ")<<msg<<std::endl;if(!ok)++checksFailed;};
 QVector<QVector3D> hover;
 const auto originalHover=d->topologyHoverChanged;
 d->topologyHoverChanged=[&](const QVector<QVector3D> &p){hover=p;if(originalHover)originalHover(p);};
 int acceptanceCalls=0;
 const auto originalAcceptance=d->acceptanceError;
 d->acceptanceError=[&](const SelectionChain &selection,const QString &id){++acceptanceCalls;return originalAcceptance(selection,id);};
 int rings=0;
 for(int j=0;j<d->m_candidates[candidate].boundaryLoops.size();++j){
  const auto &ring=d->m_candidates[candidate].boundaryLoops[j];
  if(ring.outer || !QStringList({"edge:452","edge:460","edge:484","edge:494","edge:505","edge:514"}).contains(ring.orderedEdgeIds.first()))continue;
  ++rings;
  const auto before=d->m_controller.commit().orderedTopologyRefs;
  SelectionChain expected;check(d->buildLoopDraft(j,0,1,ring.orderedEdgeIds.size(),&expected),"real loop draft");
  const int callsBefore=acceptanceCalls;
  d->previewTopology(ring.orderedEdgeIds.first(),1,{},0);
  verify(hover==d->orderedPoints(expected),"Chain hover previews exactly local real loop");
  d->previewTopology({},1,{},0);
  verify(acceptanceCalls==callsBefore,"hover and leave do not call geometry acceptance");
  verify(d->m_controller.commit().orderedTopologyRefs==before,"hover never commits loop");
  d->selectTopology(ring.orderedEdgeIds.first(),1,{},0);
  if(!d->m_pendingTopologyPick.isEmpty()){
   for(int k=0;k<d->m_pendingTopologyPick.matches.size();++k)
    if(d->m_pendingTopologyPick.matches[k].candidateIndex==candidate && d->m_pendingTopologyPick.matches[k].loopIndex==j)d->m_pendingTopologyPick.matchIndex=k;
   d->confirmPendingTopologyMatch();
  }
  verify(d->m_topologyPreviewKind==SelectionChainDialog::TopologyPreviewKind::FullBoundary &&
         d->m_topologyPreviewDraft.boundaryLoops.size()==1 &&
         d->m_topologyPreviewDraft.boundaryLoops.first().orderedEdgeIds==ring.orderedEdgeIds,
         "explicit switching keeps exactly requested loop preview");
 }
 verify(rings==6,"all six known real slot loops exercised");
 const auto beforeFace=d->m_controller.commit().orderedTopologyRefs;
 d->selectTopology("face:113",0,{},0);
 verify(!d->m_pendingTopologyPick.isEmpty(),"Chain face click offers explicit loop choices");
 if(!d->m_pendingTopologyPick.isEmpty()){
  verify(!d->m_nextButton->isEnabled(),"unconfirmed loop choice cannot advance");
  QSet<QString> labels;
  for(int k=0;k<d->m_topologyContext->count();++k)labels.insert(d->m_topologyContext->itemText(k));
  verify(labels.size()==d->m_topologyContext->count(),"same-face loop labels are distinct");
  verify(d->m_controller.commit().orderedTopologyRefs==beforeFace,"loop choices do not silently commit first loop");
  for(int k=0;k<d->m_pendingTopologyPick.matches.size();++k){
   const auto &match=d->m_pendingTopologyPick.matches[k];
   if(match.candidateIndex==candidate && match.loopIndex==loop)d->m_pendingTopologyPick.matchIndex=k;
  }
  d->previewPendingTopologyMatch();d->confirmPendingTopologyMatch();
  verify(d->m_topologyPreviewKind==SelectionChainDialog::TopologyPreviewKind::FullBoundary &&
    d->m_topologyPreviewDraft.boundaryLoops.first().orderedEdgeIds==d->m_candidates[candidate].boundaryLoops[loop].orderedEdgeIds,
    "explicit face loop confirmation offers only chosen boundary");
 }

 w->grab().save(out+"/six-loops.png");
 if(checksFailed){std::cout<<"FAILURES "<<checksFailed<<std::endl;std::_Exit(1);}
 d->acceptanceError=originalAcceptance;
 d->topologyHoverChanged=originalHover;
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

 const auto draft=d->m_controller.commit();check(draft.closed&&draft.freeEndCount==0,"original edges form closed chain");check(draft.boundaryLoops.size()==1&&!draft.boundaryLoops.first().outer,"source inner identity retained");
 check(d->m_controller.reverse(),"reverse selected opening");
 SelectionChainController reversed=d->m_controller;
 check(evaluateSelectionAccess(reversed.commit(),project->sourceFileFingerprint(),project->setupFingerprint(),"G54",true).ok(),"reverse retains machining role and complete topology");
 check(d->m_controller.undo(),"undo reverse");

 mode(ChainSelectionMode::Chain);pick(ring.orderedEdgeIds.first(),1,loop);d->useFullBoundary();
 auto *validation=d->findChild<QLabel *>("selectionValidationStatus");
 check(validation && validation->text().contains(QStringLiteral("粗加工：选区检查通过")) &&
       validation->text().contains(QStringLiteral("侧壁：未检查")) &&
       validation->text().contains(QStringLiteral("底面：未检查")) &&
       validation->text().contains(QStringLiteral("最终 NC：未检查")),
       "only current rough operation is checked; other operations and NC stay unchecked");
 const int callsBeforeSelectedHover=acceptanceCalls;
 d->acceptanceError=[&](const SelectionChain &selection,const QString &id){++acceptanceCalls;return originalAcceptance(selection,id);};
 d->previewTopology(ring.orderedEdgeIds.first(),1,{},0);d->previewTopology({},1,{},0);
 check(acceptanceCalls==callsBeforeSelectedHover,"verified selection hover never repeats geometry acceptance");
 d->selectTopology("face:113",0,{},0);
 check(!d->m_pendingTopologyPick.isEmpty(),"verified region can preview another loop without committing");
 check(!d->m_chainStatus->text().contains(QStringLiteral("下一步：加工参数")),"initial pending choice replaces stale next-step text");

 const auto pendingOriginalRefs=d->m_controller.commit().orderedTopologyRefs;
 auto *reverseButton=d->findChild<QPushButton *>("selectionReverse");check(reverseButton,"reverse control");
 reverseButton->click();
 verify(!d->m_pendingTopologyPick.isEmpty() && !d->m_nextButton->isEnabled(),"reverse refresh cannot enable pending selection");
 verify(!d->m_chainStatus->text().contains(QStringLiteral("下一步：加工参数")),"pending status does not claim machining parameters are next");
 d->restoreHistory(false);
 verify(!d->m_pendingTopologyPick.isEmpty() && !d->m_nextButton->isEnabled(),"undo restores pending with Next disabled");
 verify(d->m_controller.commit().orderedTopologyRefs==pendingOriginalRefs,"undo preserves original adopted boundary");
 d->m_side->setCurrentIndex(0);QMetaObject::invokeMethod(d->m_side,"activated",Qt::DirectConnection,Q_ARG(int,0));
 verify(!d->m_pendingTopologyPick.isEmpty() && !d->m_nextButton->isEnabled(),"machining side refresh cannot enable pending selection");
 d->restoreHistory(false);
 for(int k=0;k<d->m_pendingTopologyPick.matches.size();++k)
  if(d->m_pendingTopologyPick.matches[k].candidateIndex==candidate && d->m_pendingTopologyPick.matches[k].loopIndex==loop)d->m_pendingTopologyPick.matchIndex=k;
 d->confirmPendingTopologyMatch();d->useFullBoundary();
 verify(d->m_pendingTopologyPick.isEmpty() && d->m_nextButton->isEnabled(),"explicit confirmation restores normal advancement");
 if(checksFailed){std::cout<<"FAILURES "<<checksFailed<<std::endl;std::_Exit(1);}
 d->selectTopology("face:113",0,{},0);
 check(validation->text().contains(QStringLiteral("粗加工：待确认")) &&
       !validation->text().contains(QStringLiteral("检查通过")),"pending candidate does not inherit old geometry approval");
 const QString pendingFaceId=d->m_pendingTopologyPick.id;
 bool testedVertex=false;
 for(const auto &other:d->m_candidates){
  if(other.geometryId=="topology:face:113")continue;
  for(const auto &vertex:other.topologyVertices){
   if(d->vertexForId(d->m_candidates[candidate],vertex.id) || d->topologyMatches(vertex.id,2).size()!=1)continue;
   d->selectTopology(vertex.id,2,vertex.point,0);
   check(d->m_pendingTopologyPick.id==pendingFaceId,"rejected foreign vertex preserves pending loop choice");testedVertex=true;break;
  }
  if(testedVertex)break;
 }
 check(testedVertex,"real foreign vertex exercised");
 d->confirmPendingTopologyMatch();d->useFullBoundary();
 const auto verified=d->m_controller.commit();
 project->setSourceFileFingerprint("changed-real-model-version");
 check(!originalAcceptance(verified,d->selectedCandidateGeometryId()).isEmpty(),"old selection rejected after source version changes");
 d->accept();check(validation->text().contains(QStringLiteral("粗加工：选区检查未通过")) &&
                  d->result()!=QDialog::Accepted,"source rejection clears previously passed display");
 project->setSourceFileFingerprint("real-feature-board-identity-selection");
 SetupOrigin changedOrigin=origin;changedOrigin.confirmed=false;project->setSetupOrigin(changedOrigin);
 check(!originalAcceptance(verified,d->selectedCandidateGeometryId()).isEmpty(),"old selection rejected after Setup changes");
 d->accept();check(validation->text().contains(QStringLiteral("粗加工：选区检查未通过")) &&
                  d->result()!=QDialog::Accepted,"Setup rejection cannot retain a passed display");
 project->setSetupOrigin(origin);
 d->acceptanceError=originalAcceptance;
 d->selectTopology("face:113",0,{},0);
 for(int k=0;k<d->m_pendingTopologyPick.matches.size();++k)
  if(d->m_pendingTopologyPick.matches[k].candidateIndex==candidate && d->m_pendingTopologyPick.matches[k].loopIndex==loop){
   d->m_pendingTopologyPick.matchIndex=k;d->m_topologyContext->setCurrentIndex(k);break;
  }
 d->previewPendingTopologyMatch();
 w->grab().save(out+"/explicit-loop-choice.png");
 std::cout << "PASS real face113 explicit loop, Edge, Chain, Partial, reverse and undo checks" << std::endl;
 settings.remove(); std::_Exit(0);
}
