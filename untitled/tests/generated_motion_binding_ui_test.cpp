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
#include "../src/strategies/mill/BlindSlotMillingStrategy.h"
#include "../src/strategies/mill/PocketRoughingStrategy.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include "../src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"
#include "../src/workspaces/process_design/operations/MachiningInput.h"
#define CHECK(x) do { if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x<<std::endl;std::_Exit(1);} } while(0)
class HiddenWarnings : public QObject {
 bool eventFilter(QObject *object,QEvent *event) override {
  if(auto *box=qobject_cast<QMessageBox *>(object)) if(event->type()==QEvent::Polish){
   box->setAttribute(Qt::WA_DontShowOnScreen);
   QTimer::singleShot(0,box,[box]{std::cout<<box->text().toStdString()<<std::endl;box->accept();});
  }
  return false;
 }
};
int main(int argc,char **argv){
 QApplication app(argc,argv);QTemporaryDir temp;
 QTranslator translations;if(argc>2){CHECK(translations.load(QString::fromLocal8Bit(argv[2])));app.installTranslator(&translations);}
 QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
 QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,temp.path());
 HiddenWarnings warnings;app.installEventFilter(&warnings);
 auto *w=new MainWindow;w->setAttribute(Qt::WA_DontShowOnScreen);w->resize(1600,1000);w->show();
 CHECK(!IsWindowVisible(reinterpret_cast<HWND>(w->winId())));
 auto *project=AppController::instance().projectManager();
 MeshData mesh;mesh.bbMin={-2,-2,-1};mesh.bbMax={2,2,10};
 Triangle triangle;triangle.v0={-2,-2,10};triangle.v1={2,-2,10};triangle.v2={0,2,10};mesh.triangles={triangle};
 project->setLoadedMesh(mesh);project->setActiveRegion(FaceRegion::Front);
 SetupOrigin origin;origin.confirmed=true;project->setSetupOrigin(origin);
 StockDefinition stock;stock.confirmed=true;project->setStockDefinition(stock);
 ToolEntry tool;tool.id=963;tool.diameter=2;ToolLibrary::instance().addTool(tool);
 const QString header="G17 G40 G49 G80\nG21\nG90\nG54\nT963 M6\nS2000 M3\nG0 Z50\nG0 X-100 Y0\n";
 const QString footer="G0 Z50\nM5\nM9\nM30\n";
 auto add=[&](const QString &body){ProgramEntry p;p.name="check fixture";p.gcodeText=header+body+footer;p.coordinateVersion=1;
  p.setupFingerprint=project->setupFingerprint();p.coordinateEvidence=p.coordinateFingerprint();return project->upsertProgram(p);};
 const QString bad=add("G0 Z9\nG0 X100\n");
 const QString good=add("G1 Z40 F100\nG1 X100\n");
 w->loadProgramById(bad);CHECK(w->m_simCtrl->checkResult().state==SimulationCheckState::Collision);
 CHECK(!w->validateCurrentGCodeForOutput("export"));
 w->onSimPlay();CHECK(w->m_bottomBar->m_label->text().contains(w->m_simCtrl->checkResult().message));
 w->onSimPause();CHECK(w->m_bottomBar->m_label->text().contains(w->m_simCtrl->checkResult().message));
 w->onSimStop();CHECK(w->m_bottomBar->m_label->text().contains(w->m_simCtrl->checkResult().message));
 w->m_simCtrl->simulationFinished();
 CHECK(w->m_simulationStateBadge->property("reviewState")=="blocked");
 CHECK(w->m_bottomBar->m_label->text().contains(w->m_simCtrl->checkResult().message.simplified()));
 if(argc>1)w->grab().save(QString::fromLocal8Bit(argv[1])+"/collision.png");
 w->loadProgramById(good);CHECK(w->m_simCtrl->checkResult().state==SimulationCheckState::Limited);
 CHECK(w->validateCurrentGCodeForOutput("export"));
 // Export recomputes the current program, independent of a different player result.
 w->m_simCtrl->loadGCode(project->programById(bad).gcodeText);
 CHECK(w->m_simCtrl->checkResult().state==SimulationCheckState::Collision);
 CHECK(w->validateCurrentGCodeForOutput("export"));
 w->loadProgramById(bad);w->m_simCtrl->loadGCode(project->programById(good).gcodeText);
 CHECK(!w->validateCurrentGCodeForOutput("export"));
 mesh.triangles.fill(triangle,13202);project->setLoadedMesh(mesh);w->loadProgramById(bad);
 CHECK(w->m_simCtrl->checkResult().state==SimulationCheckState::Incomplete);
 w->m_simCtrl->simulationFinished();CHECK(w->m_simulationStateBadge->property("reviewState")=="warning");
 CHECK(!w->m_bottomBar->m_label->text().contains('\n'));
 CHECK(w->m_bottomBar->m_label->toolTip().contains(w->m_simCtrl->checkResult().message));
 CHECK(!w->validateCurrentGCodeForOutput("export"));
 if(argc>1)w->grab().save(QString::fromLocal8Bit(argv[1])+"/budget-incomplete.png");
 // Loop disclaimers cannot survive replacement by unmatched editor text.
 w->loadProgramById(good);w->m_simCtrl->loadGCode(header+"G73 X0 Y0 Z5 R20 Q1 F100\nG80\n"+footer);
 w->updateProgramReviewSummary();CHECK(!w->m_simulationStateBadge->text().contains("G73"));
 const QString external=temp.filePath("external.nc");{QFile file(external);CHECK(file.open(QIODevice::WriteOnly));file.write("G90 G17\nG0 Z50\nG0 X0 Y0\nG1 X10 F100\nM30\n");}
 QString error;CHECK(w->loadExternalGCodeFile(external,&error));
 CHECK(w->m_simCtrl->checkResult().state==SimulationCheckState::Incomplete);
 w->onSimPlay();CHECK(w->m_simCtrl->isRunning());w->onSimStop();
 CHECK(!w->validateCurrentGCodeForOutput("export"));
 w->loadProgramById(good);CHECK(w->m_simCtrl->checkResult().state==SimulationCheckState::Limited);
 CHECK(w->validateCurrentGCodeForOutput("export"));
 // A stale expanded preview must never stand in for different output text.
 auto forged=project->programById(bad);forged.expandedGcodeText=project->programById(good).gcodeText;
 project->upsertProgram(forged);w->loadProgramById(forged.id);
 CHECK(!w->validateCurrentGCodeForOutput("export"));
 auto package=project->programById(good);ProgramFileEntry file;file.kind="main";file.fileName="main.nc";file.content=project->programById(bad).gcodeText;
 package.mainProgramFileName=file.fileName;package.packageFiles={file};project->upsertProgram(package);w->loadProgramById(package.id);
 w->updateProgramActionAvailability();CHECK(!w->m_actExportGCode->isEnabled());CHECK(!w->m_actSendToMachine->isEnabled());
 CHECK(w->m_outputReadinessBadge->property("reviewState")=="blocked");
 CHECK(w->m_programSnapshotBadge->property("reviewState")=="blocked");
 CHECK(!w->validateCurrentGCodeForOutput("export"));
 // Actual generation and output gate, both plain and packaged CQ8 programs.
 mesh.triangles={triangle};mesh.bbMin={-2,-2,-100};mesh.bbMax={2,2,-90};
 for(auto &t:mesh.triangles){t.v0.setZ(-90);t.v1.setZ(-90);t.v2.setZ(-90);}
 project->setLoadedMesh(mesh);
 tool.id=964;tool.type="end_mill";tool.diameter=6;tool.fluteLen=20;tool.totalLen=60;
 ToolLibrary::instance().addTool(tool);
 for(int mode=0;mode<2;++mode){
  std::shared_ptr<StrategyBase> strategy=mode==0 ? std::static_pointer_cast<StrategyBase>(std::make_shared<BlindSlotMillingStrategy>())
                                              : std::static_pointer_cast<StrategyBase>(std::make_shared<PocketRoughingStrategy>());
  ProgramGenerationService generator([=](const QString &id){return id==strategy->id()?strategy:std::shared_ptr<StrategyBase>();},
                                      [=](int){return tool;});
  MachiningOperation operation;operation.id="generated-binding";operation.opType=OperationType::Contour;
  operation.stage=OperationStage::RoughCut;operation.strategyId=strategy->id();operation.toolId=tool.id;
  operation.params=strategy->defaultParams();operation.contourFeature.subType=mode==0?"blind_slot":"rectangular_pocket";
  operation.contourFeature.length=40;operation.contourFeature.width=mode==0?12:20;operation.contourFeature.depth=4;
  operation.contourFeature.axis={0,0,1};operation.contourFeature.region=FaceRegion::Front;
  operation.params.set("depth",4);operation.params.set("slotLength",40);operation.params.set("slotWidth",12);
  operation.params.set("stockToLeave",1);operation.params.set("stepover",4);operation.params.set("entryMode",0);operation.params.set("helixRadius",0);
  operation.contourFeature=applyMachiningParameters(operation.contourFeature,operation.params);
  project->setOperations({operation});
  ProgramGenerationSnapshotOptions snapshot;snapshot.machineProfile=project->machineProfile();snapshot.setupFingerprint=project->setupFingerprint();
  PostProcessorOptions options;options.programNumber="O2401";options.workOffset="G54";
  options.safeStartBlocks=QStringList{"G17 G40 G49 G80","G21","G90","G54","G94"};
  Cq8PostProcessor processor;auto generated=generator.generate(project->operations(),processor,options,snapshot);
  CHECK(generated.ok);const auto original=generated.snapshot;CHECK(original.generationContentMatches());
  CHECK((mode==0)==original.packageFiles.isEmpty());
  project->upsertProgram(original);w->loadProgramById(original.id);
  std::cout<<"mode="<<mode<<" source="<<original.sourceOperationFingerprint.toStdString()
           <<" project="<<ProgramSnapshotFingerprint::calculate(project->operations(),original.sourceOperationIds).toStdString()
           <<" panel="<<ProgramSnapshotFingerprint::calculate(w->m_operationPanel->operations(),original.sourceOperationIds).toStdString()<<std::endl;
  CHECK(w->validateCurrentGCodeForOutput("export"));
  const QString saved=temp.filePath(QString("generated-%1.json").arg(mode));CHECK(project->saveToFile(saved));
  CHECK(project->loadFromFile(saved));project->setLoadedMesh(mesh);w->loadProgramById(original.id);
  CHECK(project->programById(original.id).generationContentMatches());CHECK(w->validateCurrentGCodeForOutput("export"));
  if(mode==1){
   for(int i=0;i<original.packageFiles.size();++i){
    auto changed=original;changed.packageFiles[i].content+="\nG0 X100\n";
    project->upsertProgram(changed);w->loadProgramById(original.id);CHECK(!w->validateCurrentGCodeForOutput("export"));
   }
   auto changed=original;changed.expandedGcodeText+="\nG0 X100\n";
   project->upsertProgram(changed);w->loadProgramById(original.id);CHECK(!w->validateCurrentGCodeForOutput("export"));
   changed=original;changed.generationContentEvidence.clear();
   project->upsertProgram(changed);w->loadProgramById(original.id);CHECK(!w->validateCurrentGCodeForOutput("export"));
  }
  project->upsertProgram(original);w->loadProgramById(original.id);
  w->m_gcodeEditor->setPlainText(original.gcodeText+"\n; edited\n");w->syncCurrentProgramSnapshot();
  CHECK(project->programById(original.id).generationContentEvidence.isEmpty());
 }
 std::cout<<"PASS stale expanded/package blocked; real CQ8 plain/package generation reload and output gate; edits revoke evidence\n";std::_Exit(0);
}
