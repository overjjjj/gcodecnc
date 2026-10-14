#include "../src/workspaces/process_design/selection/PocketVolumeProof.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <TopoDS_Shell.hxx>
#include <gp_Ax1.hxx>
#include <QVector3D>
#include <iostream>
static bool separatedVolume(const TopoDS_Shape &a,const TopoDS_Shape &b) {
    const auto proof=verifyPocketVolume(a,b);
    return proof.separated && proof.boundaryBudget>0 && proof.maximumEscapeBudget>=proof.boundaryBudget;
}
template<class Operation> static TopoDS_Shape fixtureBoolean(const TopoDS_Shape &a,const TopoDS_Shape &b) {
    Operation operation;TopTools_ListOfShape arguments,tools;arguments.Append(a);tools.Append(b);
    operation.SetArguments(arguments);operation.SetTools(tools);operation.SetNonDestructive(true);operation.Build();
    if(!operation.IsDone())throw Standard_Failure("fixture boolean failed");
    return operation.Shape();
}

static int volumeCases() {
    int failures=0;
    auto check=[&](bool condition,const char *name){std::cout<<"VOLUME_CASE "<<name<<" pass="<<condition<<std::endl;if(!condition)++failures;};
    const auto material=BRepPrimAPI_MakeBox(10,10,10).Shape();
    const auto inside=BRepPrimAPI_MakeBox(gp_Pnt(2,2,2),2,2,2).Shape();
    check(!separatedVolume(inside,material),"prism wholly inside material rejected");
    check(!separatedVolume(material,inside),"material wholly inside prism rejected");
    const auto separatedBox=BRepPrimAPI_MakeBox(gp_Pnt(20,0,0),2,2,2).Shape();
    check(separatedVolume(separatedBox,material),"separated solids accepted");
    check(!separatedVolume(BRepPrimAPI_MakeBox(gp_Pnt(10,0,0),2,2,2).Shape(),material),"touching face rejected");
    check(!separatedVolume(BRepPrimAPI_MakeBox(gp_Pnt(10+1e-8,0,0),2,2,2).Shape(),material),"gap below numerical budget rejected");
    check(separatedVolume(BRepPrimAPI_MakeBox(gp_Pnt(10+1e-4,0,0),2,2,2).Shape(),material),"positive gap above budget accepted");
    BRep_Builder builder;
    for(bool swap:{false,true}) {
        TopoDS_Compound compound;builder.MakeCompound(compound);
        builder.Add(compound,swap?inside:separatedBox);builder.Add(compound,swap?separatedBox:inside);
        check(!separatedVolume(material,compound),"all material components checked regardless of order");
    }
    TopoDS_Compound loose;builder.MakeCompound(loose);builder.Add(loose,separatedBox);
    TopExp_Explorer face(material,TopAbs_FACE);builder.Add(loose,face.Current());
    check(!separatedVolume(material,loose),"loose face mixed with solid rejected");
    TopoDS_Shell shell;builder.MakeShell(shell);int n=0;
    for(TopExp_Explorer it(material,TopAbs_FACE);it.More();it.Next())if(++n>1)builder.Add(shell,it.Current());
    TopoDS_Solid open;builder.MakeSolid(open);builder.Add(open,shell);
    check(!separatedVolume(separatedBox,open),"open material solid rejected");
    gp_Trsf tr;tr.SetTranslation(gp_Vec(100,20,30));
    TopoDS_Compound child,parent;builder.MakeCompound(child);builder.Add(child,material);
    child.Location(TopLoc_Location(tr));builder.MakeCompound(parent);builder.Add(parent,child);
    const auto placedInside=inside.Moved(TopLoc_Location(tr));
    check(!separatedVolume(placedInside,parent),"nested source placement retained");
    const auto baked=BRepBuilderAPI_Transform(material,tr,true).Shape();
    check(separatedVolume(separatedBox,parent)==separatedVolume(separatedBox,baked),"located and baked material separation agree");
    const auto thin=BRepPrimAPI_MakeBox(gp_Pnt(3.13,4.17,2),.01,.02,5).Shape();
    check(!separatedVolume(material,thin),"thin interior island outside typical sample points rejected");
    const auto stock=BRepPrimAPI_MakeBox(gp_Pnt(-1,-1,0),22,22,10).Shape();
    const auto cavity=fixtureBoolean<BRepAlgoAPI_Cut>(stock,BRepPrimAPI_MakeBox(gp_Pnt(0,0,2),20,20,9).Shape());
    const auto stepped=fixtureBoolean<BRepAlgoAPI_Fuse>(cavity,BRepPrimAPI_MakeBox(gp_Pnt(10,0,2),10,20,3).Shape());
    check(separatedVolume(BRepPrimAPI_MakeBox(gp_Pnt(1,1,6),18,18,4).Shape(),stepped),"stepped floor shallow prism with coplanar top accepted");
    check(!separatedVolume(BRepPrimAPI_MakeBox(gp_Pnt(1,1,4),18,18,6).Shape(),stepped),"same polygon deeper into raised floor rejected");
    const auto lip=fixtureBoolean<BRepAlgoAPI_Fuse>(stepped,BRepPrimAPI_MakeBox(gp_Pnt(-.1,2.13,9.99),1.5,.02,.01).Shape());
    check(!separatedVolume(BRepPrimAPI_MakeBox(gp_Pnt(1,1,6),18,18,4).Shape(),lip),"thin top lip rejected without removing top cap");
    const auto largeMaterial=BRepPrimAPI_MakeBox(gp_Pnt(1000000,0,0),10,10,10).Shape();
    const double idealX=1000010.001;const QVector3D storedStart(float(idealX),0,0);
    check(separatedVolume(BRepPrimAPI_MakeBox(gp_Pnt(idealX,0,0),2,2,2).Shape(),largeMaterial),"ideal large-coordinate gap accepted");
    check(!separatedVolume(BRepPrimAPI_MakeBox(gp_Pnt(storedStart.x(),0,0),2,2,2).Shape(),largeMaterial),"actual float geometry that loses gap rejected");
    gp_Trsf innerRotation, outerRotation;
    innerRotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),.43);
    innerRotation.SetTranslationPart(gp_Vec(13,-7,4));
    outerRotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,0,0)),-.61);
    outerRotation.SetTranslationPart(gp_Vec(-5,11,17));
    TopoDS_Compound rotatedChild,rotatedParent;builder.MakeCompound(rotatedChild);builder.Add(rotatedChild,material);
    rotatedChild.Location(TopLoc_Location(innerRotation));builder.MakeCompound(rotatedParent);builder.Add(rotatedParent,rotatedChild);
    rotatedParent.Location(TopLoc_Location(outerRotation));
    const gp_Trsf composed=outerRotation*innerRotation;
    check(gp_Pnt(2,3,4).Transformed(composed).Distance(gp_Pnt(2,3,4).Transformed(innerRotation*outerRotation))>1,"fixture transforms do not commute");
    const auto bakedRotated=BRepBuilderAPI_Transform(material,composed,true).Shape();
    const auto rotatedInside=BRepBuilderAPI_Transform(inside,composed,true).Shape();
    const auto rotatedOutside=BRepBuilderAPI_Transform(separatedBox,composed,true).Shape();
    check(!separatedVolume(rotatedInside,rotatedParent) && !separatedVolume(rotatedInside,bakedRotated),"nested rotations and translations preserve containment rejection");
    check(separatedVolume(rotatedOutside,rotatedParent) && separatedVolume(rotatedOutside,bakedRotated),"nested rotations and translations agree with baked separation");
    return failures;
}



