#include "src/import/StepImporter.h"
#include "src/workspaces/process_design/selection/PocketOpening.h"
#include <QCoreApplication>
#include <iostream>
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);StepImporter importer;
 if(!importer.load(QString::fromUtf8("D:/codex/home/worktrees/5a59/gcodecnc/untitled/测试模型/验收/特征板.STEP")))return 2;
 for(const auto &loop:importer.mesh().boundaryLoops) {
  if(loop.outer || loop.faceId!="face:113" || loop.orderedEdgeIds.size()!=18)continue;
  SelectionBoundaryLoop proof;MachiningFeature feature;QString error;
  if(!verifyPocketOpening(importer.mesh(),loop,&proof,&feature,&error)) {
   std::cout<<"FAIL real 18-edge slot opening: "<<error.toStdString()<<std::endl;return 1;
  }
  if(proof.consumedBoundaryBound>.0005 || proof.sourceBoundaryBound>.0005)return 3;
  if(!openingEvidenceSupported(proof)) {
   std::cout<<"FAIL downstream opening evidence: "<<proof.candidateRule.toStdString()<<std::endl;return 5;
  }
  std::cout<<"PASS real 18-edge source core, unchanged total budget; consumed="<<proof.consumedBoundaryBound<<" depth="<<proof.maximumDepth<<" points="<<proof.machiningBoundary.size()<<std::endl;
  std::cout<<"SUPPORT "<<proof.supportFaceIds.join(",").toStdString()<<std::endl;
  double depth=0,z=0;bool flat=verifyPocketFlatFloor(importer.mesh(),loop,proof,&depth,&error,&z);
  std::cout<<"FLOOR flat="<<flat<<" depth="<<depth<<" z="<<z<<" error="<<error.toStdString()<<std::endl;
  bool through=verifyPocketThroughExit(importer.mesh(),loop,proof,&depth,&error);
  std::cout<<"THROUGH ok="<<through<<" depth="<<depth<<" error="<<error.toStdString()<<std::endl;
  if(flat || !through || std::abs(depth-50)>.002)return 6;
  return 0;
 }
 return 4;
}
