#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <Poly_Triangulation.hxx>
#include <gp_Ax2.hxx>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#undef private
#include "../src/tool/ToolLibrary.h"
#include <iostream>
#include <cmath>

int failures=0;
void check(bool ok,const char *message){if(!ok){++failures;std::cerr<<"FAIL "<<message<<std::endl;}}
MeshData holeMesh()
{
    const auto block=BRepPrimAPI_MakeBox(gp_Pnt(-10,-10,-10),20,20,10).Shape();
    const auto bore=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,-11),gp_Dir(0,0,1)),4,12).Shape();
    MeshData mesh;mesh.sourceShape=BRepAlgoAPI_Cut(block,bore).Shape();
    mesh.bbMin={-10,-10,-10};mesh.bbMax={10,10,0};
    BRepMesh_IncrementalMesh mesher(mesh.sourceShape,.5,false,.5,true);
    for(TopExp_Explorer it(mesh.sourceShape,TopAbs_FACE);it.More();it.Next()){
        TopLoc_Location location;const auto tri=BRep_Tool::Triangulation(TopoDS::Face(it.Current()),location);
        if(tri.IsNull())continue;
        for(int i=1;i<=tri->NbTriangles();++i){
            int a,b,c;tri->Triangle(i).Get(a,b,c);
            auto point=[&](int n){const auto p=tri->Node(n).Transformed(location.Transformation());return QVector3D(p.X(),p.Y(),p.Z());};
            Triangle t;t.v0=point(a);t.v1=point(b);t.v2=point(c);mesh.triangles.append(t);
        }
    }
    return mesh;
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    auto mesh=holeMesh();
    ToolMove move;move.isRapid=true;move.from={0,0,-5};move.to={0,0,50};move.toolDiameter=7.998;move.fluteLength=8;move.totalLength=12;move.shankDiameter=6;move.sourceLine=17;
    auto result=SimulationController::checkMotion(mesh,{move});
    check(!result.blocksOutput(),"strictly separated full retract should pass exact geometry despite coarse mesh");
    move.toolDiameter=8;
    result=SimulationController::checkMotion(mesh,{move});
    check(result.state==SimulationCheckState::Incomplete&&result.blocksOutput(),"same-radius contact is unproven, not certified collision or clearance");
    move.from.setX(5e-8);move.to.setX(5e-8);
    check(SimulationController::checkMotion(mesh,{move}).state==SimulationCheckState::Incomplete,"sub-tolerance contact remains unproven");
    move.from.setX(1);move.to.setX(1);
    result=SimulationController::checkMotion(mesh,{move});
    check(result.state==SimulationCheckState::Collision&&result.blocksOutput(),"large offset proves material interference");
    move.from.setX(0);move.to.setX(0);move.toolDiameter=9;
    check(SimulationController::checkMotion(mesh,{move}).state==SimulationCheckState::Collision,"oversized cutter proves interference");
    move.toolDiameter=6;move.fluteLength=1;move.shankDiameter=12;
    check(SimulationController::checkMotion(mesh,{move}).state==SimulationCheckState::Collision,"shank entering material cannot inherit cutter clearance");
    move.fluteLength=0;
    check(SimulationController::checkMotion(mesh,{move}).state==SimulationCheckState::Incomplete,"missing cutter length leaves full retract unproven");
    move.toolDiameter=7.998;move.fluteLength=8;move.shankDiameter=6;move.from.setZ(-16);
    check(SimulationController::checkMotion(mesh,{move}).state==SimulationCheckState::Collision,"deep upward move stays blocked before exact clearance");
    move.from={0,0,-5};move.to={1,0,50};
    check(SimulationController::checkMotion(mesh,{move}).blocksOutput(),"diagonal move cannot use vertical retract proof");
    move.to={0,0,-6};check(SimulationController::checkMotion(mesh,{move}).blocksOutput(),"downward move cannot use retract proof");
    move.to={0,0,50};auto noShape=mesh;noShape.sourceShape.Nullify();
    check(SimulationController::checkMotion(noShape,{move}).blocksOutput(),"missing exact source cannot replace mesh collision");
    auto extraMaterial=mesh;BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
    builder.Add(compound,mesh.sourceShape);builder.Add(compound,BRepPrimAPI_MakeBox(gp_Pnt(-1,-1,-3),2,2,1).Shape());extraMaterial.sourceShape=compound;
    check(SimulationController::checkMotion(extraMaterial,{move}).state==SimulationCheckState::Collision,"a second solid inside the bore must block the sweep");
    auto openSource=mesh;TopExp_Explorer face(mesh.sourceShape,TopAbs_FACE);openSource.sourceShape=face.Current();
    check(SimulationController::checkMotion(openSource,{move}).state==SimulationCheckState::Incomplete,"an open exact source cannot certify a retract");
    move.toolModelPath="custom.step";
    check(SimulationController::checkMotion(mesh,{move}).state==SimulationCheckState::Incomplete,"unbounded custom tool shape remains unproven");
    move.toolModelPath.clear();move.shankDiameter=0;
    check(!SimulationController::checkMotion(mesh,{move}).blocksOutput(),"unknown shank radius is harmless only when its entire start is above all material");
    move.fluteLength=1;
    check(SimulationController::checkMotion(mesh,{move}).state==SimulationCheckState::Incomplete,"unknown shank near material remains unproven");
    move.fluteLength=8;move.shankDiameter=6;
    auto contact=move;contact.toolDiameter=8;
    check(SimulationController::checkMotion(mesh,{contact,move}).state==SimulationCheckState::Incomplete,"later separated segment must not clear previous uncertainty");
    QVector<ToolMove> many;
    for(int i=0;i<20;++i){
        auto safe=move;
        safe.from.setX((i-9.5)*.00004);safe.to.setX(safe.from.x());
        safe.from.setY((i%3-1)*.00002);safe.to.setY(safe.from.y());
        safe.sourceLine=i;many.append(safe);
    }
    result=SimulationController::checkMotion(mesh,many);
    check(!result.blocksOutput(),"twenty distinct separated retracts must all be checked and pass");
    auto mixedSource=mesh;TopoDS_Compound mixed;builder.MakeCompound(mixed);
    builder.Add(mixed,mesh.sourceShape);builder.Add(mixed,face.Current());mixedSource.sourceShape=mixed;
    check(SimulationController::checkMotion(mixedSource,many).state==SimulationCheckState::Incomplete,
          "valid solid prefix cannot certify a source containing a loose face across a batch");
    const auto prefix=many.mid(0,16);
    auto afterPrefix=[&](const ToolMove &last){auto batch=prefix;batch.append(last);return SimulationController::checkMotion(mesh,batch);};
    auto wide=move;wide.toolDiameter=9;wide.sourceLine=99;
    result=afterPrefix(wide);
    check(result.state==SimulationCheckState::Collision&&result.message.contains("100")&&result.message.contains("Exact cutter/shank"),"seventeenth cutter interference is checked exactly");
    auto deep=move;deep.from.setZ(-16);deep.sourceLine=100;
    result=afterPrefix(deep);
    check(result.state==SimulationCheckState::Collision&&result.message.contains("101"),"later known bottom violation retains priority");
    auto shortTool=move;shortTool.toolDiameter=6;shortTool.fluteLength=1;shortTool.shankDiameter=12;
    check(afterPrefix(shortTool).state==SimulationCheckState::Collision,"seventeenth short-flute large-shank collision is checked");
    auto unknown=move;unknown.fluteLength=1;unknown.shankDiameter=0;
    result=afterPrefix(unknown);
    check(result.state==SimulationCheckState::Incomplete&&result.message.contains("clearance is unproven"),"seventeenth unknown shank remains unproven");
    unknown=move;unknown.toolModelPath="custom.step";
    result=afterPrefix(unknown);
    check(result.state==SimulationCheckState::Incomplete&&result.message.contains("clearance is unproven"),"seventeenth custom tool remains unproven");
    auto tinyXY=move;tinyXY.to.setX(.005);
    check(afterPrefix(tinyXY).blocksOutput(),"sub-XY-threshold lateral motion cannot inherit vertical clearance");
    auto uncertainThenSafe=many;uncertainThenSafe.prepend(contact);
    check(SimulationController::checkMotion(mesh,uncertainThenSafe).state==SimulationCheckState::Incomplete,"twenty later safe retracts do not erase contact uncertainty");
    check(!SimulationController::checkMotion(mesh,many).blocksOutput()&&
          SimulationController::checkMotion(extraMaterial,many).state==SimulationCheckState::Collision&&
          !SimulationController::checkMotion(mesh,many).blocksOutput(),"source changes cannot reuse earlier clearance");
    auto lower=move;lower.from.setZ(-8);
    check(!SimulationController::checkMotion(mesh,{move,lower}).blocksOutput()&&
          !SimulationController::checkMotion(mesh,{lower,move}).blocksOutput(),"shallow/deep order does not change separated result");
    auto upper=mesh;TopoDS_Compound upperShape;builder.MakeCompound(upperShape);
    builder.Add(upperShape,mesh.sourceShape);builder.Add(upperShape,BRepPrimAPI_MakeBox(gp_Pnt(-.5,-.5,20),1,1,1).Shape());
    upper.sourceShape=upperShape;upper.bbMax.setZ(21);upper.triangles.clear();
    BRepMesh_IncrementalMesh upperMesher(upper.sourceShape,.5,false,.5,true);
    for(TopExp_Explorer it(upper.sourceShape,TopAbs_FACE);it.More();it.Next()){
        TopLoc_Location location;const auto tri=BRep_Tool::Triangulation(TopoDS::Face(it.Current()),location);
        if(tri.IsNull())continue;
        for(int i=1;i<=tri->NbTriangles();++i){
            int a,b,c;tri->Triangle(i).Get(a,b,c);
            auto point=[&](int n){const auto p=tri->Node(n).Transformed(location.Transformation());return QVector3D(p.X(),p.Y(),p.Z());};
            Triangle t;t.v0=point(a);t.v1=point(b);t.v2=point(c);upper.triangles.append(t);
        }
    }
    auto lowEnd=move;lowEnd.to.setZ(5);
    check(!SimulationController::checkMotion(upper,{lowEnd}).blocksOutput(),"short upward sweep remains below independent upper solid");
    QVector<ToolMove> highEnd(16,lowEnd);auto high=move;high.sourceLine=101;highEnd.append(high);
    result=SimulationController::checkMotion(upper,highEnd);
    check(result.state==SimulationCheckState::Collision&&result.message.contains("102"),"seventeenth higher endpoint checks independent upper solid");
    ToolEntry tool;tool.id=987;tool.diameter=7.998;tool.fluteLen=8;tool.totalLen=12;tool.shankDia=6;
    ToolLibrary::instance().removeTool(tool.id);ToolLibrary::instance().addTool(tool);
    for(const QString &cycle: {QString("G1 Z-5\nG0 Z50"),QString("G98 G81 X0 Y0 Z-5 R3 F40\nG80"),QString("MCALL CYCLE81(50,0,3,-5,)\nX0 Y0\nMCALL")}){
        SimulationController sim;sim.loadGCode("T987 M6\nG0 X0 Y0 Z50\n"+cycle,true);
        check(sim.motionError().isEmpty(),"tool metadata fixture parses");
        for(const auto &m:sim.moves())check(m.fluteLength==8&&m.totalLength==12&&m.shankDiameter==6,"tool dimensions must follow every linear and expanded-cycle move");
    }
    SimulationController replaced;replaced.loadGCode("T987 M6\nG0 X0 Y0 Z50\nT998 M6\nG1 Z-5",true);
    check(replaced.moves().last().toolDiameter==0&&replaced.moves().last().fluteLength==0,"unknown tool cannot retain prior diameter or dimensions");
    for(const QString &prefix: {QString("G98 G81 X0 Y0 Z-5 R3 F40\n"),QString("MCALL CYCLE81(50,0,3,-5,)\nX0 Y0\n")}) {
        SimulationController changed;changed.loadGCode("T987 M6\nG0 X0 Y0 Z50\n"+prefix+"T998 X1 Y0",true);
        check(changed.motionError().isEmpty()&&changed.moves().last().toolDiameter==0&&changed.moves().last().totalLength==0,"active Fanuc and Siemens cycles reset dimensions on unknown T");
    }
    SimulationController arc;arc.loadGCode("T987 M6\nG0 X1 Y0 Z50\nG3 X0 Y1 I-1 J0",true);
    check(arc.motionError().isEmpty()&&arc.moves().size()>2,"arc metadata fixture has real segments");
    for(const auto &m:arc.moves())check(m.fluteLength==8&&m.totalLength==12&&m.shankDiameter==6,"arc segments retain dimensions");
    std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
