#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/postprocessor/FanucPostProcessor.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);int failures=0;
    auto check=[&](bool ok,const char *why){if(!ok){++failures;std::cerr<<why<<'\n';}};
    const QStringList markers{
        ";CNEXT_HOLE_CYCLE code=G81 rtp=50 rfp=1.0000001 sdis=2.0000002 x=1 y=2 z=-0.9999999 q=0 p=0 f=40 vari=0",
        ";CNEXT_HOLE_CYCLE code=G81 rtp=50 rfp=1.0000004 sdis=2.0000002 x=3 y=2 z=-0.9999996 q=0 p=0 f=40 vari=0"};
    FanucPostProcessor fanuc;Cq8PostProcessor cq8;PostProcessorOptions options;
    for(const PostProcessorBase *post:QVector<const PostProcessorBase*>{&fanuc,&cq8}){
        const QString nc=post->wrapGCode(markers,options);
        check(nc.count("G98 G81")==2&&nc.contains("R3.000000")&&nc.contains("R3.000001"),"cycle batching must distinguish the final rounded R plane");
    }
    SimulationController linear;
    linear.loadGCode("G90\nG0 X1 Y2 Z3\nG1 X1.000001\nY2.000001\nZ3.000001",false);
    check(linear.moves().size()==4,"six-decimal linear moves must not disappear");
    const QString arc="G90\nG0 X1 Y0 Z3\nG3 X0.99999999995 Y0.00001 I-1 J0";
    check(SimulationController::externalGCodeError(arc).isEmpty(),"valid small arc must retain its specified endpoint");
    SimulationController curved;curved.loadGCode(arc,false);
    double length=0;
    for(const auto &move:curved.moves())if(!move.isRapid)length+=(move.to-move.from).length();
    check(length>0.000009&&length<0.000011,"small arc must not become a full circle");
    check(!curved.moves().isEmpty()&&std::abs(curved.moves().last().to.y()-.00001)<1e-12,"small arc ends at final NC endpoint");
    SimulationController circle;circle.loadGCode("G90\nG0 X1 Y0 Z3\nG3 X1 Y0 I-1 J0",false);
    double circleLength=0;for(const auto &move:circle.moves())if(!move.isRapid)circleLength+=(move.to-move.from).length();
    check(circleLength>6&&circleLength<6.3,"explicit coincident endpoints still produce a full circle");
    SimulationController unrepresentable;
    unrepresentable.loadGCode("G90\nG0 X1000000000000 Y0 Z3\nG3 I0.00001 J0",false);
    check(!unrepresentable.motionError().isEmpty(),"unrepresentable arc center must fail instead of silently becoming a line");
    SimulationController partialCenter;partialCenter.loadGCode("G90\nG0 X10000000000000000 Y0 Z0\nG3 I1 J1",false);
    check(!partialCenter.motionError().isEmpty(),"partially lost arc-center components must fail");
    SimulationController radial;radial.loadGCode("G90\nG0 X1 Y0 Z0\nG3 X1.00001 Y0 I-1 J0",false);
    check(!radial.motionError().isEmpty(),"distinct radial endpoints must not become a full circle");
    SimulationController unknown;unknown.loadGCode("G90\nG0 X1 Y2 Z3\nG91 G28 Z0\nG90\nG0 X4",false);
    check(!unknown.moves().isEmpty()&&unknown.moves().last().sourceLine<2,"G28 must not fabricate a known motion before XYZ is re-established");
    std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
