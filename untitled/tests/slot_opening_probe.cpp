#include <QCoreApplication>
#include <QSet>
#include <iostream>
#include "../src/import/StepImporter.h"
#include "../src/workspaces/process_design/selection/PocketOpening.h"
static void point(QVector3D p){std::cout<<p.x()<<","<<p.y()<<","<<p.z();}
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 2;
 StepImporter importer;if(!importer.loadWithRotation(QString::fromLocal8Bit(argv[1]),QQuaternion::fromAxisAndAngle(1,0,0,180)))return 3;
 const auto &mesh=importer.mesh();std::cout<<"ROTATED 180X normalized bounds ";point(mesh.bbMin);std::cout<<" / ";point(mesh.bbMax);std::cout<<"\n";
 for(const auto &loop:mesh.boundaryLoops){if(loop.faceIndex!=98||loop.outer||loop.orderedEdgeIds.size()!=4)continue;
  SelectionBoundaryLoop proof;MachiningFeature feature;
  const bool verified=verifyPocketOpening(mesh,loop,&proof,&feature);
  std::cout<<"VERIFIED="<<verified<<" maximumDepth="<<proof.maximumDepth<<"\n";
  std::cout<<"LOOP face="<<loop.faceIndex<<" outer="<<loop.outer<<"\n";QSet<int> walls;
  for(const auto &id:loop.orderedEdgeIds)for(const auto &e:mesh.pickEdges)if(e.id==id){
   std::cout<<"EDGE "<<id.toStdString()<<" ";point(e.points.first());std::cout<<" -> ";point(e.points.last());std::cout<<" adjacent";
   for(int f:e.adjacentFaceIndices){std::cout<<" "<<f;if(f!=98)walls.insert(f);}std::cout<<"\n";
  }
  for(int f:walls){QVector3D lo(1e9,1e9,1e9),hi(-1e9,-1e9,-1e9),normal;for(const auto &t:mesh.triangles)if(t.faceIndex==f){normal=t.normal;for(auto p:{t.v0,t.v1,t.v2})for(int a=0;a<3;++a){lo[a]=qMin(lo[a],p[a]);hi[a]=qMax(hi[a],p[a]);}}
   std::cout<<"WALL "<<f<<" bounds ";point(lo);std::cout<<" / ";point(hi);std::cout<<" normal ";point(normal);std::cout<<" adjacent";
   QSet<int> other;for(const auto &e:mesh.pickEdges)if(e.adjacentFaceIndices.contains(f))for(int n:e.adjacentFaceIndices)if(n!=f)other.insert(n);for(int n:other)std::cout<<" "<<n;std::cout<<"\n";
  }
 }
 for(int face:{98,376}){QSet<QString> normals;for(const auto &t:mesh.triangles)if(t.faceIndex==face)normals.insert(QString("%1,%2,%3").arg(t.normal.x()).arg(t.normal.y()).arg(t.normal.z()));std::cout<<"FACE "<<face<<" normals="<<normals.values().join(";").toStdString()<<"\n";for(const auto &l:mesh.boundaryLoops)if(l.faceIndex==face){std::cout<<"FACELOOP "<<face<<" outer="<<l.outer<<" edges="<<l.orderedEdgeIds.size()<<"\n";if(face==376)for(const auto &id:l.orderedEdgeIds)for(const auto &e:mesh.pickEdges)if(e.id==id){std::cout<<id.toStdString()<<" ";point(e.points.first());std::cout<<" -> ";point(e.points.last());std::cout<<"\n";}}}
 for(int i=0;i<importer.features().size();++i){const auto &f=importer.features()[i];if(f.kind!=FeatureKind::Slot&&f.kind!=FeatureKind::Pocket)continue;
  std::cout<<"FEATURE "<<i<<" "<<f.subType.toStdString()<<" center ";point(f.center);std::cout<<" axis ";point(f.axis);std::cout<<" LWD="<<f.length<<","<<f.width<<","<<f.depth<<" faces";for(int n:f.faceIndices)std::cout<<" "<<n;std::cout<<"\n";
 }
 return 0;
}
