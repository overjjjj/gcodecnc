#include "../src/strategies/mill/PocketRoughingStrategy.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include <QCoreApplication>
#include <iostream>
#include <limits>
#include <cmath>
int main(int argc,char **argv){QCoreApplication app(argc,argv);int failures=0;PocketRoughingStrategy strategy;
 ToolEntry tool;tool.id=1;tool.type="end_mill";tool.diameter=2;tool.fluteLen=30;tool.totalLen=60;
 struct Case {double top,depth,expected;};
 for(const auto &c:{Case{0,.6,-.6},Case{0,1.2,-1.2},Case{.25,.6,-.35},Case{-.25,.6,-.85},Case{.25,.6004,-.35},Case{-.25,.6004,-.85},Case{1.25,.6004,.65}}) {
  ContourFeature f;f.subType="verified_slot_opening";f.center={0,0,float(c.top)};f.depth=c.depth;f.length=10;f.width=10;
  f.points={{0,0,float(c.top)},{10,0,float(c.top)},{10,10,float(c.top)},{0,10,float(c.top)}};
  auto params=strategy.defaultParams();params.set("depth",c.depth);params.set("entryMode",0);params.set("stockToLeave",0);params.set("stepDown",2);
  const auto result=strategy.generate(f,tool,params);SimulationController parser;parser.loadGCode(result.gcode,false);
  double minimum=std::numeric_limits<double>::infinity();for(const auto &move:parser.moves())if(!move.isRapid&&move.toKnownAxes==7)minimum=std::min(minimum,move.to.z());
  const bool pass=result.ok && parser.motionError().isEmpty() && std::abs(minimum-c.expected)<1e-12 && minimum>=c.top-c.depth;
  std::cout<<"DEPTH top="<<c.top<<" depth="<<c.depth<<" actual="<<minimum<<" expected="<<c.expected<<" pass="<<pass<<std::endl;if(!pass)++failures;
 }
 std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
