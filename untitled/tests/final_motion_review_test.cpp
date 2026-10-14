#include <QCoreApplication>
#include <iostream>
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/generation/RegionToolpathValidation.h"
#include "../src/strategies/mill/PocketRoughingStrategy.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
class InjectedPocket : public PocketRoughingStrategy {
public:
 QString text;
 ToolpathResult generate(const ContourFeature &,const ToolEntry &,const StrategyParams &)const override {ToolpathResult r;r.ok=true;r.gcode=text;return r;}
};
int main(int argc,char**argv){QCoreApplication app(argc,argv);int failures=0;
 auto expect=[&](bool condition,const char*name){std::cout<<(condition?"PASS ":"FAIL ")<<name<<std::endl;if(!condition)++failures;};
 auto strategy=std::make_shared<InjectedPocket>();ToolEntry tool;tool.id=1;tool.type="end_mill";tool.diameter=2;tool.fluteLen=30;tool.totalLen=60;
 ProgramGenerationService service([&](const QString&){return strategy;},[&](int){return tool;});Cq8PostProcessor post;PostProcessorOptions options;options.workOffset="G54";options.safeStartBlocks=QStringList{"G17 G40 G49 G80","G21","G90","G54","G94"};ProgramGenerationSnapshotOptions snapshot;
 MachiningOperation op;op.id="motion-review";op.opType=OperationType::Contour;op.strategyId="mill_pocket_rough";op.toolId=1;op.params=strategy->defaultParams();op.params.set("depth",3);op.params.set("stockToLeave",.3);op.params.set("entryMode",0);op.params.set("feedHeight",3);op.params.set("safeHeight",50);
 op.contourFeature.subType="rectangular_pocket";op.contourFeature.axis={0,0,1};op.contourFeature.region=FaceRegion::Front;op.contourFeature.depth=3;op.contourFeature.length=20;op.contourFeature.width=4;op.contourFeature.points={{-10,-2,0},{10,-2,0},{10,2,0},{-10,2,0}};
 auto generate=[&](QString body){strategy->text="T1 M6\nS1200 M3\nM8\n"+body;auto result=service.generate({op},post,options,snapshot);if(!result.ok)std::cout<<result.errors.join(" | ").toStdString()<<std::endl;return result;};
 op.contourFeature.center.setZ(10);for(auto &p:op.contourFeature.points)p.setZ(10);
 expect(generate("G0 Z50\nG0 X-5 Y0\nG1 Z9 F100\nG1 X5 F100\nG0 Z50").ok,"unknown initial position permits standalone safe Z establishment at elevated top");
 expect(!generate("G0 Z50\nG0 X-5 Y0\nG1 Z6 F100\nG0 Z50").ok,"known excessive depth remains rejected before upward retract");
 op.contourFeature.center.setZ(0);for(auto &p:op.contourFeature.points)p.setZ(0);
 const QString prefix="G0 Z3\nG0 X-5 Y0\nG1 Z-1 F100\n",suffix="\nG0 Z3";
 expect(generate(prefix+"G1 X5 Y0 F100"+suffix).ok,"legitimate center line fixture generates");
 for(const auto &arc:QStringList{"G2 X5 Y0 R5","G2 X5 Y0","G18 G2 X5 Z-1 I5 K0","G19 G3 Y1 Z-1 J1 K0","G90.1 G2 X5 Y0 I0 J0"}){
  auto result=generate(prefix+arc+suffix);expect(!result.ok,qPrintable("unsupported motion rejected: "+arc));expect(result.ok || result.snapshot.gcodeText.isEmpty(),"rejection exposes no partial snapshot");
 }
 expect(!generate(prefix+"G2 X5 Y0 I5 J0"+suffix).ok,"supported IJ semicircle is still rejected for actual boundary crossing");
 // Known upward rapid from below the bottom must never use an initialization exemption.
 ToolMove known;known.from={0,0,-4};known.to={0,0,3};known.isRapid=true;
 expect(regionMoveError(known,op.contourFeature,op.params,2)=="REGION_TOOLPATH_DEPTH_EXCEEDED","known below-bottom rapid remains a depth violation");
 std::cout<<"Failures="<<failures<<std::endl;return failures?1:0;
}
