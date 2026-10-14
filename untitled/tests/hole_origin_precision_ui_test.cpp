#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTimer>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QKeyEvent>
#include <iostream>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/process_design/ui/FeatureListPanel.h"
#undef private
#include "../src/workspaces/process_design/ui/SetupOriginDialog.h"
#include "../src/app/AppController.h"
#include "../src/core/MachiningCoordinates.h"
#include "../src/core/SourceFileFingerprint.h"
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x<<std::endl;std::_Exit(1);}}while(0)
class HiddenDialogs:public QObject{
public:
 bool eventFilter(QObject *object,QEvent *event)override{
  if(event->type()!=QEvent::Polish)return false;
  if(auto *dialog=qobject_cast<QDialog*>(object))dialog->setAttribute(Qt::WA_DontShowOnScreen);
  if(auto *box=qobject_cast<QMessageBox*>(object))QTimer::singleShot(0,box,[box]{CHECK(box->button(QMessageBox::Yes));box->button(QMessageBox::Yes)->click();});
  else if(auto *origin=qobject_cast<SetupOriginDialog*>(object))QTimer::singleShot(0,origin,[origin]{QMetaObject::invokeMethod(origin,"accept");});
  return false;
 }
};
int main(int argc,char **argv){
 QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);CHECK(argc==2);
 QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());
 HiddenDialogs hidden;app.installEventFilter(&hidden);auto *window=new MainWindow;window->setAttribute(Qt::WA_DontShowOnScreen);window->show();CHECK(!IsWindowVisible(reinterpret_cast<HWND>(window->winId())));
 const QString path=QString::fromLocal8Bit(argv[1]);StepImporter initial;CHECK(initial.loadWithRotation(path,{}));
 QVector3D axis;for(const auto &f:initial.features())if(f.region==FaceRegion::Front&&f.radius>=2&&f.depth>=7){axis=f.axis;break;}
 const auto rotation=QQuaternion::rotationTo(axis,{0,0,axis.z()<0?-1.f:1.f});StepImporter importer;CHECK(importer.loadWithRotation(path,rotation));
 auto *project=AppController::instance().projectManager();project->setLoadedMesh(importer.mesh());project->setFeatures(importer.features());project->setSourceFilePath(path);project->setSourceFileFingerprint(SourceFileFingerprint::calculate(path));project->setSetupRotation(rotation);project->setActiveRegion(FaceRegion::Front);window->m_activeRegion=FaceRegion::Front;
 window->m_featurePanel->setFeatures(project->features());auto *tree=window->m_featurePanel->findChild<QTreeWidget*>();CHECK(tree);
 HoleFeature selected;bool found=false;QTreeWidgetItemIterator it(tree);while(*it){auto *item=*it;const int id=item->data(0,Qt::UserRole).toInt();if(item->childCount()==0&&id>=0&&id<project->features().size()){
  const auto &f=project->features()[id];if(f.kind==FeatureKind::Hole&&f.region==FaceRegion::Front){tree->setCurrentItem(item);selected=f;found=true;break;}}++it;}
 CHECK(found);window->onSetOriginFromSelectedHole();CHECK(project->setupOrigin().confirmed);
 auto zero=[&]{const auto work=inWorkCoordinates(selected,machiningOrigin(project));return work.center.x()==0&&work.center.y()==0;};CHECK(zero());
 const auto before=project->setupOrigin().customPoint;CHECK(project->saveToFile(settings.filePath("hole-origin.cnext")));CHECK(AppController::instance().loadProject(settings.filePath("hole-origin.cnext")));CHECK(zero());
 window->onEditSetupOrigin();CHECK(project->setupOrigin().customPoint==before);CHECK(zero());
 SetupOriginDialog edit(true);edit.setOrigin(project->setupOrigin());auto spins=edit.findChildren<QDoubleSpinBox*>();CHECK(spins.size()==6);const double edited=spins[0]->value()+.125;spins[0]->setValue(edited);CHECK(edit.origin().customPoint.x()==edited);CHECK(edit.origin().customPoint.y()==before.y());
 auto exact=project->setupOrigin();exact.customPoint.setY(-12.000000164);exact.offset.setX(.000000164);edit.setOrigin(exact);
 CHECK(edit.origin().customPoint.y()==exact.customPoint.y()&&edit.origin().offset.x()==exact.offset.x());
 auto type=[&](QDoubleSpinBox *spin,const QString &text){auto *line=spin->findChild<QLineEdit*>();CHECK(line);line->selectAll();QKeyEvent key(QEvent::KeyPress,Qt::Key_0,Qt::NoModifier,text);QApplication::sendEvent(line,&key);spin->interpretText();};
 type(spins[1],"-12");type(spins[3],"0");CHECK(edit.origin().customPoint.y()==-12);CHECK(edit.origin().offset.x()==0);
 std::cout<<"PASS selected hole origin, project reload, unchanged dialog confirmation and explicit edit preserve precise coordinates"<<std::endl;std::_Exit(0);
}
