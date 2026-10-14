#include <QCoreApplication>
#include <iostream>
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);int failures=0;
    auto check=[&](bool ok,const char *message){if(!ok){std::cerr<<"FAIL "<<message<<"\n";++failures;}};
    const QString header="G17 G21 G90 G94\nG40 G49 G80 G69\nG54\nG0 X10\nG1 X20\nM30\n";
    check(SimulationController::externalGCodeError(header).isEmpty(),"common initialization with G69");
    const QString middle="G0 X10\nG1 X20 F100\nG69\nX30\n(G69 X999)\n; G69 G0 X999\nY10\nM30\n";
    check(SimulationController::externalGCodeError(middle).isEmpty(),"standalone G69 after motion");
    SimulationController sim;sim.loadGCode(middle,false);
    check(sim.moves().size()==4,"G69 and its comments must not create movement");
    if(sim.moves().size()==4) {
        check(sim.moves()[2].from==QVector3D(20,0,0)&&sim.moves()[2].to==QVector3D(30,0,0)&&!sim.moves()[2].isRapid,"G69 retains position and G1 mode");
        check(sim.moves()[2].sourceLine==3&&sim.moves()[3].sourceLine==6,"source line mapping preserved");
    }
    const QStringList rejected={"G68 R90","G123","G69.1","G69 X10","G69 G1 X10","G69 I1 J0","G69 G68 R90"};
    for(const auto &line:rejected)check(SimulationController::externalGCodeError("; header\n"+line+"\nG1 X20\n").contains("2"),qPrintable(line));
    check(!SimulationController::externalGCodeError("G69\n(G69 X999)\n; G69 X999").isEmpty(),"cancellation and comments alone are no motion");
    std::cout<<"G69 failures="<<failures<<std::endl;return failures?1:0;
}
