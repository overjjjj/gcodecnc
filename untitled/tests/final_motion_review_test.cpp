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
class AlteredPost : public Cq8PostProcessor {
public:
 QString mode;
 QString wrapGCode(const QStringList &blocks,const PostProcessorOptions &options)const override {
  auto text=Cq8PostProcessor::wrapGCode(blocks,options);
  if(mode=="missing")text.replace("; ---- Operation 1 [op:motion-review] ----","");
  if(mode=="unknown")text.replace("[op:motion-review]","[op:ghost]");
  if(mode=="moved") {
   const QString second="; ---- Operation 2 [op:ordinary-contour] ----";
   text.replace(second+"\n","");
   text.replace("; ---- Operation 1 [op:motion-review] ----", "; ---- Operation 1 [op:motion-review] ----\n"+second);
  }
  return text;
 }
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
 const QString unsafe=prefix+"G1 X50 Y0 F100"+suffix;
 expect(!generate(unsafe).ok,"normal owner runs final boundary rejection");
 for(const auto &id:QStringList{"slope]","slope[","slope\nother","slope]\n; [op:ghost]"}) {
  op.id=id;expect(!generate(unsafe).ok,qPrintable("unsafe NC rejected with malformed ID: "+id));
 }
 op.id="motion-review";
 expect(!generate(prefix+"; [op:ghost]\nG1 X50 Y0 F100"+suffix).ok,"forged unknown owner cannot disable final check");
 strategy->text="T1 M6\nS1200 M3\nM8\n"+unsafe;
 for(const auto &mode:QStringList{"missing","unknown"}) {
  AlteredPost altered;altered.mode=mode;
  expect(!service.generate({op},altered,options,snapshot).ok,qPrintable("final text owner "+mode+" is rejected"));
 }
 strategy->text="T1 M6\nS1200 M3\nM8\n"+prefix+"G1 X5 Y0 F100"+suffix;
 auto nonRegion=op;nonRegion.id="ordinary-contour";nonRegion.strategyId="mill_closed_contour";
 nonRegion.params.set("compensation",0);
 expect(service.generate({nonRegion,op,nonRegion},post,options,snapshot).ok==false,"duplicate IDs still rejected");
 auto after=nonRegion;after.id="ordinary-after";
 const auto mixed=service.generate({nonRegion,op,after},post,options,snapshot);
 std::cout<<mixed.errors.join(" | ").toStdString()<<std::endl;
 expect(mixed.ok,"known non-region operations before and after region remain valid");
 auto headerOptions=options;headerOptions.safeStartBlocks<<"G0 Z50";
 expect(service.generate({op},post,headerOptions,snapshot).ok,"header may establish safe Z before first operation");
 headerOptions.safeStartBlocks.last()="G0 Z1";
 expect(!service.generate({op},post,headerOptions,snapshot).ok,"unowned low header motion is rejected");
 strategy->text="T1 M6\nS1200 M3\nM8\n"+prefix+"; [op:ordinary-contour]\nG1 X50 Y0 F100"+suffix;
 expect(!service.generate({op,nonRegion},post,options,snapshot).ok,"forged known non-region owner rejected");
 strategy->text="T1 M6\nS1200 M3\nM8\n"+unsafe;
 AlteredPost moved;moved.mode="moved";
 expect(!service.generate({op,nonRegion},moved,options,snapshot).ok,"moved genuine non-region header cannot steal region motions");
 std::cout<<"Failures="<<failures<<std::endl;return failures?1:0;
}
