#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <iostream>
#include <limits>
#include "../src/core/MachiningCoordinates.h"
#include "../src/app/AppController.h"
#include "../src/app/MillingBatchCommit.h"
#include "../src/strategies/StrategyFactory.h"
#include "../src/strategies/mill/FaceMillingStrategy.h"
#include "../src/workspaces/process_design/ui/OperationEditDialog.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/gcode/ProgramSnapshotStatus.h"
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x<<std::endl;return 1;}}while(0)

int main(int argc,char **argv)
{
    QApplication app(argc,argv);QTemporaryDir temp;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
    ToolEntry tool;tool.id=917;tool.type="end_mill";tool.diameter=4;tool.fluteLen=12;tool.totalLen=50;
    ToolLibrary::instance().addTool(tool);
    FaceMillingStrategy face;
    MachiningOperation op;op.id="face";op.opType=OperationType::Contour;op.strategyId="mill_face";op.toolId=tool.id;
    op.params=face.defaultParams();op.params.set("depth",1);op.params.set("stepover",2);op.params.set("safeHeight",50);op.params.set("feedHeight",5);
    auto &f=op.contourFeature;f.subType="flat_surface";f.center={0,0,100};f.axis={0,0,1};f.region=FaceRegion::Front;
    f.length=40;f.width=20;f.depth=1;f.planarBoundaryVerified=true;
    f.points={{-20,-10,100},{20,-10,100},{20,10,100},{-20,10,100}};
    auto *project=AppController::instance().projectManager();MeshData mesh;mesh.bbMin={-20,-10,90};mesh.bbMax={20,10,100};project->setLoadedMesh(mesh);
    SetupOrigin origin;origin.anchor=SetupOriginAnchor::CustomPoint;origin.customPoint={0,0,100};origin.confirmed=true;project->setSetupOrigin(origin);
    CHECK(operationEditError(op,op.params,tool.id).isEmpty());
    bool committed=false;
    CHECK(validateAndCommitMillingBatch({op},face,tool,[&](const QList<MachiningOperation>&ops){committed=true;return QStringList{ops.first().id};},machiningOrigin(project)).ok);
    CHECK(committed);
    ProgramGenerationService service([](const QString&id){return StrategyFactory::instance().strategy(id);},[&](int){return tool;});
    Cq8PostProcessor post;PostProcessorOptions options;options.workOffset="G54";
    options.safeStartBlocks=QStringList{"G17 G40 G49 G80","G21","G90","G54","G94"};
    ProgramGenerationSnapshotOptions snapshot;snapshot.stockConfirmed=true;snapshot.stockBounds={mesh.bbMin,mesh.bbMax};snapshot.modelOrigin={0,0,100};
    auto result=service.generate({op},post,options,snapshot);
    if(!result.ok)std::cerr<<result.errors.join(" | ").toStdString()<<std::endl;
    CHECK(result.ok && result.snapshot.coordinatesVerified());
    CHECK(result.snapshot.gcodeText.contains("Z-1.000"));
    CHECK(result.snapshot.sourceOperationFingerprint==ProgramSnapshotFingerprint::calculate({op},{op.id}));
    snapshot.modelOrigin={0,0,-100};
    CHECK(!service.generate({op},post,options,snapshot).ok);
    // Absolute safe Z=2 is above work top=-10 plus a 5 mm clearance.
    snapshot.modelOrigin={0,0,110};origin.customPoint=snapshot.modelOrigin;project->setSetupOrigin(origin);op.params.set("safeHeight",2);
    CHECK(operationEditError(op,op.params,tool.id).isEmpty());
    CHECK(service.generate({op},post,options,snapshot).ok);
    op.params.set("safeHeight",50);snapshot.modelOrigin={10,-5,102};
    const auto shifted=service.generate({op},post,options,snapshot);CHECK(shifted.ok);
    CHECK(shifted.snapshot.gcodeText.contains("X-32.000 Y-7.000"));
    CHECK(shifted.snapshot.gcodeText.contains("Z-3.000"));
    CHECK(shifted.snapshot.gcodeText.contains("Z3.000")); // feedHeight remains a distance.
    CHECK(shifted.snapshot.gcodeText.contains("G0 Z50.000"));
    // All six stock allowances keep their signed dimensions under the same translation.
    StockDefinition stock;stock.minusX=1;stock.plusX=2;stock.minusY=3;stock.plusY=4;stock.minusZ=5;stock.plusZ=6;
    const auto modelStock=stock.resolvedBounds(mesh.bbMin,mesh.bbMax);
    const auto workStock=inWorkCoordinates(modelStock,snapshot.modelOrigin);
    CHECK(workStock.minimum==QVector3D(-31,-8,-17));CHECK(workStock.maximum==QVector3D(12,19,4));
    CHECK(workStock.size()==modelStock.size());
    snapshot.stockBounds=modelStock;CHECK(!service.generate({op},post,options,snapshot).ok); // entry disk intersects enlarged XY stock
    snapshot.stockBounds={mesh.bbMin,mesh.bbMax};snapshot.stockBounds.maximum.setZ(152);
    CHECK(!service.generate({op},post,options,snapshot).ok); // safe Z=50 reaches stock top=50
    snapshot.modelOrigin.setX(std::numeric_limits<float>::quiet_NaN());CHECK(!service.generate({op},post,options,snapshot).ok);
    // Hole loops and contour islands share the point transform, while dimensions/axes do not.
    HoleFeature hole;hole.center={10,20,30};hole.depth=8;hole.radius=5;hole.axis={0,0,1};hole.boundaryPoints={{1,2,3}};hole.islandBoundaries={{{4,5,6}}};
    const auto wh=inWorkCoordinates(hole,{10,-5,2});CHECK(wh.center==QVector3D(0,25,28));
    CHECK(wh.depth==8 && wh.radius==5 && wh.axis==hole.axis && wh.boundaryPoints[0]==QVector3D(-9,7,1));
    CHECK(wh.islandBoundaries[0][0]==QVector3D(-6,10,4));
    ToolEntry drill=tool;drill.id=918;drill.type="drill";ToolLibrary::instance().addTool(drill);
    MachiningOperation drilled;drilled.id="drill";drilled.opType=OperationType::Hole;drilled.strategyId="hole_g81";drilled.toolId=drill.id;
    drilled.holeFeature=hole;drilled.holeFeature.center={0,0,-5};drilled.holeFeature.depth=10;drilled.holeFeature.region=FaceRegion::Front;
    drilled.params=StrategyFactory::instance().strategy("hole_g81")->defaultParams();drilled.params.set("safeHeight",2);drilled.params.set("feedHeight",5);
    origin.customPoint={};project->setSetupOrigin(origin);
    CHECK(!operationEditError(drilled,drilled.params,drill.id).isEmpty());
    origin.customPoint={0,0,10};project->setSetupOrigin(origin);
    CHECK(operationEditError(drilled,drilled.params,drill.id).isEmpty());
    auto explicitDepth=drilled.holeFeature;explicitDepth.center={0,0,0};explicitDepth.depth=0;
    auto explicitParams=drilled.params;explicitParams.set("depth",20);explicitParams.set("safeHeight",6);
    CHECK(!workHeightError(explicitDepth,explicitParams,"hole_g81").isEmpty());
    CHECK(!workHeightError(explicitDepth,explicitParams,"hole_peck").isEmpty());
    auto contour=op;contour.strategyId="mill_closed_contour";contour.params=StrategyFactory::instance().strategy(contour.strategyId)->defaultParams();
    contour.params.set("safeHeight",2);contour.params.set("feedHeight",5);
    origin.customPoint={0,0,100};project->setSetupOrigin(origin);
    CHECK(!operationEditError(contour,contour.params,tool.id).isEmpty());
    snapshot.modelOrigin={0,0,-100};contour.params.set("safeHeight",50);
    const auto unsafe=service.generate({contour},post,options,snapshot);
    CHECK(!unsafe.ok && unsafe.snapshot.id.isEmpty());
    contour.params.set("safeHeight",2);
    origin.customPoint={0,0,110};project->setSetupOrigin(origin);
    CHECK(operationEditError(contour,contour.params,tool.id).isEmpty());
    std::cout<<"PASS work-coordinate generation, absolute safe Z, relative clearance, source fingerprints and stock bounds"<<std::endl;
    return 0;
}
