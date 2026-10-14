#define NOMINMAX
#include <windows.h>
#include "../src/core/ProjectManager.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

static int failures=0;
static void check(bool value,const char *message) {
    if(!value){++failures;std::cerr<<"FAIL "<<message<<std::endl;}
}
static QByteArray read(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTemporaryDir dir;
    ProjectManager project;
    ProgramEntry first;first.id="first";first.gcodeText="G0 Z10\nM30\n";
    ProgramEntry second=first;second.id="second";
    project.setPrograms({first,second});
    project.setCurrentProgramId(first.id);
    const QString path=dir.filePath("project.cnext");
    check(project.isModified(),"program insertion is dirty");
    check(project.saveToFile(path) && !project.isModified(),"successful save clears dirty");
    const auto original=read(path);
    project.setCurrentProgramId(second.id);
    check(!project.isModified(),"program browsing alone does not dirty machining content");
    first.gcodeText="G0 Z20\nM30\n";project.upsertProgram(first);
    check(!project.saveToFile(dir.path()) && project.isModified(),"open failure preserves dirty");
    const HANDLE locked=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(locked!=INVALID_HANDLE_VALUE,"lock existing file against atomic replacement");
    if(locked!=INVALID_HANDLE_VALUE) {
        check(!project.saveToFile(path),"real atomic commit failure is reported");
        check(project.isModified(),"commit failure preserves dirty");
        check(read(path)==original,"commit failure preserves exact original bytes");
        CloseHandle(locked);
    }
    check(project.saveToFile(path) && !project.isModified(),"unlocked retry saves and clears dirty");
    ProjectManager reloaded;
    check(reloaded.loadFromFile(path) && reloaded.programById(first.id).gcodeText==first.gcodeText,
        "retry persists edited program");
    std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
