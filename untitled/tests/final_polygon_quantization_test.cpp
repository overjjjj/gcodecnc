#include "../src/workspaces/program_verification/generation/RegionToolpathValidation.h"
#include <iostream>
#include <QCoreApplication>
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/strategies/mill/PocketRoughingStrategy.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
static int failures=0;
static void check(bool pass,const char *label){std::cout<<"CASE "<<label<<" "<<pass<<std::endl;if(!pass)++failures;}
class InjectedPocket : public PocketRoughingStrategy {
public: QString text;
 ToolpathResult generate(const ContourFeature &,const ToolEntry &,const StrategyParams &)const override {ToolpathResult r;r.ok=true;r.gcode=text;return r;}
};
int main(int argc,char **argv){QCoreApplication app(argc,argv);
 ContourFeature f;f.depth=3;f.points={{0,0,0},{10,0,0},{10,10,0},{0,10,0}};
 StrategyParams p;p.set("stockToLeave",0);p.set("feedHeight",3);ToolMove m;
 for(float x:{.999f,.999999f,1.f,1.001f}){m.from={x,2,-1};m.to={x,8,-1};check(regionMoveError(m,f,p,2).isEmpty()==(x>=1),"strict outer disk boundary");}
 p.set("stockToLeave",.25);m.from={1.249f,2,-1};m.to={1.249f,8,-1};check(!regionMoveError(m,f,p,2).isEmpty(),"stock allowance is not consumed by NC tolerance");
 m.from.setX(1.25f);m.to.setX(1.25f);check(regionMoveError(m,f,p,2).isEmpty(),"exact stock allowance accepted");p.set("stockToLeave",0);
 m.from={1.001f,2,-1};m.to={1.001f,8,-1};m.chordDeviation=.002;check(!regionMoveError(m,f,p,2).isEmpty(),"arc sagitta is not consumed by NC tolerance");
 m.chordDeviation=.0005;check(regionMoveError(m,f,p,2).isEmpty(),"bounded arc chord envelope fits");m.chordDeviation=0;
 m.isRapid=true;m.from={1,2,-1};m.to={.999f,2,0};check(!regionMoveError(m,f,p,2).isEmpty(),"small lateral upward rapid is not vertical retract");m.isRapid=false;
 m.from={2,2,-3};m.to={2,2,-3.001f};check(!regionMoveError(m,f,p,2).isEmpty(),"actual NC cannot cut deeper than proved volume");
 f.points={{0,0,0},{20,0,0},{20,20,0},{0,20,0}};f.islands={{{8,8,0},{12,8,0},{12,12,0},{8,12,0}}};
 for(float x:{6.999f,7.f,7.001f}){m.from={x,9,-1};m.to={x,11,-1};check(regionMoveError(m,f,p,2).isEmpty()==(x<=7),"strict island disk boundary");}
 auto strategy=std::make_shared<InjectedPocket>();ToolEntry tool;tool.id=1;tool.type="end_mill";tool.diameter=2;tool.fluteLen=30;tool.totalLen=60;
 ProgramGenerationService service([&](const QString &){return strategy;},[&](int){return tool;});Cq8PostProcessor post;PostProcessorOptions options;options.workOffset="G54";
 options.safeStartBlocks=QStringList{"G17 G40 G49 G80","G21","G90","G54","G94"};ProgramGenerationSnapshotOptions snapshot;
 MachiningOperation op;op.id="strict-final-polygon";op.opType=OperationType::Contour;op.strategyId="mill_pocket_rough";op.toolId=1;op.params=strategy->defaultParams();
 op.params.set("depth",3);op.params.set("stockToLeave",0);op.params.set("entryMode",0);op.params.set("feedHeight",3);op.params.set("safeHeight",50);
 op.contourFeature.subType="rectangular_pocket";op.contourFeature.axis={0,0,1};op.contourFeature.region=FaceRegion::Front;op.contourFeature.depth=3;op.contourFeature.length=10;op.contourFeature.width=10;op.contourFeature.points={{0,0,0},{10,0,0},{10,10,0},{0,10,0}};
 for(const auto &x:QStringList{"0.999","1.000"}) {
  strategy->text="T1 M6\nS1200 M3\nM8\nG0 Z50\nG0 X"+x+" Y2\nG1 Z-1 F100\nG1 Y8 F100\nG0 Z50";
  const auto result=service.generate({op},post,options,snapshot);
  check(result.ok==(x=="1.000"),"actual postprocessed NC respects strict cutter envelope");
  check(result.ok || result.snapshot.gcodeText.isEmpty(),"rejected final motion exposes no partial program");
 }
 std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
