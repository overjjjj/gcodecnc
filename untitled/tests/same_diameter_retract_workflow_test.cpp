#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QSet>
#include <QRegularExpression>
#include <limits>
#include <iostream>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/process_design/ui/FeatureListPanel.h"
#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/workspaces/process_design/ui/OperationEditDialog.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#undef private
#include "../src/app/AppController.h"
#include "../src/core/Settings.h"
#include "../src/core/SourceFileFingerprint.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/workspaces/process_design/ui/HoleFeatureGrouping.h"
#include "../src/workspaces/process_design/ui/ToolOperationCompatibility.h"
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
int main(int argc,char **argv){
 QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);CHECK(argc==3);
 QTimer::singleShot(240000,&app,[]{std::cerr<<"FAIL test exceeded four minutes"<<std::endl;std::_Exit(2);});
 QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());
 HiddenDialogs dialogs;app.installEventFilter(&dialogs);
 auto *w=new MainWindow;w->setAttribute(Qt::WA_DontShowOnScreen);w->resize(1600,1000);w->show();
 CHECK(!IsWindowVisible(reinterpret_cast<HWND>(w->winId())));
 struct Case{const char *id;const char *strategy;const char *type;const char *cycle;int tool;int mode;const char *key;double initial;double changed;};
 const QVector<Case> cases{
  {"D03.spot","hole_spot","spot_drill","G81",971,0,"depth",5,6},
  {"D03.g81","hole_g81","drill","G81",972,0,"depth",5,6},
  {"D03.g83","hole_deephole","drill","G83",973,0,"depth",5,6},
  {"D03.g73","hole_g73","drill","G73",974,0,"depth",5,6},
  {"D03.g76","hole_g76","boring_bar","G76",975,0,"depth",5,6},
  {"D03.g86","hole_g86","boring_bar","G86",976,0,"depth",5,6},
  {"D03.tap","hole_tapping","tap","G84",977,0,"depth",5,6},
  {"D03.chamfer","hole_chamfer","chamfer_mill","G2",978,0,"chamferWidth",1,1.5},
  {"D03.chamfer","hole_chamfer","chamfer_mill","G81",979,1,"axialDepth",2,2.5}};
 for(const auto &c:cases)CHECK(w->findChild<QToolButton *>("designFeature_"+QString(c.id)));
 const QString sourcePath=QString::fromLocal8Bit(argv[1]);
 StepImporter initial;CHECK(initial.loadWithRotation(sourcePath,QQuaternion()));
 QVector3D sourceAxis;for(const auto &f:initial.features())if(f.region==FaceRegion::Front&&f.radius>=2&&f.depth>=7&&!holeFeatureGroupId(f).isEmpty()){sourceAxis=f.axis;break;}
 CHECK(!sourceAxis.isNull());
 const auto rotation=QQuaternion::rotationTo(sourceAxis,QVector3D(0,0,sourceAxis.z()<0?-1:1));
 StepImporter importer;CHECK(importer.loadWithRotation(sourcePath,rotation));
 const QString out=QString::fromLocal8Bit(argv[2]);CHECK(!QFileInfo::exists(out));CHECK(QDir().mkpath(out));
 auto *project=AppController::instance().projectManager();project->setLoadedMesh(importer.mesh());project->setFeatures(importer.features());project->setSourceFilePath(sourcePath);project->setSourceFileFingerprint(SourceFileFingerprint::calculate(sourcePath));project->setSetupRotation(rotation);
 project->setActiveRegion(FaceRegion::Front);w->m_activeRegion=FaceRegion::Front;emit w->activeRegionChanged(FaceRegion::Front);
 SetupOrigin origin;origin.confirmed=true;project->setSetupOrigin(origin);StockDefinition stock;stock.confirmed=true;project->setStockDefinition(stock);
 Settings::instance().setPostProcessorId("cq8");w->m_featurePanel->setFeatures(project->features());
 QVector<int> holes;
 for(int i=0;i<project->features().size()&&holes.isEmpty();++i){const auto &a=project->features()[i];
  if(std::hypot(a.axis.x(),a.axis.y())>1e-6||a.region!=FaceRegion::Front||a.depth<7||a.radius<2||holeFeatureGroupId(a).isEmpty())continue;
  for(int j=i+1;j<project->features().size();++j){const auto &b=project->features()[j];
   if(std::hypot(b.axis.x(),b.axis.y())<=1e-6&&b.region==FaceRegion::Front&&b.depth>=7&&holeFeatureGroupId(a)==holeFeatureGroupId(b)){holes={i,j};break;}}
 }
 CHECK(holes.size()==2);const double diameter=project->features()[holes[0]].radius*2;
 std::cout<<"HOLES "<<holes[0]<<","<<holes[1]<<" D="<<diameter<<std::endl;
 for(const auto &c:cases){ToolEntry tool;tool.id=c.tool;tool.type=c.type;
  tool.diameter=(tool.type=="tap"||tool.type=="drill")?diameter:tool.type=="boring_bar"?3:diameter*.5;
  tool.pitch=project->features()[holes[0]].pitch>0?project->features()[holes[0]].pitch:1;
  tool.pointAngle=90;tool.extra.insert("angle",90);tool.fluteLen=80;tool.totalLen=120;ToolLibrary::instance().removeTool(tool.id);ToolLibrary::instance().addTool(tool);CHECK(ToolLibrary::instance().tool(tool.id).diameter==tool.diameter);std::cout<<"TOOL "<<tool.id<<" D="<<tool.diameter<<std::endl;}
 auto *tree=w->m_featurePanel->findChild<QTreeWidget *>();CHECK(tree);
 auto selectHoles=[&](int count){QTreeWidgetItemIterator it(tree);while(*it){auto *item=*it;if(item->childCount()==0&&(item->flags()&Qt::ItemIsUserCheckable))item->setCheckState(0,holes.mid(0,count).contains(item->data(0,Qt::UserRole).toInt())?Qt::Checked:Qt::Unchecked);++it;}};
 auto exportProgram=[&](const ProgramEntry &,const QString &stem){
  const int before=dialogs.chooserCount, warnings=dialogs.warnings.size();
  w->onExportGCode();
  CHECK(dialogs.chooserCount==before&&dialogs.warnings.size()==warnings+1);
  CHECK(dialogs.warnings.last().contains("Retract clearance is unproven"));
  CHECK(dialogs.warnings.last().contains("CAD/numerical bound"));
  std::cout<<"EXPECTED_UNPROVEN_BLOCK "<<stem.toStdString()<<std::endl;
 };
 auto assertCuttingDepth=[&](const ProgramEntry &program,const QList<MachiningOperation> &operations){
  SimulationController sim;sim.loadGCode(program.expandedGcodeText.isEmpty()?program.gcodeText:program.expandedGcodeText,false);CHECK(sim.motionError().isEmpty());
  if(operations.first().strategyId=="hole_g73")CHECK(sim.hasUnverifiedRetract());
  if(operations.first().strategyId=="hole_g76")CHECK(sim.hasUnverifiedG76LateralShift());
  if(operations.first().strategyId=="hole_g86")CHECK(sim.hasUnverifiedG86SpindleStopRetract());
  if(operations.first().strategyId=="hole_tapping")CHECK(sim.hasUnverifiedSpindleSync());
  for(const auto &op:operations){
   double entry=std::numeric_limits<double>::quiet_NaN();
   for(int index:holes){const auto &source=importer.features()[index];if(std::hypot(source.center.x()-op.holeFeature.center.x(),source.center.y()-op.holeFeature.center.y())<.001)entry=source.center.z()+source.depth*.5;}
   CHECK(std::isfinite(entry));double lowest=std::numeric_limits<double>::infinity();
   for(const auto &move:sim.moves())if(!move.isRapid&&move.toKnownAxes==7){const auto point=move.to+program.modelOrigin;
    if(std::hypot(point.x()-op.holeFeature.center.x(),point.y()-op.holeFeature.center.y())<=op.holeFeature.radius+.001)lowest=std::min(lowest,double(point.z()));}
   double depth=op.params.get("depth");
   if(op.strategyId=="hole_chamfer")depth=op.params.get("chamferMode")==1?op.params.get("axialDepth"):op.params.get("chamferWidth")/std::tan(op.params.get("chamferAngle")*3.14159265358979323846/180.);
   CHECK(std::abs(lowest-(entry-depth))<.002);
  }
 };
 auto editDepth=[&](const QString &id,const QString &key,double depth){emit w->m_operationPanel->editOperationRequested(id);auto *edit=dynamic_cast<OperationEditDialog *>(w->m_operationEditDialog.data());CHECK(edit);
  bool found=false;for(int row=0;row<edit->m_table->rowCount();++row)if(edit->m_table->item(row,0)->data(Qt::UserRole).toString()==key){edit->m_table->item(row,1)->setText(QString::number(depth));found=true;}CHECK(found);edit->accept();CHECK(edit->result()==QDialog::Accepted);
 };
 const QString onlyCase=QString::fromLocal8Bit(qgetenv("CNEXT_HOLE_CASE"));int completed=0;
 for(const auto &c:cases)for(int count:{1,2}){
  const QString stem=QString(c.id)+"-mode"+QString::number(c.mode);
  if(!onlyCase.isEmpty()&&onlyCase!=stem)continue;
  std::cout<<"CASE "<<stem.toStdString()<<" "<<count<<std::endl;
  project->setOperations({});project->setPrograms({});w->m_operationPanel->setOperations({});selectHoles(count);
  auto *button=w->findChild<QToolButton *>("designFeature_"+QString(c.id));CHECK(button->isEnabled());button->click();
  auto *panel=w->m_strategyPanel;CHECK(w->m_parameterDialog->isVisible());CHECK(panel->m_strategyCombo->currentData().toString()==c.strategy);
  auto params=panel->parameterDraft();params.set("depth",5);params.set(c.key,c.initial);params.set("chamferMode",c.mode);params.set("chamferAngle",45);params.set("boringOffset",.1);params.set("safeHeight",50);params.set("feedHeight",3);params.set("feedRate",40);params.set("spindleSpeed",600);
  params.set("peckDepth",2);params.set("stepDown",1);params.set("plungeRate",40);params.set("stockToLeave",.2);params.set("bottomAllowance",0);params.set("springPasses",1);
  panel->restoreDraft(params,c.tool);
  if(!panel->m_confirmOperationBtn->isEnabled())for(auto *label:panel->findChildren<QLabel *>())std::cerr<<label->objectName().toStdString()<<": "<<label->text().toStdString()<<std::endl;
  CHECK(panel->m_confirmOperationBtn->isEnabled());panel->m_confirmOperationBtn->click();CHECK(project->operations().size()==count);
  QSet<int> confirmedSources;
  for(const auto &op:project->operations()){
   CHECK(op.strategyId==c.strategy&&op.toolId==c.tool&&op.params.get(c.key)==c.initial);
   bool sourcePreserved=false;for(int index:holes.mid(0,count)){const auto &source=importer.features()[index];if(source.center==op.holeFeature.center&&source.radius==op.holeFeature.radius&&source.depth==op.holeFeature.depth){CHECK(!confirmedSources.contains(index));confirmedSources.insert(index);sourcePreserved=true;}}CHECK(sourcePreserved);
  }
  const auto validOperations=project->operations();const auto op=validOperations.first();
  ToolEntry wrong=ToolLibrary::instance().tool(c.tool);wrong.id=995;wrong.type=QString(c.type)=="tap"?"end_mill":"tap";ToolLibrary::instance().removeTool(wrong.id);ToolLibrary::instance().addTool(wrong);
  CHECK(!operationEditError(op,op.params,wrong.id).isEmpty());
  if(QString(c.type)=="drill"||QString(c.type)=="tap"){wrong.type=c.type;wrong.diameter=diameter*2;ToolLibrary::instance().updateTool(wrong);CHECK(!operationEditError(op,op.params,wrong.id).isEmpty());}
  const QString invalidKey=c.key;
  OperationEditDialog invalid(op);bool invalidField=false;
  for(int row=0;row<invalid.m_table->rowCount();++row)if(invalid.m_table->item(row,0)->data(Qt::UserRole).toString()==invalidKey){invalid.m_table->item(row,1)->setText("0");invalidField=true;}
  CHECK(invalidField);invalid.accept();CHECK(invalid.result()!=QDialog::Accepted&&!invalid.m_error->text().isEmpty());
  if(QString(c.strategy)!="hole_circular_mill"){
   OperationEditDialog speedEdit(op);for(int row=0;row<speedEdit.m_table->rowCount();++row)if(speedEdit.m_table->item(row,0)->data(Qt::UserRole).toString()=="spindleSpeed")speedEdit.m_table->item(row,1)->setText("0.5");
   speedEdit.accept();CHECK(speedEdit.result()!=QDialog::Accepted&&!speedEdit.m_error->text().isEmpty());
   auto invalidOperations=validOperations;invalidOperations[0].params.set("spindleSpeed",.5);
   project->setOperations(invalidOperations);w->m_operationPanel->setOperations(invalidOperations);emit w->m_operationPanel->recalculateRequested(invalidOperations);
   CHECK(w->m_operationPanel->operations().first().toolpathState==ToolpathState::Error);
   project->setOperations(validOperations);w->m_operationPanel->setOperations(validOperations);
  }
  w->m_operationPanel->onGenerateProgram();CHECK(!project->programs().isEmpty());const auto program=project->programById(w->m_currentProgramId);CHECK(program.postProcessorId=="cq8");
  CHECK(QRegularExpression(QString("(?:^|\\s)%1(?=\\s|$)").arg(QString(c.strategy)=="hole_circular_mill"?"G0?[23]":c.cycle)).match(program.gcodeText).hasMatch());
  assertCuttingDepth(program,project->operations());
  exportProgram(program,stem+QString("-%1").arg(count));w->grab().save(out+"/"+stem+QString("-%1.png").arg(count));
  const QString path=out+"/"+stem+QString("-%1.cnext").arg(count);CHECK(project->saveToFile(path));CHECK(AppController::instance().loadProject(path));CHECK(project->setupRotation()==rotation.normalized());CHECK(!project->mesh().sourceShape.IsNull());w->m_operationPanel->setOperations(project->operations());w->loadProgramById(program.id);
  CHECK(project->operations().size()==count&&project->programById(program.id).gcodeText==program.gcodeText);
  exportProgram(project->programById(program.id),stem+QString("-%1-reloaded").arg(count));
  editDepth(project->operations().first().id,c.key,c.changed);const int chooserCount=dialogs.chooserCount;w->onExportGCode();CHECK(dialogs.chooserCount==chooserCount);
  std::cout<<"RECALCULATE "<<c.id<<" "<<count<<std::endl;
  const auto recalculated=project->operations();emit w->m_operationPanel->recalculateRequested(recalculated);w->m_operationPanel->onGenerateProgram();
  const auto changed=project->programById(w->m_currentProgramId);CHECK(changed.gcodeText!=program.gcodeText);
  for(int i=0;i<project->operations().size();++i)CHECK(project->operations()[i].params.get(c.key)== (i==0?c.changed:c.initial));
  assertCuttingDepth(changed,project->operations());exportProgram(changed,stem+QString("-%1-edited").arg(count));++completed;
 }
 CHECK(completed==(onlyCase.isEmpty()?18:2));
 std::cout<<"PASS blocked same-diameter cases="<<completed<<" basic hole modes, dedicated buttons, real single/double holes, CQ8 numeric-uncertainty export rejection, reload and edited parameter\n";std::_Exit(0);
}
