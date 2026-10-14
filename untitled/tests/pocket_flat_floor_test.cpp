#include "../src/workspaces/process_design/selection/PocketOpening.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <TopoDS_Compound.hxx>
#include <STEPControl_Writer.hxx>
#include <sstream>
#include "../src/workspaces/program_verification/simulation/ExactRetractionCheck.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"

static int failures=0;
static void check(bool ok,const char *label) {
    QTextStream(stdout)<<(ok?"PASS ":"FAIL ")<<label<<Qt::endl;if(!ok)++failures;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTemporaryDir dir;
    const auto block=BRepPrimAPI_MakeBox(40,30,12).Shape();
    const auto cut=BRepPrimAPI_MakeBox(gp_Pnt(5,5,4),30,20,10).Shape();
    const auto shape=BRepAlgoAPI_Cut(block,cut).Shape();
    const QString file=dir.path()+"/flat.step";
    STEPControl_Writer writer;writer.Transfer(shape,STEPControl_AsIs);
    check(writer.Write(file.toLocal8Bit().constData())==IFSelect_RetDone,"write real flat-floor solid");
    StepImporter importer;check(importer.load(file),"read flat-floor STEP");
    SelectionBoundaryLoop opening;MachiningFeature recognized;bool found=false;
    for(const auto &loop:importer.mesh().boundaryLoops)if(verifyPocketOpening(importer.mesh(),loop,&opening,&recognized)) {found=true;break;}
    check(found,"existing strict void proof accepts rectangular opening");if(!found)return 1;
    SelectionChain selection;selection.boundaryLoops={opening};
    for(int i=0;i<opening.orderedEdgeIds.size();++i) {
        SelectionTopologyRef ref;ref.kind=SelectionTopologyKind::Edge;ref.id=opening.orderedEdgeIds[i];ref.forward=opening.orderedForward[i];selection.orderedTopologyRefs.append(ref);
    }
    ContourFeature feature;feature.subType="verified_slot_opening";feature.center=machiningBoundaryCenter(opening.machiningBoundary);
    feature.points=opening.machiningBoundary;feature.axis={0,0,1};feature.planarBoundaryVerified=true;
    StrategyParams params;params.set("stockToLeave",.3);params.set("floorStockToLeave",.5);
    auto error=[&](double depth) {feature.depth=depth;params.set("depth",depth);return pocketOpeningOperationError(selection,feature,params,"mill_pocket_rough",&importer.mesh());};
    check(error(opening.maximumDepth).isEmpty(),"legacy common void depth remains valid");
    const QString floorError=error(8);
    QTextStream(stdout)<<"FULL_FLOOR_ERROR="<<floorError<<Qt::endl;
    check(floorError.isEmpty(),"actual flat bottom authorizes true depth with separate floor stock");
    check(!error(8.001).isEmpty(),"depth below actual floor remains rejected");
    check(!pocketOpeningOperationError(selection,feature,params,"mill_pocket_rough",nullptr).isEmpty(),"missing source cannot authorize floor contact");
    feature.depth=8;params.set("depth",8);params.set("sideStockToLeave",.3);params.set("entryMode",0);
    for(const QString &strategy:QStringList{"mill_pocket_finish","mill_pocket_floor_finish"}) {
        check(pocketOpeningOperationError(selection,feature,params,strategy,&importer.mesh()).isEmpty(),
              "source flat floor authorizes independently selected finish strategy");
    }
    feature.depth=7;params.set("depth",7);
    check(!pocketOpeningOperationError(selection,feature,params,"mill_pocket_floor_finish",&importer.mesh()).isEmpty(),
          "floor finish cannot mistake arbitrary shallow level for actual floor");
    const double bottom=feature.center.z()-8;
    ToolMove retract;retract.isRapid=true;retract.from={feature.center.x(),feature.center.y(),bottom};retract.to={feature.center.x(),feature.center.y(),50};
    retract.fromKnownAxes=7;retract.toKnownAxes=7;retract.toolDiameter=4;
    retract.fluteLength=2;retract.totalLength=10;retract.shankDiameter=4;
    check(checkExactRetraction(importer.mesh(),retract).state==RetractionEvidenceState::Separated,
          "source-proved flat floor permits exact upward release from contact");
    auto below=retract;below.from.setZ(bottom-1e-8);
    check(checkExactRetraction(importer.mesh(),below).state!=RetractionEvidenceState::Separated,"no tolerance authorizes a start below floor");
    auto outside=retract;outside.from.setX(feature.center.x()-14);outside.to.setX(feature.center.x()-14);
    check(checkExactRetraction(importer.mesh(),outside).state!=RetractionEvidenceState::Separated,"floor release cannot authorize outside cutter footprint");
    auto shank=retract;shank.shankDiameter=32;
    check(checkExactRetraction(importer.mesh(),shank).state==RetractionEvidenceState::Interference,"floor release preserves original full shank sweep");
    auto custom=retract;custom.toolModelPath="custom.step";
    check(checkExactRetraction(importer.mesh(),custom).state!=RetractionEvidenceState::Separated,"floor release does not certify custom tools");
    auto unknown=retract;unknown.fromKnownAxes=3;
    check(checkExactRetraction(importer.mesh(),unknown).state!=RetractionEvidenceState::Separated,"floor release requires known coordinates");
    auto upper=importer.mesh();BRep_Builder extraBuilder;TopoDS_Compound upperSource;extraBuilder.MakeCompound(upperSource);
    extraBuilder.Add(upperSource,upper.sourceShape);extraBuilder.Add(upperSource,BRepPrimAPI_MakeBox(gp_Pnt(feature.center.x()-1,feature.center.y()-1,20),2,2,1).Shape());
    upper.sourceShape=upperSource;
    check(checkExactRetraction(upper,retract).state==RetractionEvidenceState::Interference,"floor release still checks upper material");
    BRepBuilderAPI_MakePolygon tilted;
    tilted.Add(gp_Pnt(5,5,4));tilted.Add(gp_Pnt(35,5,5));tilted.Add(gp_Pnt(35,25,5));tilted.Add(gp_Pnt(5,25,4));tilted.Close();
    const auto sloped=BRepAlgoAPI_Cut(block,BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(tilted.Wire()).Face(),gp_Vec(0,0,20)).Shape()).Shape();
    const auto through=BRepAlgoAPI_Cut(block,BRepPrimAPI_MakeBox(gp_Pnt(5,5,-1),30,20,20).Shape()).Shape();
    const auto perforated=BRepAlgoAPI_Cut(shape,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20,15,-1),gp_Dir(0,0,1)),2,6).Shape()).Shape();
    TopoDS_Compound floating;BRep_Builder builder;builder.MakeCompound(floating);
    builder.Add(floating,shape);builder.Add(floating,BRepPrimAPI_MakeBox(gp_Pnt(15,10,4.0005),10,10,.0002).Shape());
    int fixtureId=0;
    for(const auto &fixture:{shape,sloped,through,perforated,TopoDS_Shape(floating)}) {
        const QString path=dir.path()+QString("/case-%1.step").arg(fixtureId);
        STEPControl_Writer out;out.Transfer(fixture,STEPControl_AsIs);
        check(out.Write(path.toLocal8Bit().constData())==IFSelect_RetDone,"write floor counterexample");
        StepImporter item;check(item.load(path),"read floor counterexample");
        std::ostringstream before,after;BRepTools::Write(item.mesh().sourceShape,before);
        bool accepted=false,voidAccepted=false;
        for(const auto &loop:item.mesh().boundaryLoops) {
            SelectionBoundaryLoop proof;
            if(!verifyPocketOpening(item.mesh(),loop,&proof,nullptr))continue;
            voidAccepted=true;double floorDepth=0;QString why;
            const bool ok=verifyPocketFlatFloor(item.mesh(),loop,proof,&floorDepth,&why);
            QTextStream(stdout)<<"FIXTURE="<<fixtureId<<" FLOOR="<<ok<<" DEPTH="<<floorDepth<<" ERROR="<<why<<Qt::endl;
            accepted|=ok;
            if(ok)check(std::abs(floorDepth-8)<1e-8,"flat-floor result is real depth, not maximum void depth");
        }
        BRepTools::Write(item.mesh().sourceShape,after);
        check(before.str()==after.str(),"floor and void proof preserve source geometry");
        check(voidAccepted,"counterexample retains valid upper void evidence");
        check(accepted==(fixtureId==0),"only intact horizontal flat floor authorizes contact");
        ++fixtureId;
    }
    QTextStream(stdout)<<"FAILURES="<<failures<<Qt::endl;return failures?1:0;
}
