#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QTimer>
#include <QMessageBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QRegularExpression>
#include <iostream>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#undef private
#include "../src/app/AppController.h"
#include "../src/workspaces/process_design/ui/OperationBatchEditDialog.h"
#include "../src/workspaces/process_design/ui/OperationEditDialog.h"
#include "../src/workspaces/process_design/operations/OperationWorkflow.h"
#include "../src/strategies/StrategyFactory.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/gcode/ProgramSnapshotStatus.h"
// Qt 5.14's Windows message-box Show handler dereferences a native menu under
// the offscreen plugin. Keep the real modal validation, but skip native chrome.
class HiddenMessageBoxFilter : public QObject {
 bool eventFilter(QObject *object,QEvent *event) override {
  return event->type()==QEvent::Show && qobject_cast<QMessageBox*>(object);
 }
};
int main(int argc,char **argv){
 QApplication app(argc,argv);QTemporaryDir temp;
 HiddenMessageBoxFilter hiddenMessages;app.installEventFilter(&hiddenMessages);
 QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
 int failures=0;auto check=[&](bool ok,const char*m){std::cout<<(ok?"PASS ":"FAIL ")<<m<<std::endl;failures+=!ok;};
 ToolEntry drill;drill.id=501;drill.type="drill";drill.diameter=6;drill.fluteLen=20;drill.totalLen=50;ToolLibrary::instance().addTool(drill);
 ToolEntry mill;mill.id=502;mill.type="end_mill";mill.diameter=2;mill.fluteLen=8;mill.totalLen=50;ToolLibrary::instance().addTool(mill);
 MachiningOperation a,b,untouched;a.id="drill-a";a.strategyId="hole_g81";a.toolId=501;a.featureRef="Hole A";
 a.holeFeature.center={-20,0,0};a.holeFeature.axis={0,0,1};a.holeFeature.radius=3;a.holeFeature.depth=6;a.holeFeature.region=FaceRegion::Front;
 a.params=StrategyFactory::instance().strategy(a.strategyId)->defaultParams();a.params.set("depth",3);a.params.set("safeHeight",50);a.params.set("feedHeight",3);a.params.set("spindleSpeed",1000);a.params.set("feedRate",100);
 b.id="pocket-b";b.opType=OperationType::Contour;b.strategyId="mill_pocket_rough";b.toolId=502;b.featureRef="Pocket B";
 b.params=StrategyFactory::instance().strategy(b.strategyId)->defaultParams();b.params.set("depth",5);b.params.set("entryMode",0);b.params.set("safeHeight",50);b.params.set("feedHeight",3);b.params.set("feedRate",160);b.params.set("stepDown",1);
 b.contourFeature.subType="rectangular_pocket";b.contourFeature.center={20,0,0};b.contourFeature.axis={0,0,1};b.contourFeature.region=FaceRegion::Front;b.contourFeature.depth=5;b.contourFeature.length=20;b.contourFeature.width=10;b.contourFeature.points={{10,-5,0},{30,-5,0},{30,5,0},{10,5,0}};
 untouched=a;untouched.id="unselected";untouched.holeFeature.center={-40,0,0};
 MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);auto *panel=window.m_operationPanel;auto *project=AppController::instance().projectManager();project->setOperations({a,b,untouched});
 ProgramGenerationService service([](const QString&id){return StrategyFactory::instance().strategy(id);},[](int id){return ToolLibrary::instance().tool(id);});Cq8PostProcessor post;PostProcessorOptions options;options.workOffset="G54";options.safeStartBlocks=QStringList{"G17 G40 G49 G80","G21","G90","G54","G94"};ProgramGenerationSnapshotOptions snapshot;
 auto generate=[&](const QList<MachiningOperation>&ops){auto r=service.generate(enabledOperations(ops),post,options,snapshot);if(!r.ok)std::cout<<r.errors.join(" | ").toStdString()<<std::endl;return r;};
 const auto before=generate(project->operations());check(before.ok,"mixed drill and pocket baseline generates final CQ8");
 auto outputValid=[&](const ProgramEntry &program){return ProgramSnapshotStatus::evaluate(project->operations(),program.sourceOperationIds,program.sourceOperationFingerprint,{}, {},program.toolFingerprint,program.toolFingerprint).okForOutput();};
 auto open=[&](){auto *tree=panel->findChild<QTreeWidget*>("operationTree");panel->selectOperationById(a.id);auto *first=tree->currentItem();panel->selectOperationById(b.id);first->setSelected(true);panel->findChild<QToolButton*>("batchEditOperationsButton")->click();return dynamic_cast<OperationBatchEditDialog*>(window.m_operationEditDialog.data());};
 auto close=[&](){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);};
 auto recalculate=[&](){panel->findChild<QTreeWidget*>("operationTree")->selectAll();panel->onRecalculate();};
 QTimer dismissWarnings;QObject::connect(&dismissWarnings,&QTimer::timeout,[](){for(auto *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox*>(widget))box->accept();});dismissWarnings.start(10);
 auto field=[&](OperationBatchEditDialog*d,const QString&key,const QString&value){auto*c=d->findChild<QCheckBox*>("batchParameterSelect_"+key);auto*e=d->findChild<QLineEdit*>("batchParameterValue_"+key);check(c&&e,"common parameter exposed");if(c&&e){c->setChecked(true);e->setText(value);}};
 for(int repeat=0;repeat<3;++repeat){
  emit panel->editOperationRequested(a.id);
  auto *single=dynamic_cast<OperationEditDialog*>(window.m_operationEditDialog.data());if(!single)return 7;
  single->findChild<QDoubleSpinBox*>("operationParameter_depth")->setValue(4);
  auto changed=project->operations();changed[0].params.set("feedRate",111);project->setOperations(changed);
  bool accepting=true,deletedDuringAccept=false;QObject::connect(single,&QObject::destroyed,[&](){deletedDuringAccept=accepting;});
  single->accept();accepting=false;check(!deletedDuringAccept,"single conflict warning does not destroy accepting dialog");close();
  check(project->operations()[0].params.get("depth")==3&&project->operations()[0].params.get("feedRate")==111,"single conflict preserves newer parameters");
  project->setOperations({a,b,untouched});
 }
 auto *cancel=open();if(!cancel)return 2;field(cancel,"depth","4");cancel->reject();close();
 check(project->operations()[0].params.values==a.params.values&&project->operations()[1].params.values==b.params.values,"cancel leaves every operation unchanged");
 for(int change=0;change<3;++change){
  auto *pending=open();if(!pending)return 5;field(pending,"depth","4");
  auto changed=project->operations();
  if(change==0)changed[0].params.set("feedRate",111);
  if(change==1)changed.removeAt(1);
  if(change==2)changed[1].enabled=false;
  bool accepting=true,deletedDuringAccept=false;
  QObject::connect(pending,&QObject::destroyed,[&](){deletedDuringAccept=accepting;});
  project->setOperations(changed);pending->accept();accepting=false;
  check(!deletedDuringAccept,"conflict warning does not destroy dialog inside accept signal stack");close();
  const auto actual=project->operations();
  check(actual.size()==changed.size()&&actual[0].params.values==changed[0].params.values
        &&(change==1||actual[1].enabled==changed[1].enabled),"pending dialog cannot overwrite changed deleted or disabled operations");
  project->setOperations({a,b,untouched});
 }
 auto *invalid=open();if(!invalid)return 3;field(invalid,"depth","12");invalid->accept();
 check(invalid->result()!=QDialog::Accepted&&invalid->findChild<QLabel*>("batchEditError")->text().contains(b.id),"one excessive tool depth identifies failing pocket");
 check(project->operations()[0].params.values==a.params.values&&project->operations()[1].params.values==b.params.values,"late invalid item does not partially update earlier drill");
 field(invalid,"depth","NaN");invalid->accept();check(invalid->result()!=QDialog::Accepted,"nonfinite batch input rejected");
 field(invalid,"depth","4");invalid->findChild<QCheckBox*>("batchChangeTool")->setChecked(true);auto *tools=invalid->findChild<QComboBox*>("batchEditTool");tools->setCurrentIndex(tools->findData(501));invalid->accept();
 check(invalid->result()!=QDialog::Accepted&&project->operations()[1].toolId==502,"incompatible common tool rejects entire batch");invalid->reject();close();
 auto *valid=open();if(!valid)return 4;field(valid,"depth","4");valid->accept();close();const auto edited=project->operations();
 check(edited[0].params.get("depth")==4&&edited[1].params.get("depth")==4&&edited[1].contourFeature.depth==4,"mixed batch uses shared parameter-to-geometry application");
 check(edited[0].params.get("feedRate")==100&&edited[1].params.get("feedRate")==160&&edited[2].params.values==untouched.params.values,"unselected fields and operations retain their own values");
 check(edited[0].toolpathState==ToolpathState::Stale&&edited[1].toolpathState==ToolpathState::Stale,"successful batch invalidates both selected toolpaths");
 check(!outputValid(before.snapshot),"parameter edit invalidates the previously generated program");
 check(!generate(edited).ok,"stale batch cannot generate before real recalculation");
 recalculate();
 check(project->operations()[0].toolpathState==ToolpathState::Valid&&project->operations()[1].toolpathState==ToolpathState::Valid,"MainWindow recalculates selected toolpaths");
 const auto after=generate(project->operations());check(after.ok&&after.snapshot.expandedGcodeText!=before.snapshot.expandedGcodeText,"batch edit changes final expanded NC");
 SimulationController sim;sim.loadGCode(after.snapshot.expandedGcodeText);bool atDepth=false;for(const auto&m:sim.moves())atDepth|=!m.isRapid&&std::abs(m.to.z()+4)<.0001;
 check(sim.motionError().isEmpty()&&atDepth,"final NC simulation reaches edited depth");
 const QString saved=temp.filePath("batch-project.json");check(project->saveToFile(saved),"save mixed edited project");ProjectManager restored;check(restored.loadFromFile(saved),"reload mixed edited project");
 check(restored.operations().size()==3&&restored.operations()[0].id==a.id&&restored.operations()[1].id==b.id&&restored.operations()[1].contourFeature.depth==4,"reload retains IDs and applied contour depth");
 project->setOperations(restored.operations());
 recalculate();
 QEventLoop clockBoundary;QTimer::singleShot(1100,&clockBoundary,&QEventLoop::quit);clockBoundary.exec();
 const auto regenerated=generate(project->operations());
 const QString evidence=argc>1?QString::fromLocal8Bit(argv[1]):QCoreApplication::applicationDirPath()+"/workflow-evidence";QDir().mkpath(evidence);
 auto saveNc=[&](const QString &name,const QString &nc){QFile file(evidence+"/"+name);if(file.open(QIODevice::WriteOnly))file.write(nc.toUtf8());};
 saveNc("before-reload.nc",after.snapshot.expandedGcodeText);saveNc("after-reload.nc",regenerated.snapshot.expandedGcodeText);
 auto motionText=[](QString text){text.remove(QRegularExpression("^\\( Generated by CNEXT-CAM  [0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} \\)\\r?\\n"));return text;};
 check(regenerated.ok&&motionText(regenerated.snapshot.expandedGcodeText)==motionText(after.snapshot.expandedGcodeText),"reload and recalculate preserves all NC except exact generation timestamp header");
 auto enabledSet=project->operations();enabledSet[1].enabled=false;project->setOperations(enabledSet);
 check(!outputValid(after.snapshot),"disabling B invalidates the actual generated A+B program");
 const auto activeProgram=generate(project->operations());
 check(activeProgram.ok&&outputValid(activeProgram.snapshot)&&!activeProgram.snapshot.sourceOperationIds.contains(b.id),"regeneration contains only currently enabled operations and is output-valid");
 enabledSet[1].enabled=true;project->setOperations(enabledSet);
 check(!outputValid(activeProgram.snapshot),"reenabling B invalidates actual A-only program");
 for(auto &op:enabledSet)op.enabled=false;project->setOperations(enabledSet);
 check(!generate(project->operations()).ok&&!outputValid(activeProgram.snapshot),"all disabled cannot generate or output old program");
 project->setOperations(restored.operations());recalculate();
 auto *feedEdit=open();if(!feedEdit)return 6;field(feedEdit,"feedRate","140");feedEdit->accept();close();recalculate();
 const auto feedProgram=generate(project->operations());
 check(feedProgram.ok&&motionText(feedProgram.snapshot.expandedGcodeText)!=motionText(after.snapshot.expandedGcodeText)
       &&feedProgram.snapshot.expandedGcodeText.contains("F140")
       &&project->operations()[0].params.get("feedRate")==140&&project->operations()[1].params.get("feedRate")==140
       &&project->operations()[0].params.get("depth")==4,"legal batch feed edit reaches final NC and preserves depth");
 project->setOperations(restored.operations());recalculate();
 // Copy through the real list action, preserving full region evidence and disabled state.
 auto source=project->operations();source[1].enabled=false;
 source[1].selectionEvidence.id="evidence-copy";
 SelectionBoundaryLoop loop;loop.faceId="face:12";loop.orderedEdgeIds=QStringList{"edge:1","edge:2","edge:3","edge:4"};loop.orderedForward={true,true,true,true};loop.machiningRole=MachiningBoundaryRole::Outer;loop.machiningBoundary=source[1].contourFeature.points;loop.maximumDepth=5;loop.supportFaceIds=QStringList{"face:floor"};
 source[1].selectionEvidence.boundaryLoops.append(loop);
 project->setOperations(source);panel->selectOperationById(b.id);
 panel->findChild<QToolButton*>("copyOperationsButton")->click();
 auto copies=project->operations();check(copies.size()==4,"real copy action commits one new operation");
 if(copies.size()==4){
  const auto copy=copies.last();check(copy.id!=b.id&&!copy.id.isEmpty()&&!copy.enabled&&copy.selectionEvidence==source[1].selectionEvidence,"copy has new ID and retains disabled state and region proof");
  check(copy.toolpathState==ToolpathState::Stale,"copy requires recalculation");
  copies.last().params.set("feedRate",177);project->setOperations(copies);
  check(project->operations()[1].params.get("feedRate")==160,"copy parameters are independent of source");
  const QString copyFile=temp.filePath("copied.json");check(project->saveToFile(copyFile),"save copied operation");ProjectManager copyReload;check(copyReload.loadFromFile(copyFile),"reload copied operation");
  check(copyReload.operations().size()==4&&copyReload.operations().last().id==copy.id
        &&copyReload.operations().last().selectionEvidence==copy.selectionEvidence&&!copyReload.operations().last().enabled
        &&copyReload.operations().last().params.get("feedRate")==177,"reload retains independent copy ID parameters enabled state and region proof");
 }
 project->setOperations(restored.operations());
 if(argc>1){QDir().mkpath(QString::fromLocal8Bit(argv[1]));auto*d=open();window.resize(1500,950);window.grab().save(QString::fromLocal8Bit(argv[1])+"/operations.png");if(d){d->grab().save(QString::fromLocal8Bit(argv[1])+"/batch-editor.png");d->reject();close();}QFile nc(QString::fromLocal8Bit(argv[1])+"/batch-depth4.nc");if(nc.open(QIODevice::WriteOnly))nc.write(after.snapshot.expandedGcodeText.toUtf8());}
 return failures?1:0;
}
