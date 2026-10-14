#include "../src/import/StepImporter.h"
#include "../src/workspaces/process_design/selection/PocketOpening.h"
#include "../src/workspaces/program_verification/simulation/ExactRetractionCheck.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepTools.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopExp.hxx>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <sstream>
#include <iostream>

static int failures=0;
static QByteArray geometryRecord(const TopoDS_Shape &shape) {
    // Normalize only transient bookkeeping flags on a deep copy. Retain all
    // topology, geometry, tolerances, orientations, locations and Closed flags.
    auto copy=BRepBuilderAPI_Copy(shape,true,false).Shape();
    TopTools_IndexedMapOfShape parts;TopExp::MapShapes(copy,parts);
    for(int i=1;i<=parts.Extent();++i){auto p=parts(i);p.Free(false);p.Modified(false);p.Checked(false);}
    std::ostringstream stream;BRepTools::Write(copy,stream,false,false,TopTools_FormatVersion_CURRENT);
    const auto text=stream.str();return QByteArray(text.data(),int(text.size()));
}
static void check(bool value,const char *name) {
    std::cout<<"CASE "<<name<<" pass="<<value<<std::endl;if(!value)++failures;
}
static void compare(const QByteArray &before,const TopoDS_Shape &shape,const QString &path) {
    const auto after=geometryRecord(shape);
    check(before==after,"source geometry topology and tolerances unchanged");
    if(before!=after) {
        QFile a(path+"-before.brep"),b(path+"-after.brep");
        if(a.open(QIODevice::WriteOnly))a.write(before);
        if(b.open(QIODevice::WriteOnly))b.write(after);
    }
}
static MeshData hole() {
    MeshData mesh;
    mesh.sourceShape=BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(gp_Pnt(-10,-10,-10),20,20,10).Shape(),
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,-11),gp_Dir(0,0,1)),4,12).Shape()).Shape();
    return mesh;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);if(argc!=3)return 2;
    const QString out=QString::fromLocal8Bit(argv[2]);QDir().mkpath(out);
    StepImporter importer;if(!importer.load(QString::fromLocal8Bit(argv[1])))return 3;
    const auto before=geometryRecord(importer.mesh().sourceShape);
    check(before==geometryRecord(importer.mesh().sourceShape),"record calibration is stable before validation");
    bool found=false;
    for(const auto &loop:importer.mesh().boundaryLoops)if(loop.faceIndex==113 && loop.orderedEdgeIds.contains("edge:452")) {
        SelectionBoundaryLoop first,second;QString error;
        check(verifyPocketOpening(importer.mesh(),loop,&first,nullptr,&error),"existing eight-edge opening accepted");
        compare(before,importer.mesh().sourceShape,out+"/opening");
        check(verifyPocketOpening(importer.mesh(),loop,&second,nullptr,&error) && first==second,"repeated opening evidence agrees");
        compare(before,importer.mesh().sourceShape,out+"/opening-repeat");found=true;
    }
    check(found,"real opening fixture selected");
    auto material=hole();const auto initial=geometryRecord(material.sourceShape);
    check(initial==geometryRecord(material.sourceShape),"retraction record calibration stable");
    ToolMove move;move.isRapid=true;move.from={0,0,-5};move.to={0,0,50};move.fromKnownAxes=7;move.toKnownAxes=7;
    move.fluteLength=8;move.totalLength=8;
    QVector<RetractionEvidenceState> states;
    for(double diameter:{7.998,8.,9.}) {
        move.toolDiameter=diameter;const auto result=checkExactRetraction(material,move);states.append(result.state);
        compare(initial,material.sourceShape,out+QString("/retract-%1").arg(diameter));
    }
    check(states[0]==RetractionEvidenceState::Separated && states[1]==RetractionEvidenceState::Uncertain && states[2]==RetractionEvidenceState::Interference,"clearance contact and interference remain distinct");
    auto reordered=hole();int index=2;
    for(double diameter:{9.,8.,7.998}) {
        move.toolDiameter=diameter;check(checkExactRetraction(reordered,move).state==states[index--],"reversed check order keeps result");
    }
    compare(initial,reordered.sourceShape,out+"/reordered");
    std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
