#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <iostream>
#include <cstdlib>
#include <limits>
#include <QComboBox>
#include "../src/app/MainWindow.h"
#define private public
#include "../src/workspaces/process_design/ui/ContourMachiningChoiceDialog.h"
#undef private
#include "../src/workspaces/process_design/ui/ContourMachiningChoice.h"
#include "../src/workspaces/process_design/ui/OperationParameterGroups.h"
#include "../src/strategies/mill/ClosedContourMillingStrategy.h"
#include "../src/strategies/mill/ContourSideGeometry.h"
#include "../src/strategies/StrategyFactory.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"

static void require(bool value,const char *message)
{
    if(!value){std::cerr<<"FAIL "<<message<<std::endl;std::exit(1);}
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    ContourFeature source;source.subType="closed_contour";source.center={5,5,0};
    source.axis={0,0,1};source.region=FaceRegion::Front;source.depth=1;
    source.points={{0,0,0},{10,0,0},{10,10,0},{0,10,0}};
    ToolEntry tool;tool.id=44;tool.type="end_mill";tool.diameter=4;tool.fluteLen=10;tool.totalLen=50;
    ClosedContourMillingStrategy strategy;
    ProgramGenerationService service([](const QString &id){return StrategyFactory::instance().strategy(id);},
        [&](int){return tool;});
    Cq8PostProcessor post;PostProcessorOptions options;options.workOffset="G54";
    options.safeStartBlocks=QStringList{"G17 G40 G49 G80","G21","G90","G54","G94"};
    ProgramGenerationSnapshotOptions snapshot;
    for(auto side:{ChainMachiningSide::Inside,ChainMachiningSide::Outside})
    for(auto direction:{ContourTraversalDirection::Forward,ContourTraversalDirection::Reverse})
    for(auto mode:{ContourCompensationChoice::CamOffsetG40,ContourCompensationChoice::LeftG41}) {
        ContourFeature feature=source;
        auto params=strategy.defaultParams();params.set("stockToLeave",mode==ContourCompensationChoice::CamOffsetG40?0.5:0);
        ContourMachiningChoice choice;choice.machiningSide=side;choice.direction=direction;
        choice.compensation=mode;choice.startPointIndex=2;
        applyContourMachiningChoice(feature,params,choice);
        require(feature.points.first()==QVector3D(10,10,0),"Confirmed start must survive reordering");
        require(feature.points[1]==(direction==ContourTraversalDirection::Forward ? QVector3D(0,10,0):QVector3D(10,0,0)),
                "Confirmed direction must survive reordering");
        MachiningOperation operation;operation.id="contour";operation.opType=OperationType::Contour;
        operation.strategyId="mill_closed_contour";operation.toolId=tool.id;
        operation.params=params;operation.contourFeature=feature;operation.selectionEvidence.machiningSide=side;
        const auto result=service.generate({operation},post,options,snapshot);
        if(!result.ok)std::cerr<<result.errors.join(" | ").toStdString()<<std::endl;
        require(result.ok,"Inside/outside must generate final CQ8");
        const QString nc=result.snapshot.expandedGcodeText.isEmpty()?result.snapshot.gcodeText:result.snapshot.expandedGcodeText;
        if(mode!=ContourCompensationChoice::CamOffsetG40) {
            auto allowance=operation;allowance.params.set("stockToLeave",0.5);
            require(!service.generate({allowance},post,options,snapshot).ok,"Machine compensation must not silently ignore positive stock allowance");
            const bool left=(side==ChainMachiningSide::Inside)==(direction==ContourTraversalDirection::Forward);
            require(nc.contains(left?"G1 G41 D44":"G1 G42 D44"),"Control compensation must match final traversal and side");
            // The controller's D register is not known here. These are nominal
            // path/output assertions, never a claim about the actual center path.
        } else {
            require(!nc.contains("G41 D")&&!nc.contains("G42 D"),"CAM must retain G40 output mode");
            SimulationController simulation;simulation.loadGCode(nc,false);
            require(simulation.motionError().isEmpty()&&!simulation.moves().isEmpty(),"Final CAM NC must parse");
            bool cutting=false;
            for(const auto &move:simulation.moves()) {
                if(move.isRapid || move.to.z()>0)continue;
                cutting=true;
                const auto &a=move.from;const auto &b=move.to;
                if(side==ChainMachiningSide::Inside) {
                    for(const auto &point:{a,b})
                        require(point.x()>=2.5-1e-5&&point.x()<=7.5+1e-5&&point.y()>=2.5-1e-5&&point.y()<=7.5+1e-5,
                                "Entire entry/plunge/cut/exit must retain radius plus stock inside rectangle");
                } else {
                    require((a.x()<=-2.5+1e-5&&b.x()<=-2.5+1e-5)||(a.x()>=12.5-1e-5&&b.x()>=12.5-1e-5)||
                            (a.y()<=-2.5+1e-5&&b.y()<=-2.5+1e-5)||(a.y()>=12.5-1e-5&&b.y()>=12.5-1e-5),
                            "Every exterior cutting segment must clear the stock-expanded rectangle");
                }
            }
            require(cutting,"Final CAM path must contain cutting motion");
        }
        auto tampered=operation;tampered.params.set("stockToLeave",0);tampered.params.set("compensation",-contourCutterSide(feature,side));
        require(!service.generate({tampered},post,options,snapshot).ok,"Generation must reject compensation conflicting with saved side");
    }
    auto params=strategy.defaultParams();params.set("compensation",0);params.set("camOffsetSide",1);
    auto large=tool;large.diameter=12;
    require(!strategy.generate(source,large,params).ok,"Oversize inward cutter must be rejected before emitting NC");
    large.diameter=10;
    require(!strategy.generate(source,large,params).ok,"Collapsed inward offset must be rejected");
    params.set("leadLength",20);
    require(!strategy.generate(source,tool,params).ok,"Inward lead crossing the allowed region must be rejected");
    params.set("leadLength",5);params.set("stockToLeave",4);
    require(!strategy.generate(source,tool,params).ok,"Tool radius plus positive allowance must fit");
    params.set("stockToLeave",0);
    auto island=source;island.islands={{{2,6,0},{4,6,0},{4,8,0},{2,8,0}}};
    require(!strategy.generate(island,tool,params).ok,"Inward plunge or lead through an island must be rejected");
    auto concave=source;concave.points={{0,0,0},{10,0,0},{10,10,0},{5,5,0},{0,10,0}};
    require(!strategy.generate(concave,tool,params).ok,"Concave CAM offset is explicitly unsupported");
    auto invalidHeight=source;invalidHeight.center.setZ(std::numeric_limits<float>::quiet_NaN());
    require(!strategy.generate(invalidHeight,tool,params).ok,"Nonfinite top height must never emit Znan");
    auto repeated=source;repeated.points+=source.points;
    require(!strategy.generate(repeated,tool,params).ok,"Repeated polygon traversal is not a simple contour");
    auto fallback=source;fallback.points.clear();fallback.center={0,0,0};fallback.radius=20;
    params.set("stockToLeave",2);params.set("camOffsetSide",-1);
    const auto fallbackResult=strategy.generate(fallback,tool,params);
    require(fallbackResult.ok&&fallbackResult.gcode.contains("X-24.000 Y-24.000")&&fallbackResult.gcode.contains("X24.000 Y24.000"),
            "Fallback outer rectangle must offset original radius by cutter radius plus stock exactly once");
    require(!contourMachiningSideError(source,false,ChainMachiningSide::Inside).isEmpty(),"Open chains cannot guess inside/outside");
    for(auto compensation:{ContourCompensationChoice::LeftG41,ContourCompensationChoice::RightG42,ContourCompensationChoice::CamOffsetG40}) {
        auto feature=source;ContourMachiningChoice mixed;mixed.compensation=compensation;
        applyContourMachiningChoice(feature,params,mixed);
        const double expected=compensation==ContourCompensationChoice::LeftG41?1:compensation==ContourCompensationChoice::RightG42?-1:0;
        require(params.get("compensation")==expected,"Mixed must preserve the explicitly selected compensation mode");
    }
    require(!operationParameterContract("mill_closed_contour","camOffsetSide").editable,"Derived CAM side must not be batch editable");
    require(operationParameterContract("mill_closed_contour","compensation").options[1].contains("CAM"),"G40 must be labelled as CAM geometry offset");
    if(argc>2 && QString::fromLocal8Bit(argv[1])=="--screenshot") {
        MainWindow styleProvider;
        ContourMachiningChoiceDialog dialog(&styleProvider);
        dialog.setAttribute(Qt::WA_DontShowOnScreen);
        dialog.setChineseUi(true);
        auto displayParams=strategy.defaultParams();
        dialog.setContext("mill_closed_contour",source,displayParams,ChainMachiningSide::Outside);
        dialog.show();app.processEvents();
        require(dialog.grab().save(QString::fromLocal8Bit(argv[2])+"-machine.png"),"Save nominal compensation UI");
        dialog.m_compensationCombo->setCurrentIndex(dialog.m_compensationCombo->findData(int(ContourCompensationChoice::CamOffsetG40)));
        app.processEvents();
        require(dialog.grab().save(QString::fromLocal8Bit(argv[2])+"-cam.png"),"Save CAM compensation UI");
    }
    std::cout<<"PASS CAM radius/stock/entry/exit and explicit controller-compensation output contracts"<<std::endl;
    return 0;
}
