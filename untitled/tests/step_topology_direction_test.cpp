#include <QCoreApplication>
#include <QHash>
#include <iostream>
#include <cmath>
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include "../src/import/StepImporter.h"
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc!=2)return 2;
    StepImporter importer;if(!importer.load(QString::fromLocal8Bit(argv[1])))return 3;
    QHash<QString,QVector3D> vertices;
    for(const auto &v:importer.mesh().pickVertices)vertices.insert(v.id,v.point);
    const auto &graph=importer.topoGraph();
    const double zShift=vertices.value("vertex:1").z()-BRep_Tool::Pnt(TopoDS::Vertex(graph.vertexMap(1))).Z();
    int checked=0,mismatched=0,reversed=0,parameterErrors=0;
    int coverage[2][2]={{0,0},{0,0}};
    for(const auto &edge:importer.mesh().pickEdges) {
        if(edge.degenerated||edge.points.size()<2)continue;
        ++checked;
        const auto first=vertices.value(edge.firstVertexId),last=vertices.value(edge.lastVertexId);
        if((edge.points.first()-first).length()>1e-4f||(edge.points.last()-last).length()>1e-4f) {
            ++mismatched;
            if((edge.points.last()-first).length()<1e-4f&&(edge.points.first()-last).length()<1e-4f)++reversed;
            if(edge.id=="edge:454"||edge.id=="edge:455")std::cerr<<edge.id.toStdString()<<" firstGap="<<(edge.points.first()-first).length()<<" lastGap="<<(edge.points.last()-last).length()<<"\n";
        }
        if(edge.points.size()!=edge.parameters.size())return 4;
        BRepAdaptor_Curve curve(TopoDS::Edge(graph.edgeMap(edge.id.mid(5).toInt())));
        const bool backward=graph.edgeMap(edge.id.mid(5).toInt()).Orientation()==TopAbs_REVERSED;
        if(curve.GetType()==GeomAbs_Line||curve.GetType()==GeomAbs_Circle)
            ++coverage[curve.GetType()==GeomAbs_Circle][backward];
        if(std::abs(edge.parameters.first()-(backward?curve.LastParameter():curve.FirstParameter()))>1e-9
            ||std::abs(edge.parameters.last()-(backward?curve.FirstParameter():curve.LastParameter()))>1e-9)++parameterErrors;
        for(int i=0;i<edge.points.size();++i) {
            const auto p=curve.Value(edge.parameters[i]);
            if((edge.points[i]-QVector3D(float(p.X()),float(p.Y()),float(p.Z()+zShift))).length()>1e-4f)++parameterErrors;
        }
    }
    int joins=0,joinErrors=0;
    QHash<QString,const MeshPickEdge*> edges;
    for(const auto &edge:importer.mesh().pickEdges)edges.insert(edge.id,&edge);
    for(const auto &loop:importer.mesh().boundaryLoops) {
        for(int i=0;i<loop.orderedEdgeIds.size();++i) {
            const int j=(i+1)%loop.orderedEdgeIds.size();
            const auto *a=edges.value(loop.orderedEdgeIds[i]),*b=edges.value(loop.orderedEdgeIds[j]);
            if(a->points.isEmpty()||b->points.isEmpty())continue;
            const auto end=loop.orderedForward[i]?a->points.last():a->points.first();
            const auto start=loop.orderedForward[j]?b->points.first():b->points.last();
            ++joins;if((end-start).length()>1e-4f)++joinErrors;
        }
    }
    std::cout<<"Checked="<<checked<<" mismatched="<<mismatched<<" reversed="<<reversed
        <<" parameterErrors="<<parameterErrors<<" wireJoins="<<joins<<" joinErrors="<<joinErrors<<std::endl;
    for(int type=0;type<2;++type)for(int direction=0;direction<2;++direction) {
        std::cout<<(type?"circle":"line")<<(direction?" reversed=":" forward=")<<coverage[type][direction]<<std::endl;
        if(!coverage[type][direction])return 5;
    }
    if(mismatched||parameterErrors||joinErrors||!joins)return 1;
    std::cout<<"PASS real STEP edge samples match their directed vertex IDs"<<std::endl;
    return 0;
}
