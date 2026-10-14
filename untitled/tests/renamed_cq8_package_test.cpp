#include "../src/gcode/ProgramPackageExporter.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QDir>
#include <iostream>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTemporaryDir out;
    ProgramFileEntry main,macro;main.kind="main";main.fileName="part.txt";main.content="O1000\nM98 P9001\nM30\n";
    macro.kind="macro";macro.fileName="part_MACROS.txt";macro.content="O9002\nM99\n";
    for(auto *file:{&main,&macro})file->sha256=QString::fromLatin1(QCryptographicHash::hash(file->content.toUtf8(),QCryptographicHash::Sha256).toHex());
    if(ProgramPackageExporter::exportFiles(out.path(),main.fileName,{main,macro},true).ok || !QDir(out.path()).entryList(QDir::Files).isEmpty()) {
        std::cerr<<"FAIL renamed CQ8 missing routine was exported\n";return 1;
    }
    if(ProgramPackageExporter::exportFiles(out.path(),main.fileName,{main},true).ok) {
        std::cerr<<"FAIL renamed main without dependency was exported\n";return 2;
    }
    macro.content="O9001\nM99\n";macro.sha256=QString::fromLatin1(QCryptographicHash::hash(macro.content.toUtf8(),QCryptographicHash::Sha256).toHex());
    if(!ProgramPackageExporter::exportFiles(out.path(),main.fileName,{main,macro},true).ok)return 3;
    std::cout<<"PASS renamed package still checks macro dependency\n";
}
