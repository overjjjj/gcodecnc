#include <QCoreApplication>
#include <iostream>
#include <cmath>
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);int failures=0;
    auto check=[&](bool ok,const QString &message){if(!ok){std::cerr<<message.toStdString()<<"\n";++failures;}};
    const QString valid="%\nO100\nG21 G90 G17 G94\nG40 G49 G80\nG54\nT1 M6\nS1000 M3\nG0X10.Y0Z0\nG3 X0 Y10 I-10 J0 F100 (X999 G91)\nG2 I0 J-10\nG1X0Y0;G0X999\nM5 M9\nM30\n%\n";
    check(SimulationController::externalGCodeError(valid).isEmpty(),"supported program rejected");
    SimulationController sim;sim.loadGCode(valid,false);
    check(sim.moves().size()>60,"both arcs including IJ-only circle generate motion");
    for(const auto &move:sim.moves())if(move.sourceLine==8||move.sourceLine==9)
        check(std::abs(std::hypot(move.to.x(),move.to.y())-10)<.001,"IJ arc radius mismatch");
    check(sim.moves().last().to==QVector3D(0,0,0),"final position");
    check(sim.moves().first().toolDiameter==0&&sim.moves().first().toolModelPath.isEmpty(),"external T does not assume project tool geometry");
    const QStringList rejected={"G91 G1 X1","G20 G1 X1","G18 G2 X1 I1","G19","G2.1 X1 I1","G90.1","G1 X#1","M98 P1","M99","G68 X1","G92 X1","G53 G0 X1","G55","G41","G43 H1 X1","G81 X1 Z-5 R2","MCALL CYCLE81(5,0,2,-5)","G2 X10 R5","G2 I0 J0","G2 X10 I1 J0","G2 X10","G0 G1 X10","G1 X1 X2","G1 X1 (bad","G1 X1)","G1 X1 @","G1 X1e3","G1 X1 A20","/G0 X10","IF[1]G1X10","G1 X1\nM30\nG1 X2","G80 G1 X10"};
    for(const auto &line:rejected)check(SimulationController::externalGCodeError("; header\n"+line).contains(line.contains('\n')?"4":"2"),"missing line diagnostic: "+line);
    check(!SimulationController::externalGCodeError("G21 G90 G17\nM30").isEmpty(),"no-motion rejected");
    check(SimulationController::externalGCodeError("G1 X10\nX20\n").isEmpty(),"modal omitted G supported");
    std::cout<<"Parser failures="<<failures<<std::endl;return failures?1:0;
}
