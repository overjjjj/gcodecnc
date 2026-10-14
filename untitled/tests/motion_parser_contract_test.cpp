#include <QCoreApplication>
#include <iostream>
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/workspaces/program_verification/generation/RegionToolpathValidation.h"
int main(int argc,char**argv){QCoreApplication app(argc,argv);int failures=0;auto check=[&](bool ok,const char*m){std::cout<<(ok?"PASS ":"FAIL ")<<m<<std::endl;failures+=!ok;};SimulationController parser;
 const QString start="G17 G90 G21\nG0 Z50\nG0 X-5 Y0\nG1 Z9 F100\n";
 parser.loadGCode(start+"G2 X5 Y0 I5 J0\nG0 Z50",false);
 check(parser.motionError().isEmpty()&&!parser.moves().isEmpty(),"G17 relative IJ is supported");
 check(parser.moves().first().fromKnownAxes==0 && parser.moves().first().toKnownAxes==4 && parser.moves()[1].fromKnownAxes==4 && parser.moves()[1].toKnownAxes==7,"initial XYZ knowledge is explicit and evolves independently");
 ContourFeature feature;feature.center={0,0,10};feature.depth=3;feature.points={{-20,-20,10},{20,-20,10},{20,20,10},{-20,20,10}};StrategyParams params;params.set("feedHeight",3);
 for(const auto &program:QStringList{"G0 X-5 Y0 Z50","G1 Z50 F100","G0 Z11","G0 Z6"}){
  parser.loadGCode(program,false);check(!parser.moves().isEmpty()&&!regionMoveError(parser.moves().first(),feature,params,2).isEmpty(),"unknown start cannot certify lateral/feed/low-plane motion");
 }
 for(const auto &bad:QStringList{"G2 X5 Y0 R5","G3 X5 Y0","G2 X5 Y0 I0 J0","G18 G2 X5 Z9 I5 K0","G19 G3 Y1 Z9 J1 K0","G90.1 G2 X5 Y0 I0 J0","G91 G1 X5","G20 G1 X5","G123 G1 X5"}){
  parser.loadGCode(start+bad,false);check(!parser.motionError().isEmpty()&&parser.moves().isEmpty()&&parser.toolPath().isEmpty(),qPrintable("no fabricated path: "+bad));parser.play();check(!parser.isRunning(),"invalid motion cannot play stale path");
 }
 for(const auto &cycle:QStringList{"G81","G82","G83","G84","G85","G73","G76","G86"}){
  parser.loadGCode("G17 G90 G21\nG0 Z20\nG0 X1 Y2\nG98 "+cycle+" X1 Y2 Z-5 R3 Q1 F100\nX3 Y4\nG80\nM30",false);
  bool cut=false;for(const auto&m:parser.moves())cut|=!m.isRapid&&m.to.z()==-5;
  check(parser.motionError().isEmpty()&&cut,qPrintable("CAM cycle remains supported: "+cycle));
 }
 parser.loadGCode("G0 Z20\nMCALL CYCLE81(20,0,3,-5)\nX1 Y2\nMCALL\nM30",false);bool cut=false;for(const auto&m:parser.moves())cut|=!m.isRapid&&m.to.z()==-5;
 check(parser.motionError().isEmpty()&&cut,"Siemens MCALL survives no-tool parsing used by final validation");
 parser.loadGCode(start+"G91 G28 Z0\nG90\nG0 Z50\nG0 X10 Y0",false);check(parser.motionError().isEmpty(),"zero-increment G28 remains a discontinuity");
 const QString afterHole="G90 G17\nG0 Z10\nG0 X-5 Y0\nG81 Z-1 R3 F100\n";
 for(bool projectTools:{true,false}){
  parser.loadGCode("G0 Z50\nG2 X10 Y0 I5 J0",projectTools);
  check(!parser.motionError().isEmpty()&&parser.moves().isEmpty(),"IJ arc cannot establish an unknown XY start by interpolation");
  parser.loadGCode("G0 Z10\nG0 X-5 Y0\nG81Z-1R3F100\nG2 X5 Y0 I5 J0",projectTools);
  bool compactCut=false,compactArc=false;
  for(const auto &m:parser.moves()){compactCut|=m.sourceLine==2&&!m.isRapid&&m.to.z()==-1;compactArc|=m.sourceLine==3&&std::abs(m.to.y())>0.1&&m.to.z()==10;}
  check(parser.motionError().isEmpty()&&compactCut&&compactArc,"compact Fanuc cycle has identical motion semantics");
  for(const auto &bad:QStringList{"G2 X5 Y0 R5","G2 X5 Y0"}){
   parser.loadGCode("G0 Z20\nG0 X-5 Y0\nMCALL CYCLE81(20,0,3,-5)\n"+bad+"\nMCALL",projectTools);
   check(!parser.motionError().isEmpty()&&parser.moves().isEmpty(),"MCALL cannot swallow unsupported arc positioning");
  }
  for(const auto &bad:QStringList{"G2 X5 Y0 R5","G2 X5 Y0"}){
   parser.loadGCode(afterHole+bad+"\nG80",projectTools);
   check(!parser.motionError().isEmpty()&&parser.moves().isEmpty(),qPrintable(QString("Fanuc cycle switches to rejected arc, projectTools=%1: %2").arg(projectTools).arg(bad)));
  }
  parser.loadGCode(afterHole+"G2 X5 Y0 Z9 I5 J0\nG80",projectTools);
  int curved=0;bool arcEnd=false;
  for(const auto &m:parser.moves())if(m.sourceLine==4){curved+=std::abs(m.to.y())>0.1;arcEnd|=m.to==QVector3D(5,0,9);}
  check(parser.motionError().isEmpty()&&curved>2&&arcEnd,"Fanuc cycle switches to real IJ helix");
  parser.loadGCode("G0 Z20\nMCALL CYCLE81(20,0,3,-5)\nG0 X1 Y2\nG0 X3 Y4\nMCALL\nM30",projectTools);
  int bottoms=0;for(const auto &m:parser.moves())bottoms+=!m.isRapid&&m.to.z()==-5;
  check(parser.motionError().isEmpty()&&bottoms==2,"Siemens MCALL stays active across explicit positioning");
 }
 QVector<QVector3D> emittedPath,emittedStarts,positions;bool collision=false;
 QObject::connect(&parser,&SimulationController::toolPathReady,[&](const QVector<QVector3D>&p,const QVector<bool>&,const QVector<QVector3D>&s){emittedPath=p;emittedStarts=s;});
 QObject::connect(&parser,&SimulationController::toolPositionChanged,[&](QVector3D p){positions.append(p);});
 QObject::connect(&parser,&SimulationController::checkResultChanged,[&](){collision=parser.checkResult().state==SimulationCheckState::Collision;});
 MeshData mesh;mesh.bbMin={-2,-2,-1};mesh.bbMax={2,2,10};Triangle tri;tri.v0={-2,-2,10};tri.v1={2,-2,10};tri.v2={0,2,10};mesh.triangles.append(tri);parser.setMesh(mesh);
 for(bool projectTools:{true,false}){
  positions.clear();parser.loadGCode("G0 Z50\nG0 X-5 Y0\nG1 Z9",projectTools);
  check(!collision && parser.checkResult().state!=SimulationCheckState::Complete,"unknown initial placeholder is excluded without claiming a complete check");
  check(emittedPath==QVector<QVector3D>{{-5,0,50},{-5,0,9}}&&emittedStarts==QVector<QVector3D>{{-5,0,50}},"render only fully known segment, retaining actual cut");
  positions.clear();parser.seekToGCodeLine(0);check(positions.isEmpty(),"seek never emits partially known XYZ");
  parser.stop();positions.clear();parser.play();QMetaObject::invokeMethod(&parser,"onTick",Qt::DirectConnection);
  check(positions.isEmpty(),"tick never emits partially known XYZ");parser.pause();
  parser.loadGCode("G0 Z50\nG0 X-5 Y0\nG1 Z9\nG91 G28 Z0\nG90\nG0 X5 Y0\nG0 Z50\nG1 Z9",projectTools);
  check(emittedStarts==QVector<QVector3D>{{-5,0,50},{5,0,50}},"G28 breaks displayed segments in both parser modes");
 }
 return failures?1:0;}