#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRep_TEdge.hxx>
static int componentVolumeCases() {
    int failures=0;
    auto check=[&](bool ok,const char *label){std::cout<<"COMPONENT_VOLUME "<<label<<" PASS="<<ok<<std::endl;if(!ok)++failures;};
    const auto box=BRepPrimAPI_MakeBox(10,10,10).Shape();
    const auto inner=BRepPrimAPI_MakeBox(gp_Pnt(2,2,2),2,2,2).Shape();
    const auto hollow=fixtureBoolean<BRepAlgoAPI_Cut>(box,inner);
    const auto outside=BRepPrimAPI_MakeBox(gp_Pnt(20,0,0),2,2,2).Shape();
    check(separatedVolume(outside,hollow),"independent inner and outer shells outside accepted");
    check(!separatedVolume(BRepPrimAPI_MakeBox(gp_Pnt(2.5,2.5,2.5),1,1,1).Shape(),hollow),"closed cavity remains conservatively unsupported");
    BRep_Builder builder;gp_Trsf shift;shift.SetTranslation(gp_Vec(20,0,0));
    for(bool reverse:{false,true}) {
        TopoDS_Compound copies;builder.MakeCompound(copies);
        builder.Add(copies,reverse?box.Moved(TopLoc_Location(shift)):box);
        builder.Add(copies,reverse?box:box.Moved(TopLoc_Location(shift)));
        check(!separatedVolume(inner,copies),"same TShape located instances containment rejected in either order");
    }
    const auto cone=BRepPrimAPI_MakeCone(2,0,5).Shape();
    int degenerate=0;for(TopExp_Explorer it(cone,TopAbs_EDGE);it.More();it.Next())if(BRep_Tool::Degenerated(TopoDS::Edge(it.Current())))++degenerate;
    check(degenerate>0,"cone contains actual degenerate edge");
    check(separatedVolume(outside,cone),"degenerate edge fixture outside agrees");
    const auto tolerant=BRepBuilderAPI_Copy(box).Shape();
    for(TopExp_Explorer it(tolerant,TopAbs_EDGE);it.More();it.Next())builder.UpdateEdge(TopoDS::Edge(it.Current()),.05);
    check(!separatedVolume(BRepPrimAPI_MakeBox(gp_Pnt(10.01,0,0),2,2,2).Shape(),tolerant),"high edge tolerance consumes narrow clearance");
    check(separatedVolume(outside,tolerant),"high tolerance well separated agrees");
    for(double invalid:{std::numeric_limits<double>::quiet_NaN(),-1.}) {
        const auto bad=BRepBuilderAPI_Copy(box).Shape();TopExp_Explorer edge(bad,TopAbs_EDGE);
        Handle(BRep_TEdge)::DownCast(edge.Current().TShape())->Tolerance(invalid);
        std::cout<<"RAW_TOL="<<Handle(BRep_TEdge)::DownCast(edge.Current().TShape())->Tolerance()<<" TOOL_TOL="<<BRep_Tool::Tolerance(TopoDS::Edge(edge.Current()))<<std::endl;
        check(!std::isfinite(PocketVolumeDetail::shapeTolerance(bad)),"invalid raw tolerance cannot disappear in maximum");
    }
    return failures;
}

