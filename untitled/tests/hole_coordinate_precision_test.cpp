#include "../src/import/StepImporter.h"
#include "../src/core/MachiningCoordinates.h"
#include "../src/strategies/StrategyFactory.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/gcode/ProgramSnapshotStatus.h"
#include <BRepAdaptor_Surface.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>
#include <iomanip>
#include <cmath>
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 2;int failures=0;
 auto check=[&](bool ok,const char *label){if(!ok){++failures;std::cerr<<label<<'\n';}};
 StepImporter initial;if(!initial.loadWithRotation(QString::fromLocal8Bit(argv[1]),{}))return 3;
 QVector3D axis;for(const auto &f:initial.features())if(f.region==FaceRegion::Front&&f.radius>=2&&f.depth>=7){axis=f.axis;break;}
 StepImporter importer;if(!importer.loadWithRotation(QString::fromLocal8Bit(argv[1]),QQuaternion::rotationTo(axis,{0,0,axis.z()<0?-1.f:1.f})))return 4;
 HoleFeature hole;bool found=false;for(const auto &f:importer.features())if(f.region==FaceRegion::Front&&f.radius>=2&&f.depth>=7&&std::hypot(f.axis.x(),f.axis.y())<1e-6){hole=f;found=true;break;}if(!found)return 5;
 TopTools_IndexedMapOfShape faces;TopExp::MapShapes(importer.mesh().sourceShape,TopAbs_FACE,faces);
 double exactX=0,exactY=0;bool cylinder=false;
 for(int id:hole.faceIndices){BRepAdaptor_Surface surface(TopoDS::Face(faces(id)));if(surface.GetType()!=GeomAbs_Cylinder)continue;
  const auto a=surface.Cylinder().Axis();const double t=(double(hole.center.z())-a.Location().Z())/a.Direction().Z();
  exactX=a.Location().X()+t*a.Direction().X();exactY=a.Location().Y()+t*a.Direction().Y();cylinder=true;break;}
 if(!cylinder)return 6;
 std::cout<<std::setprecision(17)<<"CAD "<<exactX<<","<<exactY<<" FEATURE "<<hole.center.x()<<","<<hole.center.y()<<std::endl;
 check(std::abs(double(hole.center.x())-exactX)<1e-9&&std::abs(double(hole.center.y())-exactY)<1e-9,"recognized center must preserve CAD double precision");
 ToolEntry tool;tool.id=989;tool.type="drill";tool.diameter=4;tool.fluteLen=80;tool.totalLen=120;
 MachiningOperation op;op.id="precision";op.opType=OperationType::Hole;op.strategyId="hole_g81";op.toolId=tool.id;op.holeFeature=hole;
 op.params=StrategyFactory::instance().strategy(op.strategyId)->defaultParams();op.params.set("depth",5);
 ProgramGenerationService service([](const QString &id){return StrategyFactory::instance().strategy(id);},[&](int){return tool;});
 Cq8PostProcessor cq8;SiemensPostProcessor siemens;PostProcessorOptions options;
 SetupOrigin setup;setup.confirmed=true;
 SetupOrigin holeOrigin;holeOrigin.anchor=SetupOriginAnchor::CustomPoint;holeOrigin.confirmed=true;
 holeOrigin.customPoint={hole.center.x(),hole.center.y(),hole.center.z()};
 const auto zero=inWorkCoordinates(hole,holeOrigin.resolvedPoint(importer.mesh().bbMin,importer.mesh().bbMax));
 check(zero.center.isNull(),"choosing the exact hole center as origin must produce XYZ zero");
 for(const auto &origin:QVector<Coordinate3D>{setup.resolvedPoint(importer.mesh().bbMin,importer.mesh().bbMax),Coordinate3D(1000000.123456789,-2000000.234567891,0.345678912)}){
  const auto work=inWorkCoordinates(hole,origin);
  check(std::abs(double(work.center.x())-(double(hole.center.x())-double(origin.x())))<1e-10,"work-coordinate subtraction must remain double");
  for(const PostProcessorBase *post:QVector<const PostProcessorBase*>{&cq8,&siemens}){
   ProgramGenerationSnapshotOptions snapshot;snapshot.modelOrigin=origin;
   const auto result=service.generate({op},*post,options,snapshot);check(result.ok,"precision program generation");if(!result.ok)continue;
   SimulationController sim;sim.loadGCode(result.snapshot.gcodeText,false);check(sim.motionError().isEmpty(),"final program parses");
   bool cutting=false;for(const auto &mv:sim.moves())if(!mv.isRapid&&mv.toKnownAxes==7){cutting=true;
    check(std::abs(double(mv.to.x())-double(work.center.x()))<=5.1e-7,"final NC preserves work center within six-decimal rounding");
    const auto model=mv.to+origin;
    check(std::abs(double(model.x())-exactX)<=5.2e-7&&std::abs(double(model.y())-exactY)<=5.2e-7,"parsed NC plus origin must recover CAD center within rounding bound");break;}
   check(cutting,"parsed final cutting move exists");
  }
 }
 SimulationController parse;parse.loadGCode("G90\nG0 X269.896419486 Y-12 Z50\nG1 Z-28.123456789",false);
 check(!parse.moves().isEmpty()&&std::abs(double(parse.moves().last().to.x())-269.896419486)<1e-10&&std::abs(double(parse.moves().last().to.z())+28.123456789)<1e-10,"motion parser retains decimal coordinates as double");
 QTemporaryDir temp;ProjectManager project;project.setFeatures({hole});project.setOperations({op});project.setSetupOrigin(holeOrigin);
 ProgramEntry program;program.id="origin-precision";program.modelOrigin=holeOrigin.resolvedPoint(importer.mesh().bbMin,importer.mesh().bbMax);program.coordinateVersion=1;program.coordinateEvidence=program.coordinateFingerprint();project.setPrograms({program});
 check(project.saveToFile(temp.filePath("precision.cnext")),"save precise feature");
 ProjectManager loaded;check(loaded.loadFromFile(temp.filePath("precision.cnext")),"load precise feature");
 check(!loaded.features().isEmpty()&&std::abs(double(loaded.features().first().center.x())-exactX)<1e-9,"source feature JSON preserves double center");
 check(!loaded.operations().isEmpty()&&std::abs(double(loaded.operations().first().holeFeature.center.x())-exactX)<1e-9,"operation JSON preserves double center");
 check(loaded.setupOrigin().customPoint.x()==hole.center.x()&&loaded.programs().first().modelOrigin.x()==hole.center.x()&&loaded.programs().first().coordinatesVerified(),"setup and program snapshot JSON preserve the same precise origin");
 auto shiftedOrigin=holeOrigin;shiftedOrigin.customPoint.setX(shiftedOrigin.customPoint.x()+1e-9);
 check(shiftedOrigin.fingerprint("G54")!=holeOrigin.fingerprint("G54"),"precise origin changes invalidate setup fingerprint");
 const QString fingerprint=ProgramSnapshotFingerprint::calculate({op},{op.id});
 auto changed=op;changed.holeFeature.center.setX(changed.holeFeature.center.x()+1e-9);
 check(ProgramSnapshotFingerprint::calculate({changed},{op.id})!=fingerprint,"sub-float center change invalidates operation fingerprint");
 MachiningOperation contour;contour.id="contour";contour.opType=OperationType::Contour;contour.contourFeature.center=hole.center;
 const QString contourFingerprint=ProgramSnapshotFingerprint::calculate({contour},{contour.id});
 auto changedContour=contour;changedContour.contourFeature.center.setX(changedContour.contourFeature.center.x()+1e-9);
 check(ProgramSnapshotFingerprint::calculate({changedContour},{contour.id})!=contourFingerprint,"contour fingerprint preserves the shared double center");
 const auto contourWork=inWorkCoordinates(contour.contourFeature,{1000000.125,-2000000.25,.375});
 check(std::abs(contourWork.center.x()-(exactX-1000000.125))<1e-10,"shared contour work coordinates preserve double subtraction");
 QFile file(temp.filePath("precision.cnext"));check(file.open(QIODevice::ReadOnly),"read current project fixture");
 auto document=QJsonDocument::fromJson(file.readAll()).object();file.close();
 auto features=document["features"].toArray();for(int i=0;i<features.size();++i){auto f=features[i].toObject();f.remove("coordinatePrecisionVersion");features[i]=f;}document["features"]=features;
 auto operations=document["operations"].toArray();for(int i=0;i<operations.size();++i){auto o=operations[i].toObject();auto f=o["holeFeature"].toObject();f.remove("coordinatePrecisionVersion");o["holeFeature"]=f;operations[i]=o;}document["operations"]=operations;
 check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"write legacy precision fixture");file.write(QJsonDocument(document).toJson());file.close();
 ProjectManager legacy;check(legacy.loadFromFile(file.fileName()),"load legacy precision fixture");
 check(!legacy.features().isEmpty()&&legacy.features().first().coordinatePrecisionVersion==0,"legacy source precision must remain unknown");
 check(!legacy.operations().isEmpty()&&legacy.operations().first().holeFeature.coordinatePrecisionVersion==0,"legacy operation precision must remain unknown");
 check(!service.generate(legacy.operations(),cq8,options,{}).ok,"legacy center evidence cannot silently regenerate machining output");
 auto malformedFeature=features[0].toObject();malformedFeature["coordinatePrecisionVersion"]=1;malformedFeature.remove("cx");features[0]=malformedFeature;document["features"]=features;
 auto malformedOperation=operations[0].toObject();auto malformedHole=malformedOperation["holeFeature"].toObject();malformedHole["coordinatePrecisionVersion"]=1;malformedHole.remove("cx");malformedOperation["holeFeature"]=malformedHole;operations[0]=malformedOperation;document["operations"]=operations;
 check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"write missing center fixture");file.write(QJsonDocument(document).toJson());file.close();
 ProjectManager missing;check(missing.loadFromFile(file.fileName()),"load missing center fixture");
 check(missing.features().first().coordinatePrecisionVersion==0&&missing.operations().first().holeFeature.coordinatePrecisionVersion==0,"missing coordinates must not masquerade as precise zero");
 const QVector<QPair<QString,QString>> modes={{"hole_spot","spot_drill"},{"hole_g81","drill"},{"hole_peck","drill"},{"hole_deephole","drill"},{"hole_g73","drill"},{"hole_g76","boring_bar"},{"hole_g86","boring_bar"},{"hole_tapping","tap"},{"hole_reaming","reamer"},{"hole_chamfer","chamfer_mill"}};
 for(const auto &mode:modes){
  auto cycleOp=op;cycleOp.strategyId=mode.first;cycleOp.params=StrategyFactory::instance().strategy(mode.first)->defaultParams();cycleOp.params.set("depth",5);cycleOp.params.set("axialDepth",5);cycleOp.params.set("chamferMode",1);cycleOp.params.set("peckDepth",2);
  ToolEntry cycleTool=tool;cycleTool.type=mode.second;cycleTool.diameter=(mode.second=="tap"||mode.second=="reamer")?hole.radius*2:3;cycleTool.pitch=hole.pitch>0?hole.pitch:1;cycleTool.pointAngle=90;cycleTool.extra.insert("angle",90);
  ProgramGenerationService cycles([](const QString &id){return StrategyFactory::instance().strategy(id);},[&](int){return cycleTool;});
  auto second=cycleOp;second.id="second";second.holeFeature.center.setX(hole.center.x()+.0002);
  ProgramGenerationSnapshotOptions snapshot;snapshot.modelOrigin={1000000.125,-2000000.25,.375};
  snapshot.machineProfile.controllerId="siemens-840d";snapshot.machineProfile.id="siemens-840d-pgz-1102";snapshot.machineProfile.version="sw6";
  for(const PostProcessorBase *post:QVector<const PostProcessorBase*>{&cq8,&siemens}){
   if(post==&siemens&&(mode.first=="hole_g76"||mode.first=="hole_g86"))continue;
   const auto result=cycles.generate({cycleOp,second},*post,options,snapshot);
   if(!result.ok)std::cerr<<mode.first.toStdString()<<": "<<result.errors.join(" | ").toStdString()<<std::endl;
   check(result.ok,"all supported fixed cycles generate precision coordinates");if(!result.ok)continue;
   SimulationController sim;sim.loadGCode(result.snapshot.gcodeText,false);check(sim.motionError().isEmpty(),"all fixed-cycle output parses");
   bool firstSeen=false,secondSeen=false;
   for(const auto &move:sim.moves())if(!move.isRapid&&move.toKnownAxes==7){
    const auto model=move.to+snapshot.modelOrigin;
    const bool first=std::abs(model.x()-hole.center.x())<=5.2e-7;
    const bool next=std::abs(model.x()-second.holeFeature.center.x())<=5.2e-7;
    check((first||next)&&std::abs(model.y()-hole.center.y())<=5.2e-7,"every cutting move preserves a distinct precise hole position");
    firstSeen|=first;secondSeen|=next;
   }
   check(firstSeen&&secondSeen,"close but distinct hole centers must not be deduplicated");
  }
 }
 std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
