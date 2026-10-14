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

 QElapsedTimer timer;timer.start();w->beginOperationWorkflow("mill_pocket_rough");
 std::cout<<"OPEN_MS "<<timer.elapsed()<<std::endl;
 auto *d=dynamic_cast<SelectionChainDialog *>(w->m_geometryDialog.data());CHECK(d);
 auto evidenceCount=[&](){int count=0;for(const auto &candidate:d->m_candidates)count+=candidate.machiningBoundaries.size()+candidate.regionErrors.size();return count;};
 CHECK(evidenceCount()==0);
 int candidate=-1,loop=-1;
 for(int i=0;i<d->m_candidates.size();++i)if(d->m_candidates[i].geometryId=="topology:face:3")
  for(int j=0;j<d->m_candidates[i].boundaryLoops.size();++j)if(d->m_candidates[i].boundaryLoops[j].orderedEdgeIds.contains("edge:10")){candidate=i;loop=j;}
 CHECK(candidate>=0 && loop>=0);const auto ring=d->m_candidates[candidate].boundaryLoops[loop];
 d->m_mode->setCurrentIndex(int(ChainSelectionMode::Chain));QMetaObject::invokeMethod(d->m_mode,"activated",Qt::DirectConnection,Q_ARG(int,int(ChainSelectionMode::Chain)));
 const auto *edge=d->edgeForId(d->m_candidates[candidate],ring.orderedEdgeIds.first());CHECK(edge);
 d->selectTopology(edge->id,1,(edge->points.first()+edge->points.last())*.5f,0);
 if(!d->m_pendingTopologyPick.isEmpty()) {
  int match=-1;for(int k=0;k<d->m_pendingTopologyPick.matches.size();++k){const auto &m=d->m_pendingTopologyPick.matches[k];if(m.candidateIndex==candidate && m.loopIndex==loop)match=k;}
  CHECK(match>=0);d->m_pendingTopologyPick.matchIndex=match;d->confirmPendingTopologyMatch();
 }
 CHECK(evidenceCount()==0);CHECK(d->m_controller.commit().orderedTopologyRefs.isEmpty());
 timer.restart();d->useFullBoundary();std::cout<<"ADOPT_MS "<<timer.elapsed()<<std::endl;
 for(const auto &item:d->m_candidates)if(!item.machiningBoundaries.isEmpty() || !item.regionErrors.isEmpty()) {
  std::cout<<"EVIDENCE "<<item.geometryId.toStdString()<<" proofs="<<item.machiningBoundaries.size()<<" errors="<<item.regionErrors.size()<<std::endl;
  for(auto it=item.regionErrors.cbegin();it!=item.regionErrors.cend();++it)std::cout<<"REGION_ERROR "<<it.key().toStdString()<<" "<<it.value().toStdString()<<std::endl;
 }
 CHECK(evidenceCount()==1);auto proof=d->m_controller.commit();
 CHECK(proof.boundaryLoops.size()==1 && openingEvidenceSupported(proof.boundaryLoops.first()));
 CHECK(d->m_nextButton->isEnabled());d->accept();app.processEvents();CHECK(w->m_parameterDialog->isVisible());
 proof=d->selection();CHECK(!proof.candidateGeometryId.isEmpty());
 w->m_parameterDialog->reject();app.processEvents();
 w->beginOperationWorkflow("mill_pocket_rough");d=dynamic_cast<SelectionChainDialog *>(w->m_geometryDialog.data());CHECK(d && evidenceCount()==0);
 d->setPreviousSelection(proof);d->m_lastSelection->click();
 CHECK(evidenceCount()==1);CHECK(d->m_controller.commit().boundaryLoops==proof.boundaryLoops);
 d->accept();app.processEvents();CHECK(w->m_parameterDialog->isVisible());
 w->m_parameterDialog->reject();app.processEvents();
 w->beginOperationWorkflow("mill_pocket_rough");d=dynamic_cast<SelectionChainDialog *>(w->m_geometryDialog.data());CHECK(d && evidenceCount()==0);
 d->m_mode->setCurrentIndex(int(ChainSelectionMode::Edge));QMetaObject::invokeMethod(d->m_mode,"activated",Qt::DirectConnection,Q_ARG(int,int(ChainSelectionMode::Edge)));
 for(int i=0;i<ring.orderedEdgeIds.size();++i) {
  const auto *part=d->edgeForId(d->m_candidates[candidate],ring.orderedEdgeIds[i]);CHECK(part);
  const bool forward=ring.orderedForward[i];
  d->selectTopology(part->id,1,forward?part->points.first():part->points.last(),forward?part->parameters.first():part->parameters.last());
  if(!d->m_pendingTopologyPick.isEmpty()) {
   int match=-1;for(int k=0;k<d->m_pendingTopologyPick.matches.size();++k){const auto &m=d->m_pendingTopologyPick.matches[k];if(m.candidateIndex==candidate && m.loopIndex==loop)match=k;}
   CHECK(match>=0);d->m_pendingTopologyPick.matchIndex=match;d->confirmPendingTopologyMatch();
  }
  auto observedController=d->m_controller;const auto observed=observedController.commit();
  std::cout<<"EDGE_STEP "<<i<<" evidence="<<evidenceCount()<<" refs="<<observed.orderedTopologyRefs.size()<<" closed="<<observed.closed<<" status="<<d->m_status->text().toStdString()<<std::endl;
  CHECK(evidenceCount()==(i+1==ring.orderedEdgeIds.size()?1:0));
  if(i+1<ring.orderedEdgeIds.size())CHECK(!d->m_nextButton->isEnabled());
 }
 CHECK(d->m_nextButton->isEnabled());d->accept();app.processEvents();CHECK(w->m_parameterDialog->isVisible());
 proof=d->selection();CHECK(!proof.candidateGeometryId.isEmpty());
 w->m_parameterDialog->reject();app.processEvents();
 w->beginOperationWorkflow("mill_pocket_rough");d=dynamic_cast<SelectionChainDialog *>(w->m_geometryDialog.data());CHECK(d && evidenceCount()==0);
 project->setSourceFileFingerprint("changed-source");
 d->setPreviousSelection(proof);d->m_lastSelection->click();
 CHECK(evidenceCount()==1);CHECK(d->m_controller.commit().orderedTopologyRefs.isEmpty());
 CHECK(!d->m_nextButton->isEnabled());d->accept();app.processEvents();CHECK(!w->m_parameterDialog->isVisible());
 std::cout<<"FAILURES=0"<<std::endl;std::_Exit(0);
}
