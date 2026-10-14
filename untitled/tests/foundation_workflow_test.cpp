#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <iostream>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#undef private
#include "../src/app/AppController.h"
int main(int argc,char **argv){
 QApplication app(argc,argv);QTemporaryDir settings;
 QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
 MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);int failures=0;
 auto check=[&](bool value,const char *message){std::cout<<(value?"PASS ":"FAIL ")<<message<<std::endl;failures+=!value;};
 auto *g81=window.findChild<QToolButton*>("designFeature_D03.g81");
 auto *thread=window.findChild<QToolButton*>("designFeature_D03.thread_mill");
 check(g81&&g81->isEnabled()&&g81->text()=="G81","catalog identifies exact G81 command");
 check(thread&&!thread->isEnabled(),"catalog retains unimplemented thread entry without enabling it");
 MachiningOperation a,b;a.id="source-a";a.strategyId="hole_g81";a.params.set("depth",3);a.geometryRefs=QStringList{"hole:a"};a.markToolpathValid();
 b=a;b.id="source-b";b.enabled=false;b.params.set("depth",5);
 auto *panel=window.m_operationPanel;panel->setOperations({a,b});
 auto *tree=panel->findChild<QTreeWidget*>("operationTree");
 panel->selectOperationById(a.id);auto *first=tree->currentItem();
 panel->selectOperationById(b.id);first->setSelected(true);
 auto *copy=panel->findChild<QToolButton*>("copyOperationsButton");
 auto *batch=panel->findChild<QToolButton*>("batchEditOperationsButton");
 check(copy&&copy->isEnabled(),"copy is available for actual multi-selection");
 check(batch&&batch->isEnabled(),"batch editor is available for actual multi-selection");
 if(copy){copy->click();const auto ops=panel->operations();check(ops.size()==4,"copy creates one independent operation per selection");
  if(ops.size()==4){check(ops[2].id!=a.id&&ops[3].id!=b.id&&ops[2].id!=ops[3].id,"copies have fresh independent IDs");
   check(ops[2].params.values==a.params.values&&ops[2].geometryRefs==a.geometryRefs&&ops[3].enabled==b.enabled,"copy retains parameters provenance and enabled state");
   check(ops[2].toolpathState==ToolpathState::Stale&&ops[3].toolpathState==ToolpathState::Stale,"copies do not inherit valid toolpath state");
   check(AppController::instance().projectManager()->operations().size()==4,"copy commits through existing project wiring");}}
 return failures?1:0;
}