int main(){int failures=volumeCases()+componentVolumeCases();
 auto check=[&](bool ok,const char *label){std::cout<<"BOUNDS_CASE "<<label<<" "<<ok<<std::endl;if(!ok)++failures;};
 const auto target=BRepPrimAPI_MakeBox(10,10,10).Shape();const auto boundary=PocketVolumeDetail::boundary(target);
 Bnd_Box bounds;bounds.Update(0,0,0,10,10,10);
 for(const auto &point:{gp_Pnt(-1,5,5),gp_Pnt(11,5,5),gp_Pnt(5,-1,5),gp_Pnt(5,11,5),gp_Pnt(5,5,-1),gp_Pnt(5,5,11)}) {
  double maximum=0;check(PocketVolumeDetail::escapePath(point,bounds,boundary,1e-6,0,maximum) && maximum>1e-6,"six complete-bounds exterior directions");
 }
 double maximum=0;const double reserve=128*std::numeric_limits<double>::epsilon()*11;
 const double equalBudget=1-reserve-Precision::Confusion();
 check(equalBudget+reserve+Precision::Confusion()==1,"exact threshold fixture");
 check(!PocketVolumeDetail::escapePath(gp_Pnt(11,5,5),bounds,boundary,equalBudget,0,maximum),"equality does not grant exterior clearance");
 check(PocketVolumeDetail::escapePath(gp_Pnt(11,5,5),bounds,boundary,equalBudget-1e-10,0,maximum),"strictly greater than complete budget accepted");
 check(!PocketVolumeDetail::escapePath(gp_Pnt(std::numeric_limits<double>::quiet_NaN(),5,5),bounds,boundary,1e-6,0,maximum),"nonfinite witness rejected");
 check(!PocketVolumeDetail::escapePath(gp_Pnt(11,5,5),bounds,boundary,std::numeric_limits<double>::infinity(),0,maximum),"nonfinite budget rejected");
 check(!PocketVolumeDetail::escapePath(gp_Pnt(11,5,5),bounds,boundary,std::numeric_limits<double>::max(),std::numeric_limits<double>::max(),maximum),"overflowed budget rejected");
 Bnd_Box open;open.SetWhole();check(!PocketVolumeDetail::escapePath(gp_Pnt(11,5,5),open,boundary,1e-6,0,maximum),"unbounded target cannot grant OUT");
 Bnd_Box distant;distant.Update(-std::numeric_limits<double>::max(),0,0,-std::numeric_limits<double>::max()*.5,10,10);
 check(!PocketVolumeDetail::escapePath(gp_Pnt(std::numeric_limits<double>::max(),5,5),distant,boundary,1e-6,0,maximum),"overflowed exterior gap rejected");
 std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;}

