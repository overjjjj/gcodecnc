#include "../src/strategies/StrategyFactory.h"
#include "../src/workspaces/process_design/operations/OperationProposal.h"
#include "../src/workspaces/process_design/ui/OperationEditDialog.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/tool/ToolLibrary.h"
#include <QCoreApplication>
#include <iostream>
#include <limits>

int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);int failures=0;
 auto check=[&](bool ok,const QString &label){if(!ok){++failures;std::cerr<<label.toStdString()<<'\n';}};
 struct Case{const char *id;const char *type;double diameter;};
 const Case cases[]={{"hole_spot","spot_drill",5},{"hole_g81","drill",10},
 {"hole_deephole","drill",10},{"hole_g73","drill",10},{"hole_g76","boring_bar",3},
 {"hole_g86","boring_bar",3},{"hole_tapping","tap",10},{"hole_chamfer","chamfer_mill",5},
 {"hole_circular_mill","end_mill",5},{"hole_peck","drill",10},{"hole_reaming","reamer",10}};
 ToolEntry tool;tool.id=989;tool.pitch=1.25;tool.fluteLen=80;tool.totalLen=120;tool.pointAngle=90;tool.extra.insert("angle",90);
 Cq8PostProcessor post;PostProcessorOptions options;ProgramGenerationSnapshotOptions snapshot;
 ProgramGenerationService service([](const QString &id){return StrategyFactory::instance().strategy(id);},[&](int){return tool;});
 for(const auto &c:cases){
  const QString id=c.id;auto strategy=StrategyFactory::instance().strategy(id);if(!strategy)return 2;
  tool.type=c.type;tool.diameter=c.diameter;ToolLibrary::instance().removeTool(tool.id);ToolLibrary::instance().addTool(tool);
  MachiningOperation op;op.id=id;op.opType=OperationType::Hole;op.strategyId=id;op.toolId=tool.id;
  op.holeFeature.radius=5;op.holeFeature.depth=20;op.holeFeature.center={20,30,-10};op.holeFeature.region=FaceRegion::Front;
  op.params=strategy->defaultParams();op.params.set("depth",5);op.params.set("stockToLeave",.2);
  auto verify=[&](const QVector3D &axis,bool valid){
   op.holeFeature.axis=axis;OperationProposal proposal;proposal.kind=OperationProposalKind::Hole;proposal.strategyId=id;
   proposal.toolId=tool.id;proposal.holeFeature=op.holeFeature;proposal.params=op.params;
   const QString label=id+QString(" axis(%1,%2,%3)").arg(axis.x()).arg(axis.y()).arg(axis.z());
   check(strategy->generate(op.holeFeature,tool,op.params).ok==valid,label+" strategy");
   check(OperationFactory::confirm(proposal,OperationConfirmationIntent::ExplicitUser,{}).ok==valid,label+" creation");
   check(operationEditError(op,op.params,tool.id).isEmpty()==valid,label+" edit");
   check(service.generate({op},post,options,snapshot).ok==valid,label+" service");
  };
  verify({0,0,1},true);verify({0,0,-1},true);verify({0,0,2},true);
  verify({-.104528f,0,-.994522f},false);verify({1,0,0},false);verify({0,0,0},false);
  verify({std::numeric_limits<float>::quiet_NaN(),0,1},false);
  verify({0,0,std::numeric_limits<float>::infinity()},false);
  op.holeFeature.region=FaceRegion::Side;verify({0,0,1},false);
  op.holeFeature.region=FaceRegion::Back;verify({0,0,-1},false);
  op.holeFeature.region=FaceRegion::Unknown;verify({0,0,1},false);
 }
 std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
