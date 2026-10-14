#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QMessageBox>
#include <QFile>
#include <QLabel>
#include <QTranslator>
#include <QAction>
#include <iostream>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/workspaces/program_verification/ui/GCodeEditor.h"
#include "../src/workspaces/shared/ui/BottomBar.h"
#undef private
#include "../src/app/AppController.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/core/Settings.h"
#include "../src/strategies/mill/BlindSlotMillingStrategy.h"
#include "../src/strategies/mill/PocketRoughingStrategy.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include "../src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"
#include "../src/workspaces/process_design/operations/MachiningInput.h"
#define CHECK(x) do { if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x<<std::endl;std::_Exit(1);} } while(0)

#include <QFileDialog>
#include <QAbstractButton>
#include <QDir>
#include <QFileInfo>
class ExportProbe : public QObject {
public:
 QString extension="nc",destination,evidence;bool cancel=false,allowOverwrite=false,removeDirectory=false;
 int choosers=0,warnings=0;
 bool eventFilter(QObject *object,QEvent *event) override {
  if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
   if(auto *dialog=qobject_cast<QDialog *>(object))dialog->setAttribute(Qt::WA_DontShowOnScreen);
  if(event->type()!=QEvent::Polish)return false;
  if(auto *file=qobject_cast<QFileDialog *>(object))QTimer::singleShot(0,file,[this,file]{
   ++choosers;
   CHECK(file->nameFilters().join(";").contains("*.txt"));CHECK(file->nameFilters().join(";").contains("*.nc"));
   CHECK(!IsWindowVisible(reinterpret_cast<HWND>(file->winId())));
   for(const auto &filter:file->nameFilters())if(filter.contains("*."+extension))file->selectNameFilter(filter);
   file->setDirectory(QFileInfo(destination).absolutePath());
   const QString typed = cancel || removeDirectory ? QFileInfo(destination).fileName()
       : QFileInfo(destination).completeBaseName() + (extension=="nc"?".wrong":"");
   file->selectFile(typed);
   file->grab().save(evidence+QString("/save-%1.png").arg(choosers));
   if(cancel){file->reject();return;}
   QMetaObject::invokeMethod(file,"accept");
   if(removeDirectory)CHECK(QDir().rmdir(QFileInfo(destination).absolutePath()));
  });
  else if(auto *box=qobject_cast<QMessageBox *>(object))QTimer::singleShot(0,box,[this,box]{
   CHECK(!IsWindowVisible(reinterpret_cast<HWND>(box->winId())));
   if(box->button(QMessageBox::Yes)) {
    box->grab().save(evidence+"/overwrite.png");
    box->button(allowOverwrite?QMessageBox::Yes:QMessageBox::No)->click();
   } else {++warnings;box->grab().save(evidence+QString("/blocked-%1.png").arg(warnings));box->accept();}
  });
  return false;
 }
};
int main(int argc,char **argv) {
 QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);QTemporaryDir temp;
 QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
 QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,temp.path());
 ExportProbe probe;const QString evidence=QString::fromLocal8Bit(argv[1]);probe.evidence=evidence;QDir().mkpath(evidence);app.installEventFilter(&probe);
 auto *w=new MainWindow;w->setAttribute(Qt::WA_DontShowOnScreen);w->resize(1400,950);w->show();
 auto *project=AppController::instance().projectManager();
 MeshData mesh;mesh.bbMin={-2,-2,-100};mesh.bbMax={2,2,-90};Triangle triangle;
 triangle.v0={-2,-2,-90};triangle.v1={2,-2,-90};triangle.v2={0,2,-90};mesh.triangles={triangle};
 project->setLoadedMesh(mesh);project->setActiveRegion(FaceRegion::Front);
 SetupOrigin origin;origin.confirmed=true;project->setSetupOrigin(origin);StockDefinition stock;stock.confirmed=true;project->setStockDefinition(stock);
 ToolEntry tool;tool.id=964;tool.type="end_mill";tool.diameter=6;tool.fluteLen=20;tool.totalLen=60;ToolLibrary::instance().addTool(tool);
 for(int mode=0;mode<3;++mode) {
  std::shared_ptr<StrategyBase> strategy=mode!=1 ? std::static_pointer_cast<StrategyBase>(std::make_shared<BlindSlotMillingStrategy>())
                                              : std::static_pointer_cast<StrategyBase>(std::make_shared<PocketRoughingStrategy>());
  ProgramGenerationService generator([=](const QString &id){return id==strategy->id()?strategy:std::shared_ptr<StrategyBase>();},
                                      [=](int){return tool;});
  MachiningOperation operation;operation.id="generated-binding";operation.opType=OperationType::Contour;
  operation.stage=OperationStage::RoughCut;operation.strategyId=strategy->id();operation.toolId=tool.id;
  operation.params=strategy->defaultParams();operation.contourFeature.subType=mode!=1?"blind_slot":"rectangular_pocket";
  operation.contourFeature.length=40;operation.contourFeature.width=mode!=1?12:20;operation.contourFeature.depth=4;
  operation.contourFeature.axis={0,0,1};operation.contourFeature.region=FaceRegion::Front;
  operation.params.set("depth",4);operation.params.set("slotLength",40);operation.params.set("slotWidth",12);
  operation.params.set("stockToLeave",1);operation.params.set("stepover",4);operation.params.set("entryMode",0);operation.params.set("helixRadius",0);
  operation.contourFeature=applyMachiningParameters(operation.contourFeature,operation.params);
  project->setOperations({operation});
  ProgramGenerationSnapshotOptions snapshot;snapshot.machineProfile=project->machineProfile();snapshot.setupFingerprint=project->setupFingerprint();
  PostProcessorOptions options;options.programNumber="O2401";options.workOffset="G54";
  options.safeStartBlocks=QStringList{"G17 G40 G49 G80","G21","G90","G54","G94"};
  Cq8PostProcessor processor;SiemensPostProcessor siemens;
  Settings::instance().setPostProcessorId(mode==2?"siemens":"cq8");
  auto generated=generator.generate(project->operations(),mode==2?static_cast<PostProcessorBase &>(siemens):processor,options,snapshot);
  CHECK(generated.ok);const auto original=generated.snapshot;CHECK(original.generationContentMatches());
  CHECK((mode==0)==original.packageFiles.isEmpty());
  project->upsertProgram(original);w->loadProgramById(original.id);
  CHECK(w->validateCurrentGCodeForOutput("export"));
  for(const auto &extension:QStringList{"nc","txt"}) {
   probe.extension=extension;probe.destination=evidence+QString("/mode-%1-%2/program.%2").arg(mode).arg(extension);
   QDir().mkpath(QFileInfo(probe.destination).absolutePath());probe.cancel=false;probe.allowOverwrite=false;
   const int before=probe.choosers;w->onExportGCode();CHECK(probe.choosers==before+1);
   QFile output(probe.destination);CHECK(output.open(QIODevice::ReadOnly));CHECK(output.readAll()==original.gcodeText.toUtf8());output.close();
   if(mode==1) {
    QFile macro(QFileInfo(probe.destination).absolutePath()+"/program_MACROS."+extension);
    CHECK(macro.open(QIODevice::ReadOnly));CHECK(macro.readAll()==original.packageFiles.last().content.toUtf8());
   }
   QFile sentinel(probe.destination);CHECK(sentinel.open(QIODevice::WriteOnly));sentinel.write("keep-existing");sentinel.close();
   w->onExportGCode();CHECK(sentinel.open(QIODevice::ReadOnly));CHECK(sentinel.readAll()=="keep-existing");sentinel.close();
   probe.allowOverwrite=true;w->onExportGCode();CHECK(sentinel.open(QIODevice::ReadOnly));CHECK(sentinel.readAll()==original.gcodeText.toUtf8());sentinel.close();
  }
  probe.cancel=true;probe.destination=evidence+QString("/cancel-%1.nc").arg(mode);w->onExportGCode();CHECK(!QFileInfo::exists(probe.destination));
  probe.cancel=false;probe.removeDirectory=true;probe.destination=evidence+QString("/removed-%1/program.nc").arg(mode);
  QDir().mkpath(QFileInfo(probe.destination).absolutePath());int warnings=probe.warnings;w->onExportGCode();
  CHECK(probe.warnings==warnings+1);CHECK(!QFileInfo::exists(probe.destination));probe.removeDirectory=false;
  auto stale=project->operations();stale[0].params.set("depth",3);project->setOperations(stale);
  int choosers=probe.choosers;warnings=probe.warnings;w->onExportGCode();CHECK(probe.choosers==choosers);CHECK(probe.warnings==warnings+1);
 }
 std::cout<<"PASS actual final export nc/txt, package, cancel, overwrite, write failure and stale rejection"<<std::endl;std::_Exit(0);
}
