#include "src/import/StepImporter.h"
#include "src/workspaces/process_design/selection/PocketOpening.h"
#include "src/workspaces/process_design/operations/MachiningInput.h"
#include "src/strategies/mill/PocketFinishStrategy.h"
#include "src/workspaces/program_verification/generation/RegionToolpathValidation.h"
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);StepImporter importer;
 if(!importer.load(QString::fromUtf8("D:/codex/home/worktrees/5a59/gcodecnc/untitled/测试模型/验收/特征板.STEP")))return 2;
 for(const auto &loop:importer.mesh().boundaryLoops) {
  if(loop.outer || loop.faceId!="face:113" || loop.orderedEdgeIds.size()!=18)continue;
  SelectionBoundaryLoop proof;MachiningFeature feature;QString error;
  if(!verifyPocketOpening(importer.mesh(),loop,&proof,&feature,&error)) {
   std::cout<<"OPENING "<<error.toStdString()<<std::endl;return 3;
  }
  QJsonArray points;for(const auto &p:proof.machiningBoundary)points.append(QJsonArray{p.x(),p.y(),p.z()});
  QDir().mkpath(QString::fromLocal8Bit(argv[1]));QFile file(QString::fromLocal8Bit(argv[1])+"/real-core-points.json");
  if(!file.open(QIODevice::WriteOnly))return 4;file.write(QJsonDocument(points).toJson());file.close();
  auto contour=machiningContour(feature);contour.depth=50;
  PocketFinishStrategy strategy;auto params=strategy.defaultParams();
  params.set("depth",50);params.set("entryMode",0);params.set("safeHeight",58);params.set("feedHeight",53);
  params.set("stepDown",10);params.set("sideStockToLeave",.1);params.set("floorStockToLeave",0);
  ToolEntry tool;tool.id=963;tool.type="end_mill";tool.diameter=6;tool.shankDia=6;tool.fluteLen=60;tool.totalLen=100;
  const auto result=strategy.generate(contour,tool,params);
  std::cout<<"REAL_WALL_GENERATE ok="<<result.ok<<" points="<<contour.points.size()<<" error="<<result.errorMsg.toStdString()<<std::endl;
  if(!result.ok)return 1;
  QFile nc(QString::fromLocal8Bit(argv[1])+"/real-wall.nc");
  if(!nc.open(QIODevice::WriteOnly))return 6;nc.write(result.gcode.toUtf8());nc.close();
  if(!nc.open(QIODevice::ReadOnly))return 7;
  SimulationController parser;parser.loadGCode("G17 G90 G94\n"+QString::fromUtf8(nc.readAll()),true);
  if(!parser.motionError().isEmpty())return 8;
  int checked=0;bool rejected=false;
  for(const auto &move:parser.moves()) {
   const auto issue=regionMoveError(move,contour,params,tool.diameter);
   if(!issue.isEmpty()){std::cout<<"REGION "<<issue.toStdString()<<std::endl;return 9;}
   ++checked;
   if(!move.isRapid && move.fromKnownAxes==7 && move.from.z()<contour.center.z()) {
    auto collision=move;collision.to.setX(contour.points.first().x());collision.to.setY(contour.points.first().y());
    rejected=rejected || !regionMoveError(collision,contour,params,tool.diameter).isEmpty();
   }
  }
  if(!rejected)return 10;
  if(PocketFinishStrategy::generateRegionPass(contour,tool,params,false).ok)return 11;
  std::cout<<"PASS real wall disk segments="<<checked<<" boundary collision refused; shared floor scope unchanged"<<std::endl;
  return 0;
 }
 return 5;
}
