#include <QCoreApplication>
#include <iostream>
#include "../src/workspaces/process_design/operations/MachiningInput.h"
static void check(bool value,const char *message){if(!value){std::cerr<<"FAIL "<<message<<std::endl;std::exit(1);}}
int main(int argc,char **argv){QCoreApplication app(argc,argv);
 MeshData mesh;MeshBoundaryLoop source;source.faceId="face:1";source.faceIndex=1;source.outer=true;
 QVector<QVector3D> square{{0,0,0},{20,0,0},{20,20,0},{0,20,0}};
 for(int i=0;i<4;++i){MeshPickEdge e;e.id=QString("edge:%1").arg(i);e.points={square[i],square[(i+1)%4]};mesh.pickEdges.append(e);source.orderedEdgeIds.append(e.id);source.orderedForward.append(true);}
 mesh.boundaryLoops.append(source);SelectionBoundaryLoop loop;loop.faceId=source.faceId;loop.outer=true;loop.orderedEdgeIds=source.orderedEdgeIds;loop.orderedForward=source.orderedForward;
 SelectionChain selection;selection.boundaryLoops={loop};selection.orderedTopologyRefs={{"edge:0",SelectionTopologyKind::Edge,true}};
 ContourFeature feature;feature.subType="open_contour";feature.points={square[0],square[1]};
 check(selectionMatchesContour(selection,feature,mesh),"valid two-point open contour binds exact selected edge");
 feature.points={square[1],square[2]};check(!selectionMatchesContour(selection,feature,mesh),"different open edge rejected");
 feature.subType="closed_contour";feature.points=square;selection.closed=true;
 selection.orderedTopologyRefs.clear();for(const auto&id:source.orderedEdgeIds)selection.orderedTopologyRefs.append({id,SelectionTopologyKind::Edge,true});
 check(selectionMatchesContour(selection,feature,mesh),"complete matching outer wire binds");
 MeshBoundaryLoop inner;inner.faceId="face:1";inner.faceIndex=1;inner.outer=false;inner.orderedEdgeIds=QStringList{"island"};inner.orderedForward={true};mesh.boundaryLoops.append(inner);
 MeshPickEdge island;island.id="island";island.points={{5,5,0},{7,5,0},{7,7,0},{5,7,0},{5,5,0}};mesh.pickEdges.append(island);
 feature.islands={{{10,10,0},{12,10,0},{12,12,0},{10,12,0}}};
 check(!selectionMatchesContour(selection,feature,mesh),"equal island count cannot bind different island geometry");
 feature.islands={island.points};check(selectionMatchesContour(selection,feature,mesh),"actual island geometry binds on edge selection");
 feature.islands.clear();check(!selectionMatchesContour(selection,feature,mesh),"outer-only edge selection cannot drop a source island");
 std::cout<<"PASS exact region binding including open path and islands"<<std::endl;
}

