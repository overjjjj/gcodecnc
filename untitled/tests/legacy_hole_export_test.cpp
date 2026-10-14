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

 const QString oldPath="D:/yanggao/gcodecnc/outputs/design-d03-basic-holes-20261008/evidence-verified-g81-mode0/D03.g81-mode0-1.cnext";
 const QString source=QString::fromLocal8Bit(argv[1]);
 ProjectManager legacy;CHECK(legacy.loadFromFile(oldPath));
 legacy.setSourceFilePath(source);legacy.setSourceFileFingerprint(SourceFileFingerprint::calculate(source));
 QTemporaryDir projectDir;const QString path=projectDir.filePath("legacy.cnext");CHECK(legacy.saveToFile(path));
 ToolEntry tool;tool.id=972;tool.type="drill";tool.diameter=legacy.operations().first().holeFeature.radius;tool.pitch=1;tool.pointAngle=90;tool.extra.insert("angle",90);tool.fluteLen=80;tool.totalLen=120;
 ToolLibrary::instance().removeTool(tool.id);ToolLibrary::instance().addTool(tool);
 CHECK(AppController::instance().loadProject(path));auto *project=AppController::instance().projectManager();
 CHECK(!project->mesh().sourceShape.IsNull());CHECK(!project->programs().isEmpty());
 w->m_operationPanel->setOperations(project->operations());w->loadProgramById(project->programs().first().id);
 dialogs.destination=QString::fromLocal8Bit(argv[2])+"/must-not-export.nc";
 w->onExportGCode();CHECK(dialogs.chooserCount==0);CHECK(!QFileInfo::exists(dialogs.destination));
 CHECK(dialogs.warnings.join("\n").contains("Source operations have changed"));
 std::cout<<"PASS legacy tilted project reimports real STEP and rejects unchanged old NC before file chooser\n";std::_Exit(0);
}
