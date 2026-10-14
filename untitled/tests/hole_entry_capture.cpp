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
#include <QTabWidget>
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
 auto *w=new MainWindow;w->setAttribute(Qt::WA_DontShowOnScreen);w->resize(1280,900);w->show();
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
 selectHoles(2);app.processEvents();CHECK(w->grab().save(out+"/direct-buttons.png"));
 w->findChild<QToolButton *>("designFeature_D03.peck")->click();
 auto *panel=w->m_strategyPanel;auto params=panel->parameterDraft();
 params.set("depth",30);params.set("peckDepth",8);params.set("safeHeight",50);params.set("feedHeight",3);params.set("spindleSpeed",600);params.set("feedRate",40);
 panel->restoreDraft(params,991);app.processEvents();
 CHECK(panel->m_effectivePeckSummary->isVisibleTo(w->m_parameterDialog));
 CHECK(panel->m_effectivePeckSummary->text().contains("有效Q 4.000"));
 CHECK(w->m_parameterDialog->grab().save(out+"/peck-effective-q.png"));
 const int labelTop = panel->m_effectivePeckSummary->mapTo(w->m_parameterDialog, QPoint(0, 0)).y();
 const int tabsBottom = panel->m_operationTabs->mapTo(w->m_parameterDialog, QPoint(0, panel->m_operationTabs->height())).y();
 std::cout<<"LAYOUT labelTop="<<labelTop<<" tabsBottom="<<tabsBottom<<" contentTop="<<panel->m_effectivePeckSummary->contentsRect().top()<<std::endl;
 CHECK(labelTop+panel->m_effectivePeckSummary->contentsRect().top()>=tabsBottom+4);
 CHECK(!IsWindowVisible(reinterpret_cast<HWND>(w->winId())));
 std::cout<<"CAPTURE "<<panel->m_effectivePeckSummary->text().toStdString()<<std::endl;std::_Exit(0);
}
