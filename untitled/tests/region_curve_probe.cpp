#include <QCoreApplication>
#include <QSet>
#include "../src/workspaces/process_design/selection/PocketOpening.h"
#include <iostream>
#include "../src/import/StepImporter.h"
#include <TopoDS.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
int main(int argc,char **argv){QCoreApplication app(argc,argv);StepImporter imp;if(argc!=2||!imp.loadWithRotation(QString::fromLocal8Bit(argv[1]),QQuaternion()))return 1;
for(int angle:{0,180}){StepImporter check;check.loadWithRotation(QString::fromLocal8Bit(argv[1]),QQuaternion::fromAxisAndAngle(1,0,0,angle));for(const auto &loop:check.mesh().boundaryLoops)if(loop.orderedEdgeIds.contains(angle==0?"edge:452":"edge:409")){SelectionBoundaryLoop proof;QString error;bool ok=verifyPocketOpening(check.mesh(),loop,&proof,nullptr,&error);std::cout<<"PROOF angle="<<angle<<" ok="<<ok<<" depth="<<proof.maximumDepth<<" error="<<error.toStdString()<<"\n";}}
const auto &m=imp.mesh();const auto &g=imp.topoGraph();QSet<int> faces;
for(const auto &l:m.boundaryLoops)if(l.faceIndex==113 && l.orderedEdgeIds.contains("edge:452")){std::cout<<"LOOP outer="<<l.outer<<" edges="<<l.orderedEdgeIds.size()<<"\n";faces.insert(113);
 for(const auto &id:l.orderedEdgeIds){for(const auto &e:m.pickEdges)if(e.id==id){BRepAdaptor_Curve c(TopoDS::Edge(g.edgeMap(id.section(':',1).toInt())));std::cout<<id.toStdString()<<" type="<<int(c.GetType())<<" range="<<c.FirstParameter()<<","<<c.LastParameter();if(c.GetType()==GeomAbs_Circle)std::cout<<" radius="<<c.Circle().Radius();std::cout<<" endpoints="<<e.points.first().x()<<","<<e.points.first().y()<<","<<e.points.first().z()<<" -> "<<e.points.last().x()<<","<<e.points.last().y()<<","<<e.points.last().z()<<" adjacent=";for(int f:e.adjacentFaceIndices){std::cout<<f<<",";faces.insert(f);}std::cout<<"\n";}}
}
QSet<int> supports=faces;for(const auto &e:m.pickEdges)for(int f:e.adjacentFaceIndices)if(faces.contains(f))for(int n:e.adjacentFaceIndices)supports.insert(n);
for(int f:supports){BRepAdaptor_Surface surf(TopoDS::Face(g.faceMap(f)));float lo=1e9,hi=-1e9;for(const auto&t:m.triangles)if(t.faceIndex==f)for(auto p:{t.v0,t.v1,t.v2}){lo=qMin(lo,p.z());hi=qMax(hi,p.z());}std::cout<<"FACE "<<f<<" surface="<<int(surf.GetType())<<" Z="<<lo<<","<<hi<<" neighbors=";QSet<int> neighbors;for(const auto&e:m.pickEdges)if(e.adjacentFaceIndices.contains(f))for(int n:e.adjacentFaceIndices)if(n!=f)neighbors.insert(n);for(int n:neighbors)std::cout<<n<<",";std::cout<<"\n";}
return 0;}
