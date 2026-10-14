#include "../src/import/StepImporter.h"
#include "../src/workspaces/process_design/selection/PocketOpening.h"
#include <QCoreApplication>
#include <iostream>
int main(int argc,char **argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;StepImporter importer;if(!importer.loadWithRotation(QString::fromLocal8Bit(argv[1]),QQuaternion()))return 3;
 int failures=0;
 for(const auto &loop:importer.mesh().boundaryLoops)if(!loop.outer && (loop.faceIndex==113 && (loop.orderedEdgeIds.contains("edge:452") || loop.orderedEdgeIds.contains("edge:460")))) {
  SelectionBoundaryLoop proof;QString error;const bool accepted=verifyPocketOpening(importer.mesh(),loop,&proof,nullptr,&error);
  std::cout<<"OPENING face="<<loop.faceIndex<<" edges="<<loop.orderedEdgeIds.size()<<" first="<<loop.orderedEdgeIds.first().toStdString()<<" accepted="<<accepted<<" error="<<error.toStdString()<<" role="<<int(proof.machiningRole)<<" depth="<<proof.maximumDepth<<std::endl;
  const bool expected=!loop.orderedEdgeIds.contains("edge:460");if(accepted!=expected)++failures;
 }
 std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
