#include "../src/gcode/ProgramPackageExporter.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <iostream>
static ProgramFileEntry entry(const QString &kind,const QString &name,const QString &content) {
    ProgramFileEntry file;file.kind=kind;file.fileName=name;file.content=content;
    file.sha256=QString::fromLatin1(QCryptographicHash::hash(content.toUtf8(),QCryptographicHash::Sha256).toHex());return file;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);int failures=0;QTemporaryDir generic,blocked;
    const auto main=entry("main","OTHER.NC","O1000\nM98 P9001\nM30\n");
    const auto sub=entry("subprogram","SUB.NC","O9001\nM99\n");
    if(!ProgramPackageExporter::exportFiles(generic.path(),main.fileName,{main,sub}).ok) {
        std::cerr<<"FAIL non-CQ8 package reclassified\n";++failures;
    }
    const auto cqMain=entry("main","CQ8_MAIN.NC",main.content);
    const auto macro=entry("macro","CQ8_MACROS.NC",sub.content);
    QDir(blocked.path()).mkdir(macro.fileName);
    QFile previous(blocked.filePath(cqMain.fileName));previous.open(QIODevice::WriteOnly);previous.write("previous-main");previous.close();
    const auto result=ProgramPackageExporter::exportFiles(blocked.path(),cqMain.fileName,{cqMain,macro});
    previous.open(QIODevice::ReadOnly);
    if(result.ok || previous.readAll()!="previous-main") {
        std::cerr<<"FAIL second file failure replaced main\n";++failures;
    }
    return failures?1:0;
}
