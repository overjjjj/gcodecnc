#include "../src/core/ProjectManager.h"
#include "../src/gcode/ProgramSnapshotStatus.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 2;int failures=0;
 auto check=[&](bool ok,const char *label){if(!ok){++failures;std::cerr<<label<<'\n';}};
 ProjectManager old;if(!old.loadFromFile(QString::fromLocal8Bit(argv[1]))||old.programs().isEmpty())return 3;
 const auto program=old.programs().first();
 std::cout<<"BASELINE_HASH="<<ProgramSnapshotFingerprint::calculate(old.operations(),program.sourceOperationIds).toStdString()<<std::endl;
 check(!ProgramSnapshotStatus::evaluate(old.operations(),program.sourceOperationIds,program.sourceOperationFingerprint,program.setupFingerprint,program.setupFingerprint,program.toolFingerprint,program.toolFingerprint).okForOutput(),"legacy hole snapshot must require regeneration");
 QFile input(QString::fromLocal8Bit(argv[1]));if(!input.open(QIODevice::ReadOnly))return 4;
 auto json=QJsonDocument::fromJson(input.readAll()).object();auto operations=json["operations"].toArray();
 for(int i=0;i<operations.size();++i){auto op=operations[i].toObject();auto hole=op["holeFeature"].toObject();hole.remove("ax");hole.remove("ay");hole.remove("az");op["holeFeature"]=hole;operations[i]=op;}
 json["operations"]=operations;QTemporaryDir temp;QFile file(temp.filePath("missing-axis.cnext"));if(!file.open(QIODevice::WriteOnly))return 5;file.write(QJsonDocument(json).toJson());file.close();
 ProjectManager missing;if(!missing.loadFromFile(file.fileName()))return 6;
 check(!missing.operations().isEmpty()&&missing.operations().first().holeFeature.axis.isNull(),"missing saved axis must not become +Z");
 std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
