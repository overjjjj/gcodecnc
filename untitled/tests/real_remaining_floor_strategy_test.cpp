#include "src/strategies/mill/SlopedFloorStrategy.h"
#include "src/workspaces/process_design/selection/SlopedFloorBinding.h"
#include "src/workspaces/process_design/operations/MachiningInput.h"
#include "src/workspaces/program_verification/simulation/SimulationController.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <iostream>
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);StepImporter importer;
 if(!importer.load(QString::fromUtf8("D:/codex/home/worktrees/5a59/gcodecnc/untitled/测试模型/验收/特征板.STEP")))return 2;
 const auto &mesh=importer.mesh();const QString out=QString::fromLocal8Bit(argv[1]);QDir().mkpath(out);
 int failures=0;
 for(int face:{340,344}) {
  SelectionChain selection;selection.explicitUserSelection=true;selection.toolAxis={0,0,1};
  SelectionTopologyRef ref;ref.kind=SelectionTopologyKind::Face;ref.id=QString("face:%1").arg(face);selection.orderedTopologyRefs={ref};
  for(const auto &loop:mesh.boundaryLoops)if(loop.faceId==ref.id)
   selection.boundaryLoops.append({loop.faceId,loop.outer,loop.orderedEdgeIds,loop.orderedForward});
  const auto bound=bindSlopedFloor(mesh,selection);
  if(!bound.error.isEmpty()){std::cout<<"BIND "<<face<<" "<<bound.error.toStdString()<<std::endl;++failures;continue;}
  SlopedFloorStrategy strategy;auto params=strategy.defaultParams();
  params.set("entryMode",0);params.set("safeHeight",10);params.set("stepover",.2);params.set("sideStockToLeave",.05);
  ToolEntry tool;tool.id=963;tool.type="end_mill";tool.diameter=1;tool.shankDia=1;tool.fluteLen=5;tool.totalLen=20;
  const auto result=strategy.generate(machiningContour(bound.feature),tool,params);
  if(!result.ok){std::cout<<"GENERATE "<<face<<" "<<result.errorMsg.toStdString()<<std::endl;++failures;continue;}
  QFile file(out+QString("/face-%1.nc").arg(face));if(!file.open(QIODevice::WriteOnly))return 3;
  file.write(result.gcode.toUtf8());file.close();if(!file.open(QIODevice::ReadOnly))return 4;
  SimulationController parser;parser.loadGCode(QString::fromUtf8(file.readAll()),true,0,{});
  if(!parser.motionError().isEmpty())return 5;
  int rows=0,sweeps=0;bool rejected=false;
  for(const auto &move:parser.moves()) {
   if(move.fromKnownAxes!=7 || move.toKnownAxes!=7) {
    if(!move.isRapid) {rejected=true;break;}
    continue;
   }
   const double clearance=bound.sourceTopZ+params.get("feedHeight");
   if(move.isRapid && std::min(move.from.z(),move.to.z())>=clearance)continue;
   const auto error=slopedFloorMoveError(mesh,face,move.from,move.to,tool);
   if(!error.isEmpty()) {
    std::cout<<"FAIL actual sweep face="<<face<<" line="<<move.sourceLine<<" "<<error.toStdString()<<std::endl;
    rejected=true;break;
   }
   ++sweeps;if(!move.isRapid && move.from.x()!=move.to.x())++rows;
  }
  std::cout<<"FACE "<<face<<" rows="<<rows<<" sweeps="<<sweeps<<" rejected="<<rejected<<std::endl;
  if(rejected || rows==0)++failures;
  const auto edge=bound.sourceOuterBoundary.first();
  Coordinate3D unsafe(edge.x(),edge.y(),edge.z()+.02);
  const auto edgeError=slopedFloorMoveError(mesh,face,unsafe,unsafe,tool);
  if(edgeError.isEmpty()){std::cout<<"FAIL source trim collision admitted"<<std::endl;++failures;}
  auto incomplete=selection;incomplete.boundaryLoops.first().orderedEdgeIds.removeLast();
  if(bindSlopedFloor(mesh,incomplete).error.isEmpty()){std::cout<<"FAIL incomplete source wire admitted"<<std::endl;++failures;}
  std::cout<<"NEGATIVE face="<<face<<" edge="<<edgeError.toStdString()<<std::endl;
 }
 std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
