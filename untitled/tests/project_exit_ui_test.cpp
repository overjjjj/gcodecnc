#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QMessageBox>
#include <QFileDialog>
#include <QTimer>
#include <QSettings>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QPushButton>
#include <QQueue>
#include <QEventLoop>
#include <QElapsedTimer>
#include <iostream>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#undef private
#include "../src/app/AppController.h"
#include "../src/workspaces/program_verification/ui/GCodeEditor.h"
#include "../src/core/Settings.h"

#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x<<std::endl;return 1;}}while(0)
static void settle() {QEventLoop loop;QTimer::singleShot(50,&loop,&QEventLoop::quit);loop.exec();}
static QByteArray read(const QString &path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
class Dialogs:public QObject {
public:
    QElapsedTimer elapsed;
    Dialogs(){elapsed.start();}
    QQueue<QMessageBox::StandardButton> decisions;
    QQueue<QString> files;
    int prompts=0,choosers=0,warnings=0;
    QString evidence;
    bool unexpected=false;
    QString overwriteFixture;
    bool eventFilter(QObject *object,QEvent *event) override {
        if(event->type()!=QEvent::Polish)return false;
        if(auto *dialog=qobject_cast<QDialog *>(object))dialog->setAttribute(Qt::WA_DontShowOnScreen);
        if(auto *box=qobject_cast<QMessageBox *>(object)) {
            QTimer::singleShot(0,box,[this,box]{
                if(box->standardButtons().testFlag(QMessageBox::Save)
                    && box->standardButtons().testFlag(QMessageBox::Discard)
                    && box->standardButtons().testFlag(QMessageBox::Cancel)) {
                    ++prompts;std::cout<<"PROMPT "<<prompts<<" ms="<<elapsed.elapsed()<<std::endl;
                    box->grab().save(evidence+QString("/prompt-%1.png").arg(prompts));
                    if(decisions.isEmpty()){unexpected=true;box->button(QMessageBox::Cancel)->click();return;}
                    box->button(decisions.dequeue())->click();
                } else if(!overwriteFixture.isEmpty() && box->text().contains(overwriteFixture)
                    && box->button(QMessageBox::Yes)) {
                    box->button(QMessageBox::Yes)->click();
                } else {
                    ++warnings;std::cout<<"WARNING ms="<<elapsed.elapsed()<<" "<<box->text().toStdString()<<std::endl;
                    box->grab().save(evidence+QString("/warning-%1.png").arg(warnings));box->accept();
                }
            });
        } else if(auto *file=qobject_cast<QFileDialog *>(object)) {
            ++choosers;std::cout<<"FILE_DIALOG "<<choosers<<" ms="<<elapsed.elapsed()<<std::endl;
            QTimer::singleShot(0,file,[this,file]{
                if(files.isEmpty()){unexpected=true;file->reject();return;}
                const QString path=files.dequeue();
                std::cout<<"FILE_REQUEST "<<path.toStdString()<<" ms="<<elapsed.elapsed()<<std::endl;
                if(path.isEmpty()){file->reject();return;}
                file->setDirectory(QFileInfo(path).absolutePath());file->selectFile(path);
                QMetaObject::invokeMethod(file,"accept");
                std::cout<<"FILE_ACCEPT_RETURN ms="<<elapsed.elapsed()<<std::endl;
            });
        }
        return false;
    }
};
int main(int argc,char **argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,dir.path());
    Settings::instance().setLanguage("zh_CN");
    const bool saveAsMode=argc>1 && QString::fromLocal8Bit(argv[1])=="save-as";
    Dialogs dialogs;dialogs.evidence=QCoreApplication::applicationDirPath()
        +(saveAsMode?"/project-save-as-evidence":"/project-exit-evidence");
    QDir().mkpath(dialogs.evidence);app.installEventFilter(&dialogs);
    QTimer::singleShot(300000,&app,[&]{
        auto *modal=QApplication::activeModalWidget();
        std::cerr<<"TIMEOUT ms="<<dialogs.elapsed.elapsed()<<" modal="<<(modal?modal->metaObject()->className():"none")
            <<" title="<<(modal?modal->windowTitle().toStdString():"")<<std::endl;
        if(modal)modal->grab().save(dialogs.evidence+"/timeout-modal.png");std::_Exit(99);
    });
    MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.show();settle();
    auto *project=AppController::instance().projectManager();
    CHECK(!project->isModified());
    CHECK(window.close() && dialogs.prompts==0);window.show();
    ProgramEntry program;program.id="exit-program";program.gcodeText="G0 Z10\nM30\n";
    project->setPrograms({program});window.loadProgramById(program.id);settle();
    if(saveAsMode) {
        const QString originalPath=dir.filePath("original.cnext");
        dialogs.files.enqueue(originalPath);window.m_actSaveProject->trigger();
        CHECK(!project->isModified());const auto originalBytes=read(originalPath);
        auto *saveAs=window.findChild<QAction *>("saveProjectAsAction");
        CHECK(saveAs);
        window.m_gcodeEditor->setGCode("G0 Z25\nM30\n");window.syncCurrentProgramSnapshot();
        dialogs.files.enqueue(QString());saveAs->trigger();
        CHECK(project->isModified() && window.m_projectFilePath==originalPath && read(originalPath)==originalBytes);
        const QString lockedPath=dir.filePath("locked.cnext");
        {QFile file(lockedPath);CHECK(file.open(QIODevice::WriteOnly));file.write("original locked contents");}
        const auto lockedBytes=read(lockedPath);
        const HANDLE lock=CreateFileW(reinterpret_cast<LPCWSTR>(lockedPath.utf16()),GENERIC_READ,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        CHECK(lock!=INVALID_HANDLE_VALUE);
        dialogs.overwriteFixture=QFileInfo(lockedPath).fileName();
        const int warningsBefore=dialogs.warnings;
        dialogs.files.enqueue(lockedPath);saveAs->trigger();CloseHandle(lock);dialogs.overwriteFixture.clear();
        CHECK(dialogs.warnings==warningsBefore+1 && project->isModified() && window.m_projectFilePath==originalPath);
        CHECK(read(lockedPath)==lockedBytes && read(originalPath)==originalBytes);
        const QString newPath=dir.filePath("copy.cnext");
        dialogs.files.enqueue(newPath);saveAs->trigger();
        CHECK(!project->isModified() && window.m_projectFilePath==newPath && read(originalPath)==originalBytes);
        ProjectManager copy;CHECK(copy.loadFromFile(newPath) && copy.programById(program.id).gcodeText=="G0 Z25\nM30\n");
        window.m_gcodeEditor->setGCode("G0 Z35\nM30\n");
        const int choosersBefore=dialogs.choosers;dialogs.decisions.enqueue(QMessageBox::Save);
        CHECK(window.close() && dialogs.choosers==choosersBefore);
        CHECK(copy.loadFromFile(newPath) && copy.programById(program.id).gcodeText=="G0 Z35\nM30\n");
        CHECK(read(originalPath)==originalBytes && !dialogs.unexpected && dialogs.files.isEmpty());
        std::cout<<"PASS Save As cancellation, commit failure, new path and subsequent Save"<<std::endl;return 0;
    }
    dialogs.decisions.enqueue(QMessageBox::Cancel);
    CHECK(!window.close() && window.isVisible() && project->isModified());
    CHECK(dialogs.prompts==1 && dialogs.decisions.isEmpty());
    dialogs.decisions.enqueue(QMessageBox::Cancel);
    window.m_actExit->trigger();settle();
    CHECK(window.isVisible() && dialogs.prompts==2 && dialogs.decisions.isEmpty());
    dialogs.decisions.enqueue(QMessageBox::Save);dialogs.files.enqueue(QString());
    CHECK(!window.close() && window.isVisible() && project->isModified());
    CHECK(dialogs.prompts==3 && dialogs.choosers==1);
    const QString savedPath=dir.filePath("saved.cnext");
    dialogs.decisions.enqueue(QMessageBox::Save);dialogs.files.enqueue(savedPath);
    CHECK(window.close() && !project->isModified() && QFileInfo::exists(savedPath));
    window.show();
    window.m_gcodeEditor->setGCode("G0 Z22\nM30\n");
    CHECK(!project->isModified()); // Text has not yet been copied into the snapshot.
    dialogs.decisions.enqueue(QMessageBox::Cancel);
    CHECK(!window.close() && project->isModified());
    CHECK(project->programById(program.id).gcodeText=="G0 Z22\nM30\n");
    const auto original=read(savedPath);
    const HANDLE locked=CreateFileW(reinterpret_cast<LPCWSTR>(savedPath.utf16()),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    CHECK(locked!=INVALID_HANDLE_VALUE);
    const int chooserBefore=dialogs.choosers,warningBefore=dialogs.warnings;
    dialogs.decisions.enqueue(QMessageBox::Save);
    const bool closed=window.close();settle();CloseHandle(locked);
    CHECK(!closed && window.isVisible() && project->isModified());
    CHECK(read(savedPath)==original && dialogs.choosers==chooserBefore && dialogs.warnings==warningBefore+1);
    // Cancelling replacement must retain the current program and its target path.
    dialogs.files.enqueue(savedPath);dialogs.decisions.enqueue(QMessageBox::Cancel);
    CHECK(QMetaObject::invokeMethod(&window,"onOpenProject",Qt::DirectConnection));
    CHECK(project->programById(program.id).gcodeText=="G0 Z22\nM30\n" && project->isModified());
    dialogs.files.enqueue("D:/codex/home/worktrees/5a59/gcodecnc/outputs/face-c-20260915/models/engineering-box.step");
    dialogs.decisions.enqueue(QMessageBox::Cancel);
    CHECK(QMetaObject::invokeMethod(&window,"onImportStep",Qt::DirectConnection));
    CHECK(project->programById(program.id).gcodeText=="G0 Z22\nM30\n" && project->sourceFilePath().isEmpty());
    dialogs.decisions.enqueue(QMessageBox::Save);
    CHECK(window.close() && !project->isModified());
    ProjectManager reloaded;
    CHECK(reloaded.loadFromFile(savedPath) && reloaded.programById(program.id).gcodeText=="G0 Z22\nM30\n");
    window.show();window.m_gcodeEditor->setGCode("G0 Z30\nM30\n");
    const auto savedBytes=read(savedPath);dialogs.decisions.enqueue(QMessageBox::Discard);
    CHECK(window.close() && read(savedPath)==savedBytes);
    window.show();
    const QString model="D:/codex/home/worktrees/5a59/gcodecnc/outputs/face-c-20260915/models/engineering-box.step";
    dialogs.files.enqueue(model);dialogs.decisions.enqueue(QMessageBox::Discard);
    CHECK(QMetaObject::invokeMethod(&window,"onImportStep",Qt::DirectConnection));
    CHECK(project->sourceFilePath()==model && !project->mesh().sourceShape.IsNull() && project->programs().isEmpty());
    CHECK(window.m_projectFilePath.isEmpty() && read(savedPath)==savedBytes);
    const QString importedPath=dir.filePath("imported.cnext");
    const int firstImportChooser=dialogs.choosers;
    dialogs.files.enqueue(importedPath);
    CHECK(QMetaObject::invokeMethod(&window,"onSaveProject",Qt::DirectConnection));
    CHECK(dialogs.choosers==firstImportChooser+1 && !project->isModified() && window.m_projectFilePath==importedPath);
    project->setPrograms({program});window.loadProgramById(program.id);
    CHECK(project->isModified());
    const QString badProject=dir.filePath("invalid.cnext"),badStep=dir.filePath("invalid.step");
    for(const auto &path:{badProject,badStep}) {QFile file(path);CHECK(file.open(QIODevice::WriteOnly));file.write("invalid");}
    const int errorsBefore=dialogs.warnings;
    dialogs.files.enqueue(badProject);dialogs.decisions.enqueue(QMessageBox::Discard);
    CHECK(QMetaObject::invokeMethod(&window,"onOpenProject",Qt::DirectConnection));
    CHECK(project->programById(program.id).gcodeText==program.gcodeText && project->isModified());
    CHECK(window.m_projectFilePath==importedPath && project->sourceFilePath()==model);
    dialogs.files.enqueue(badStep);dialogs.decisions.enqueue(QMessageBox::Discard);
    CHECK(QMetaObject::invokeMethod(&window,"onImportStep",Qt::DirectConnection));
    CHECK(project->programById(program.id).gcodeText==program.gcodeText && project->isModified());
    CHECK(window.m_projectFilePath==importedPath && project->sourceFilePath()==model && dialogs.warnings==errorsBefore+2);
    dialogs.files.enqueue(importedPath);dialogs.decisions.enqueue(QMessageBox::Discard);
    CHECK(QMetaObject::invokeMethod(&window,"onOpenProject",Qt::DirectConnection));
    CHECK(project->programs().isEmpty() && !project->mesh().sourceShape.IsNull());
    CHECK(!project->isModified() && window.m_projectFilePath==importedPath);
    project->setPrograms({program});window.loadProgramById(program.id);
    const int existingChooser=dialogs.choosers;
    CHECK(QMetaObject::invokeMethod(&window,"onSaveProject",Qt::DirectConnection));
    CHECK(!project->isModified() && dialogs.choosers==existingChooser);
    CHECK(reloaded.loadFromFile(importedPath) && reloaded.programById(program.id).gcodeText==program.gcodeText);
    const int cleanPrompts=dialogs.prompts;
    CHECK(window.close() && dialogs.prompts==cleanPrompts);
    window.show();window.m_gcodeEditor->setGCode("G0 Z40\nM30\n");
    const auto lastSaved=read(importedPath);dialogs.decisions.enqueue(QMessageBox::Discard);
    window.m_actExit->trigger();settle();
    CHECK(!window.isVisible() && read(importedPath)==lastSaved);
    CHECK(!dialogs.unexpected && dialogs.decisions.isEmpty() && dialogs.files.isEmpty());
    std::cout<<"ELAPSED_MS "<<dialogs.elapsed.elapsed()<<std::endl;
    std::cout<<"PASS real close/menu, save/discard/cancel, failed-save preservation, edited NC and cancelled replacement"<<std::endl;
    return 0;
}
