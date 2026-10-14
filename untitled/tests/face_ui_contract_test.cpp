#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/workspaces/process_design/ui/MillingOperationDialog.h"
#include <QApplication>
#include <iostream>

int main(int argc,char **argv)
{
    QApplication app(argc,argv);bool ok=true;
    MachiningFeature f;f.kind=FeatureKind::FlatSurface;f.subType="face_area";
    f.length=40;f.width=20;f.depth=9;f.center={20,10,0};f.planarBoundaryVerified=true;
    f.boundaryPoints={{0,0,0},{40,0,0},{40,20,0},{0,20,0}};
    StrategyPanel panel;panel.setContourFeature(f);panel.selectStrategy("mill_face");
    if(panel.parameterDraft().get("depth",-1)!=0){std::cerr<<"face depth guessed from recognition\n";ok=false;}
    MillingOperationDialog alternate;alternate.setFeature(f);
    if(!alternate.feature().planarBoundaryVerified || alternate.feature().points!=f.boundaryPoints){
        std::cerr<<"alternate face entry loses original boundary\n";ok=false;
    }
    if(alternate.feature().depth!=0){std::cerr<<"alternate face entry guesses depth\n";ok=false;}
    return ok?0:1;
}
