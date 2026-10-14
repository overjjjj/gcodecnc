#include "../src/workspaces/process_design/selection/PocketOpening.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/strategies/mill/PocketFinishStrategy.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/workspaces/program_verification/generation/RegionToolpathValidation.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <STEPControl_Writer.hxx>

class InjectedFinish : public PocketFinishStrategy {
public:
    QString text;
    ToolpathResult generate(const ContourFeature &,const ToolEntry &,const StrategyParams &)const override {
        ToolpathResult result;result.ok=true;result.gcode=text;return result;
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTemporaryDir dir;int failures=0;
    auto check=[&](bool ok,const QString &label){QTextStream(stdout)<<(ok?"PASS ":"FAIL ")<<label<<Qt::endl;if(!ok)++failures;};
    const auto shape=BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(40,30,12).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(5,5,4),30,20,10).Shape()).Shape();
    STEPControl_Writer writer;writer.Transfer(shape,STEPControl_AsIs);
    const QString file=dir.path()+"/source.step";
    if(writer.Write(file.toLocal8Bit().constData())!=IFSelect_RetDone)return 2;
    StepImporter importer;if(!importer.load(file))return 3;
    SelectionBoundaryLoop opening;bool found=false;
    for(const auto &loop:importer.mesh().boundaryLoops)if(verifyPocketOpening(importer.mesh(),loop,&opening,nullptr)){found=true;break;}
    if(!found)return 4;
    auto strategy=std::make_shared<InjectedFinish>();
    ToolEntry tool;tool.id=1;tool.type="end_mill";tool.diameter=4;tool.fluteLen=30;tool.totalLen=60;
    ProgramGenerationService service([&](const QString &){return strategy;},[&](int){return tool;});
    Cq8PostProcessor post;PostProcessorOptions options;options.workOffset="G54";
    options.safeStartBlocks=QStringList{"G17 G40 G49 G80","G21","G90","G54","G94"};
    ProgramGenerationSnapshotOptions snapshot;snapshot.sourceMesh=&importer.mesh();
    MachiningOperation op;op.id="verified-finish-final-gate";op.opType=OperationType::Contour;op.toolId=1;
    op.params=strategy->defaultParams();op.params.set("depth",8);op.params.set("entryMode",0);
    op.params.set("sideStockToLeave",.2);op.params.set("floorStockToLeave",.5);
    op.selectionEvidence.boundaryLoops={opening};
    for(int i=0;i<opening.orderedEdgeIds.size();++i) {
        SelectionTopologyRef ref;ref.kind=SelectionTopologyKind::Edge;ref.id=opening.orderedEdgeIds[i];
        ref.forward=opening.orderedForward[i];op.selectionEvidence.orderedTopologyRefs.append(ref);
    }
    auto &f=op.contourFeature;f.subType="verified_slot_opening";f.center=machiningBoundaryCenter(opening.machiningBoundary);
    f.points=opening.machiningBoundary;f.depth=8;f.axis={0,0,1};f.region=FaceRegion::Front;f.planarBoundaryVerified=true;
    for(const auto &id:QStringList{"mill_pocket_finish","mill_pocket_floor_finish"}) {
        op.strategyId=id;
        for(const auto &kind:QStringList{"valid","wall","floor","rapid"}) {
            double minimumX=f.points.first().x();for(const auto &point:f.points)minimumX=std::min(minimumX,double(point.x()));
            const QString x=QString::number(kind=="wall"?minimumX+1:f.center.x(),'f',3);
            const QString z=QString::number(f.center.z()-8+.5-(kind=="floor"?.001:0),'f',3);
            const QString y=QString::number(f.center.y(),'f',3),endY=QString::number(f.center.y()+3,'f',3);
            strategy->text="T1 M6\nS1200 M3\nM8\nG0 Z50\nG0 X"+x+" Y"+y+"\nG1 Z"+z+" F100\n"+
                (kind=="rapid"?"G0 Y":"G1 Y")+endY+" F100\nG0 Z50\n";
            const auto result=service.generate({op},post,options,snapshot);
            if(result.ok && kind!="valid") {
                QTextStream(stdout)<<result.snapshot.gcodeText<<Qt::endl;
                SimulationController parser;parser.loadGCode(result.snapshot.gcodeText,false);
                for(const auto &move:parser.moves())QTextStream(stdout)<<"MOVE "<<move.sourceLine<<" "<<regionMoveError(move,f,op.params,tool.diameter)<<Qt::endl;
            }
            QTextStream(stdout)<<id<<" "<<kind<<" "<<result.errors.join(" | ")<<Qt::endl;
            check(result.ok==(kind=="valid"),id+" final NC "+kind);
            check(result.ok || (result.snapshot.gcodeText.isEmpty() && result.snapshot.expandedGcodeText.isEmpty()),"rejected finish exposes no partial program");
        }
    }
    QTextStream(stdout)<<"FAILURES="<<failures<<Qt::endl;return failures?1:0;
}
