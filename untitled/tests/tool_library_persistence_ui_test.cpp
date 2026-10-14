#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QMessageBox>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QPixmap>
#include <iostream>
#include "../src/app/MainWindow.h"
#include "../src/tool/ToolLibrary.h"
#define CHECK(x) do { if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x<<std::endl;return 1;} } while(0)
class HiddenWarnings : public QObject {
public:
 QStringList messages;
 bool eventFilter(QObject *object,QEvent *event) override {
  if(auto *box=qobject_cast<QMessageBox *>(object)) if(event->type()==QEvent::Polish){
   box->setAttribute(Qt::WA_DontShowOnScreen);messages<<box->text();
   const QString screenshot=QCoreApplication::applicationDirPath()+QString("/tool-library-warning-%1.png").arg(messages.size());
   QTimer::singleShot(0,box,[box,screenshot]{box->grab().save(screenshot);box->accept();});
  }
  return false;
 }
};
static QByteArray read(const QString &path){QFile f(path);f.open(QIODevice::ReadOnly);return f.readAll();}
int main(int argc,char **argv){
 QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
 QApplication app(argc,argv);QTemporaryDir temp;
 QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
 QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,temp.path());
 HiddenWarnings warnings;app.installEventFilter(&warnings);
 const QString path=QCoreApplication::applicationDirPath()+"/tools.json";
 // The harness executes in this batch's disposable test-bin, never the user runtime.
 CHECK(path.contains("design-d07-tool-persistence-20261008/test-bin") || path.contains("design-d07-tool-persistence-20261008\\test-bin"));
 {QFile f(path);CHECK(f.open(QIODevice::WriteOnly|QIODevice::Truncate));f.write("{broken");}
 auto &library=ToolLibrary::instance();const auto count=library.allTools().size();
 {
  MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.show();
  app.processEvents();
  CHECK(!IsWindowVisible(reinterpret_cast<HWND>(window.winId())));
  CHECK(library.allTools().size()==count);CHECK(read(path)=="{broken");
  CHECK(warnings.messages.size()==1);CHECK(warnings.messages.last().contains("kept"));
  CHECK(library.saveToFile(path));const auto before=read(path);const auto modified=QFileInfo(path).lastModified();
  CHECK(library.loadFromFile(path));app.processEvents();
  CHECK(warnings.messages.size()==1);CHECK(read(path)==before);CHECK(QFileInfo(path).lastModified()==modified);
  HANDLE locked=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
                             nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  CHECK(locked!=INVALID_HANDLE_VALUE);
  ToolEntry tool;tool.name="unsaved fixture";tool.type="drill";tool.diameter=6;
  CHECK(library.addTool(tool));app.processEvents();
  CHECK(warnings.messages.size()==2);CHECK(warnings.messages.last().contains("not saved"));
  CHECK(read(path)==before);CloseHandle(locked);
 }
 std::cout<<"PASS invalid load feedback, load without rewrite, failed commit feedback, hidden window normal teardown"<<std::endl;
 return 0;
}
