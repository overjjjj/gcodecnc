#include "../src/core/ProjectManager.h"
#include "../src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);QTemporaryDir dir;bool ok=true;
    MachiningOperation op;op.id="face";op.opType=OperationType::Contour;op.strategyId="mill_face";
    op.contourFeature.planarBoundaryVerified=true;op.contourFeature.depth=2;op.params.set("depth",2);
    op.contourFeature.points={{0,0,0},{40,0,0},{40,20,0},{0,20,0}};
    const auto before=ProgramSnapshotFingerprint::calculate({op},{"face"});
    auto changed=op;changed.contourFeature.planarBoundaryVerified=false;
    if(before==ProgramSnapshotFingerprint::calculate({changed},{"face"})){
        std::cerr<<"boundary completeness omitted from snapshot fingerprint\n";ok=false;
    }
    ProjectManager project;project.setOperations({op});
    MachiningFeature f;f.kind=FeatureKind::FlatSurface;f.planarBoundaryVerified=true;f.boundaryPoints=op.contourFeature.points;
    project.setFeatures({f});const auto path=dir.path()+"/face.json";
    if(!project.saveToFile(path)) return 2;
    ProjectManager reopened;if(!reopened.loadFromFile(path)) return 3;
    if(reopened.operations().size()!=1 || !reopened.operations()[0].contourFeature.planarBoundaryVerified
        || reopened.features().size()!=1 || !reopened.features()[0].planarBoundaryVerified){
        std::cerr<<"original planar evidence lost on save/reopen\n";ok=false;
    }
    return ok?0:1;
}
