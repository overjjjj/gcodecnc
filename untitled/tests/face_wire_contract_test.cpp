#include "../src/import/StepImporter.h"
#include <QCoreApplication>
#include <QDir>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <iostream>

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    if(argc<2) return 2;
    const QString out=QString::fromLocal8Bit(argv[1]);QDir().mkpath(out);
    // Inspect existing real models first; report raw wire counts independently of recognition.
    for(int i=2;i<argc;++i){
        StepImporter model;
        if(!model.load(QString::fromLocal8Bit(argv[i]))) return 3;
        for(const auto &f:model.features()) if(f.kind==FeatureKind::FlatSurface){
            for(int face:f.faceIndices){
                const auto shape=model.topoGraph().faceMap(face);int wires=0,curves=0;
                for(TopExp_Explorer e(shape,TopAbs_WIRE);e.More();e.Next()) ++wires;
                for(TopExp_Explorer e(shape,TopAbs_EDGE);e.More();e.Next())
                    if(BRepAdaptor_Curve(TopoDS::Edge(e.Current())).GetType()!=GeomAbs_Line) ++curves;
                std::cout<<argv[i]<<" face="<<face<<" wires="<<wires<<" curves="<<curves
                         <<" axis="<<f.axis.x()<<','<<f.axis.y()<<','<<f.axis.z()<<'\n';
            }
        }
    }
    STEPControl_Writer writer;writer.Transfer(BRepPrimAPI_MakeBox(40,20,10).Shape(),STEPControl_AsIs);
    const QByteArray path=(out+"/engineering-box.step").toLocal8Bit();
    if(writer.Write(path.constData())!=IFSelect_RetDone) return 4;
    StepImporter importer;if(!importer.load(QString::fromLocal8Bit(path))) return 5;
    bool found=false;
    for(const auto &f:importer.features()) if(f.kind==FeatureKind::FlatSurface && f.axis.z()>0.99){
        found=true;
        if(!f.planarBoundaryVerified || f.boundaryPoints.size()!=4){
            std::cerr<<"F2 original box top has no complete planar boundary evidence\n";return 1;
        }
        for(const auto &p:f.boundaryPoints) if(std::abs(p.z()-f.center.z())>0.0001){
            std::cerr<<"normalized top and original boundary use different Z frames\n";return 10;
        }
    }
    if(!found){std::cerr<<"box top not recognized\n";return 6;}
    gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0.01*3.141592653589793/180);
    const auto rotated=BRepBuilderAPI_Transform(BRepPrimAPI_MakeBox(40,20,10).Shape(),rotation).Shape();
    const auto cylinder=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20,10,0),gp_Dir(0,0,1)),3,10).Shape();
    const auto holed=BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(40,20,10).Shape(),cylinder).Shape();
    int index=0;
    for(const auto &shape:{rotated,holed,cylinder}) {
        const QString filename=out+QString("/negative-%1.step").arg(++index);
        STEPControl_Writer w;w.Transfer(shape,STEPControl_AsIs);
        if(w.Write(filename.toLocal8Bit().constData())!=IFSelect_RetDone) return 7;
        StepImporter negative;if(!negative.load(filename))return 8;
        for(const auto &f:negative.features()) if(f.kind==FeatureKind::FlatSurface && f.axis.z()>0.99
            && f.planarBoundaryVerified){std::cerr<<"rotated/holed/curved face acquired rectangle evidence\n";return 9;}
    }
    std::cout<<"PASS original STEP wire evidence\n";return 0;
}
