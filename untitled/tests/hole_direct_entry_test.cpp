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
 QTimer::singleShot(120000,&app,[]{std::cerr<<"FAIL test exceeded two minutes"<<std::endl;std::_Exit(2);});
 QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());
 HiddenDialogs dialogs;app.installEventFilter(&dialogs);
 auto *w=new MainWindow;w->setAttribute(Qt::WA_DontShowOnScreen);w->resize(1600,1000);w->show();
 CHECK(!IsWindowVisible(reinterpret_cast<HWND>(w->winId())));
 struct Case{const char *id;const char *strategy;const char *type;const char *cycle;int tool;};
 const QVector<Case> cases{{"D03.peck","hole_peck","drill","G83",991},{"D03.ream","hole_reaming","reamer","G85",992},{"D03.circular","hole_circular_mill","end_mill","G2",993}};
 for(const auto &c:cases)CHECK(w->findChild<QToolButton *>("designFeature_"+QString(c.id)));
 StepImporter importer;CHECK(importer.loadWithRotation(QString::fromLocal8Bit(argv[1]),QQuaternion()));
 const QString out=QString::fromLocal8Bit(argv[2]);QDir().mkpath(out);
 auto *project=AppController::instance().projectManager();project->setLoadedMesh(importer.mesh());project->setFeatures(importer.features());project->setSourceFileFingerprint("D03-real-feature-board");
 project->setActiveRegion(FaceRegion::Front);w->m_activeRegion=FaceRegion::Front;emit w->activeRegionChanged(FaceRegion::Front);
 SetupOrigin origin;origin.confirmed=true;project->setSetupOrigin(origin);StockDefinition stock;stock.confirmed=true;project->setStockDefinition(stock);
 Settings::instance().setPostProcessorId("cq8");w->m_featurePanel->setFeatures(project->features());
 QVector<int> holes;
 for(int i=0;i<project->features().size()&&holes.isEmpty();++i){const auto &a=project->features()[i];
  if(a.region!=FaceRegion::Front||a.depth<7||a.radius<2||holeFeatureGroupId(a).isEmpty())continue;
  for(int j=i+1;j<project->features().size();++j){const auto &b=project->features()[j];
   if(b.region==FaceRegion::Front&&b.depth>=7&&holeFeatureGroupId(a)==holeFeatureGroupId(b)){holes={i,j};break;}}
 }
 CHECK(holes.size()==2);const double diameter=project->features()[holes[0]].radius*2;
 std::cout<<"HOLES "<<holes[0]<<","<<holes[1]<<" D="<<diameter<<std::endl;
 for(const auto &c:cases){ToolEntry tool;tool.id=c.tool;tool.type=c.type;tool.diameter=QString(c.type)=="reamer"?diameter:diameter*.5;tool.fluteLen=80;tool.totalLen=120;ToolLibrary::instance().addTool(tool);}
 auto *tree=w->m_featurePanel->findChild<QTreeWidget *>();CHECK(tree);
 auto selectHoles=[&](int count){QTreeWidgetItemIterator it(tree);while(*it){auto *item=*it;if(item->childCount()==0&&(item->flags()&Qt::ItemIsUserCheckable))item->setCheckState(0,holes.mid(0,count).contains(item->data(0,Qt::UserRole).toInt())?Qt::Checked:Qt::Unchecked);++it;}};
 auto exportProgram=[&](const ProgramEntry &program,const QString &stem){
  const QString directory=out+"/"+stem;QDir().mkpath(directory);
  dialogs.destination=program.packageFiles.isEmpty()?directory+"/program.nc":directory;
  const int before=dialogs.chooserCount;w->onExportGCode();CHECK(dialogs.chooserCount==before+1);
  if(program.packageFiles.isEmpty()){QFile file(dialogs.destination);CHECK(file.open(QIODevice::ReadOnly|QIODevice::Text));CHECK(QString::fromUtf8(file.readAll())==program.gcodeText);}
  else for(const auto &f:program.packageFiles){QFile file(directory+"/"+f.fileName);CHECK(file.open(QIODevice::ReadOnly));CHECK(file.readAll()==f.content.toUtf8());}
 };
 auto assertCuttingDepth=[&](const ProgramEntry &program,const QList<MachiningOperation> &operations){
  SimulationController sim;sim.loadGCode(program.expandedGcodeText.isEmpty()?program.gcodeText:program.expandedGcodeText,false);CHECK(sim.motionError().isEmpty());
  for(const auto &op:operations){
   double entry=std::numeric_limits<double>::quiet_NaN();
   for(int index:holes){const auto &source=importer.features()[index];if(std::hypot(source.center.x()-op.holeFeature.center.x(),source.center.y()-op.holeFeature.center.y())<.001)entry=source.center.z()+source.depth*.5;}
   CHECK(std::isfinite(entry));double lowest=std::numeric_limits<double>::infinity();
   for(const auto &move:sim.moves())if(!move.isRapid&&move.toKnownAxes==7){const auto point=move.to+program.modelOrigin;
    if(std::hypot(point.x()-op.holeFeature.center.x(),point.y()-op.holeFeature.center.y())<=op.holeFeature.radius+.001)lowest=std::min(lowest,double(point.z()));}
   CHECK(std::abs(lowest-(entry-op.params.get("depth")))<.002);
  }
 };
 auto editDepth=[&](const QString &id,double depth){emit w->m_operationPanel->editOperationRequested(id);auto *edit=dynamic_cast<OperationEditDialog *>(w->m_operationEditDialog.data());CHECK(edit);
  bool found=false;for(int row=0;row<edit->m_table->rowCount();++row)if(edit->m_table->item(row,0)->data(Qt::UserRole).toString()=="depth"){edit->m_table->item(row,1)->setText(QString::number(depth));found=true;}CHECK(found);edit->accept();CHECK(edit->result()==QDialog::Accepted);
 };
 for(const auto &c:cases)for(int count:{1,2}){
  std::cout<<"CASE "<<c.id<<" "<<count<<std::endl;
  project->setOperations({});project->setPrograms({});w->m_operationPanel->setOperations({});selectHoles(count);
  auto *button=w->findChild<QToolButton *>("designFeature_"+QString(c.id));CHECK(button->isEnabled());button->click();
  auto *panel=w->m_strategyPanel;CHECK(w->m_parameterDialog->isVisible());CHECK(panel->m_strategyCombo->currentData().toString()==c.strategy);
  auto params=panel->parameterDraft();params.set("depth",5);params.set("safeHeight",50);params.set("feedHeight",3);params.set("feedRate",40);params.set("spindleSpeed",600);
  params.set("peckDepth",2);params.set("stepDown",1);params.set("plungeRate",40);params.set("stockToLeave",.2);params.set("bottomAllowance",0);params.set("springPasses",1);
  panel->restoreDraft(params,c.tool);
  if(!panel->m_confirmOperationBtn->isEnabled())for(auto *label:panel->findChildren<QLabel *>())std::cerr<<label->objectName().toStdString()<<": "<<label->text().toStdString()<<std::endl;
  CHECK(panel->m_confirmOperationBtn->isEnabled());panel->m_confirmOperationBtn->click();CHECK(project->operations().size()==count);
  for(const auto &op:project->operations()){
   CHECK(op.strategyId==c.strategy&&op.toolId==c.tool&&op.params.get("depth")==5);
   bool sourcePreserved=false;for(int index:holes){const auto &source=importer.features()[index];if(source.center==op.holeFeature.center&&source.radius==op.holeFeature.radius&&source.depth==op.holeFeature.depth)sourcePreserved=true;}CHECK(sourcePreserved);
  }
  const auto validOperations=project->operations();const auto op=validOperations.first();
  ToolEntry wrong=ToolLibrary::instance().tool(c.tool);wrong.id=995;wrong.type="tap";ToolLibrary::instance().removeTool(wrong.id);ToolLibrary::instance().addTool(wrong);
  CHECK(!operationEditError(op,op.params,wrong.id).isEmpty());
  wrong.type=c.type;wrong.diameter=diameter*2;ToolLibrary::instance().updateTool(wrong);CHECK(!operationEditError(op,op.params,wrong.id).isEmpty());
  const QString invalidKey=QString(c.strategy)=="hole_peck"?"peckDepth":QString(c.strategy)=="hole_circular_mill"?"stepDown":"spindleSpeed";
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
  if(QString(c.strategy)=="hole_peck"&&count==1){
   auto deep=op;deep.holeFeature.depth=60;deep.holeFeature.radius=5;deep.params.set("depth",60);deep.params.set("peckDepth",8);
   OperationEditDialog qEdit(deep);auto *summary=qEdit.findChild<QLabel *>("effectivePeckSummary");CHECK(summary&&summary->text().contains("2.500"));
   for(int row=0;row<qEdit.m_table->rowCount();++row)if(qEdit.m_table->item(row,0)->data(Qt::UserRole).toString()=="depth")qEdit.m_table->item(row,1)->setText("20");
   CHECK(summary->text().contains("有效Q 8.000"));
   StrategyPanel preview;preview.setFeature(deep.holeFeature);CHECK(preview.selectStrategy("hole_peck"));preview.restoreDraft(deep.params,c.tool);CHECK(!preview.m_effectivePeckSummary->isHidden()&&preview.m_effectivePeckSummary->text().contains("2.500"));
   auto shallow=deep.params;shallow.set("depth",20);preview.restoreDraft(shallow,c.tool);CHECK(preview.m_effectivePeckSummary->text().contains("有效Q 8.000"));
  }
  w->m_operationPanel->onGenerateProgram();CHECK(!project->programs().isEmpty());const auto program=project->programById(w->m_currentProgramId);CHECK(program.postProcessorId=="cq8");
  CHECK(QRegularExpression(QString("(?:^|\\s)%1(?=\\s|$)").arg(QString(c.strategy)=="hole_circular_mill"?"G0?[23]":c.cycle)).match(program.gcodeText).hasMatch());
  assertCuttingDepth(program,project->operations());
  exportProgram(program,QString(c.id)+QString("-%1").arg(count));w->grab().save(out+"/"+c.id+QString("-%1.png").arg(count));
  const QString path=out+"/"+c.id+QString("-%1.cnext").arg(count);CHECK(project->saveToFile(path));CHECK(project->loadFromFile(path));project->setLoadedMesh(importer.mesh());w->m_operationPanel->setOperations(project->operations());w->loadProgramById(program.id);
  CHECK(project->operations().size()==count&&project->programById(program.id).gcodeText==program.gcodeText);
  editDepth(project->operations().first().id,6);const int chooserCount=dialogs.chooserCount;w->onExportGCode();CHECK(dialogs.chooserCount==chooserCount);
  std::cout<<"RECALCULATE "<<c.id<<" "<<count<<std::endl;
  const auto recalculated=project->operations();emit w->m_operationPanel->recalculateRequested(recalculated);w->m_operationPanel->onGenerateProgram();
  const auto changed=project->programById(w->m_currentProgramId);CHECK(changed.gcodeText!=program.gcodeText);
  for(int i=0;i<project->operations().size();++i)CHECK(project->operations()[i].params.get("depth")== (i==0?6:5));
  assertCuttingDepth(changed,project->operations());exportProgram(changed,QString(c.id)+QString("-%1-depth6").arg(count));
 }
 std::cout<<"PASS dedicated peck/ream/circular buttons, real single/double holes, CQ8 actual export, reload and edited depth\n";std::_Exit(0);
}
