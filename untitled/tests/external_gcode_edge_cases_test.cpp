#include <QCoreApplication>
#include <iostream>
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);int failures=0;
    if(SimulationController::externalGCodeError("G0 X10 Y0\nG3 X10 Y0.00005 I-10 J0\nM30").isEmpty()) {
        std::cerr<<"FAIL near-endpoint arc accepted as full circle\n";++failures;
    }
    if(SimulationController::externalGCodeError("%\nG1 X1\n%\nG1 X2").isEmpty()) {
        std::cerr<<"FAIL commands after closing percent accepted\n";++failures;
    }
    SimulationController sim;
    sim.loadGCode("G0 Z20\nN10 MCALL CYCLE81(20,0,2,-5,)\nX10 Y10\nMCALL\nM30");
    bool depth=false;for(const auto &move:sim.moves())if(move.to.z()==-5)depth=true;
    if(!depth){std::cerr<<"FAIL numbered Siemens cycle arguments removed as comment\n";++failures;}
    std::cout<<"Edge-case failures="<<failures<<std::endl;return failures?1:0;
}
