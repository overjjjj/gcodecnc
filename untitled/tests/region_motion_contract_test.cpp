#include "../src/workspaces/program_verification/generation/RegionToolpathValidation.h"
#include <QCoreApplication>
#include <iostream>
#include <cstdlib>
static void check(bool ok,const char*m){if(!ok){std::cerr<<"FAIL "<<m<<std::endl;std::exit(1);}}
int main(int argc,char**argv){QCoreApplication app(argc,argv);ContourFeature f;f.depth=3;f.points={{0,0,0},{20,0,0},{20,5,0},{5,5,0},{5,20,0},{0,20,0}};StrategyParams p;p.set("stockToLeave",.3);p.set("feedHeight",3);ToolMove m;m.from={2,15,-1};m.to={15,2,-1};
check(!regionMoveError(m,f,p,2).isEmpty(),"concave crossing rejected although both endpoints lie inside");
m.from={2,2,-1};m.to={18,2,-1};check(regionMoveError(m,f,p,2).isEmpty(),"full straight cutter sweep fits");
m.isRapid=true;check(!regionMoveError(m,f,p,2).isEmpty(),"low rapid traverse rejected");m.from.setZ(3);m.to.setZ(3);check(regionMoveError(m,f,p,2).isEmpty(),"safe-plane traverse allowed");
f.points={{-10,-2,0},{10,-2,0},{10,2,0},{-10,2,0}};SimulationController parser;parser.loadGCode("G17 G90 G21\nG0 Z3\nG0 X-5 Y0\nG1 Z-1 F100\nG2 X5 Y0 I5 J0\nG0 Z3",false);bool rejected=false,sagitta=false;for(const auto&move:parser.moves()){if(move.chordDeviation>0){sagitta=true;rejected|=!regionMoveError(move,f,p,2).isEmpty();}}check(rejected&&sagitta,"original arc bulge and analytic chord error checked");
f.points={{0,0,0},{20,0,0},{20,20,0},{0,20,0}};f.islands={{{8,8,0},{12,8,0},{12,12,0},{8,12,0}}};m.isRapid=false;m.from={2,10,-1};m.to={18,10,-1};check(!regionMoveError(m,f,p,2).isEmpty(),"complete sweep crosses island despite valid endpoints");
std::cout<<"PASS whole-segment concave/island envelope, arc deviation, safe/low rapid"<<std::endl;return 0;}
