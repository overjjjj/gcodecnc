#include "../src/core/ProjectManager.h"
#include "../src/gcode/ProgramSnapshotStatus.h"
#include <QCoreApplication>
#include <iostream>
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);if(argc!=3)return 2;
    ProjectManager project;
    if(QString::fromLocal8Bit(argv[1])=="create"){
        MachiningOperation op;op.id="coordinate-version";op.opType=OperationType::Contour;
        op.strategyId="mill_circle";op.contourFeature.center={267.283203125,-12,-10};
        op.contourFeature.radius=10;op.contourFeature.depth=2;
        project.setOperations({op});
        ProgramEntry program;program.id="old-contour";program.sourceOperationIds=QStringList{op.id};
        program.sourceOperationFingerprint=ProgramSnapshotFingerprint::calculate({op},{op.id});
        project.setPrograms({program});
        if(!project.saveToFile(QString::fromLocal8Bit(argv[2])))return 3;
        std::cout<<"BASELINE "<<program.sourceOperationFingerprint.toStdString()<<std::endl;return 0;
    }
    if(!project.loadFromFile(QString::fromLocal8Bit(argv[2]))||project.programs().size()!=1)return 4;
    const auto program=project.programs().first();
    const auto status=ProgramSnapshotStatus::evaluate(project.operations(),program.sourceOperationIds,program.sourceOperationFingerprint);
    if(status.state!=ProgramSnapshotState::Changed){std::cerr<<"Old contour coordinate semantics must invalidate the saved program"<<std::endl;return 1;}
    std::cout<<"PASS previous contour coordinate semantics are stale"<<std::endl;return 0;
}
