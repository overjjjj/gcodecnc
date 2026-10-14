#include "../src/strategies/mill/FaceMillingStrategy.h"
#include "../src/strategies/mill/SurfaceFinishStrategy.h"
#include <QCoreApplication>
#include <iostream>
#include <memory>
#include <limits>
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/postprocessor/SiemensPostProcessor.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok=true;
    ToolEntry tool;tool.id=10;tool.type="end_mill";tool.diameter=10;tool.fluteLen=20;tool.totalLen=60;
    for(const auto &strategy : {std::shared_ptr<StrategyBase>(new FaceMillingStrategy),
                               std::shared_ptr<StrategyBase>(new SurfaceFinishStrategy)}) {
        ContourFeature f;f.center={0,0,0};f.length=40;f.width=20;f.depth=2;
        f.points={{-20,-10,0},{20,-10,0},{20,10,0},{-20,10,0}};
        auto p=strategy->defaultParams();
        auto reject=[&](const ContourFeature &feature,const ToolEntry &t,const StrategyParams &params,const char *name){
            const auto r=strategy->generate(feature,t,params);
            if(r.ok || !r.gcode.isEmpty()){std::cerr<<strategy->id().toStdString()<<": "<<name<<'\n';ok=false;}
        };
        reject(f,tool,p,"F2 preview rectangle without original wire evidence accepted");
        f.planarBoundaryVerified=true;p.set("depth",2);
        if(!strategy->generate(f,tool,p).ok){std::cerr<<"valid original rectangle rejected\n";ok=false;}
        MachiningOperation op;op.id="face";op.opType=OperationType::Contour;
        op.strategyId=strategy->id();op.contourFeature=f;op.params=p;op.toolId=tool.id;
        ProgramGenerationService service([&](const QString &){return strategy;},[&](int){return tool;});
        SiemensPostProcessor post;PostProcessorOptions options;
        if(service.generate({op},post,options,{}).ok){std::cerr<<"final face accepted without confirmed stock bounds\n";ok=false;}
        ProgramGenerationSnapshotOptions snapshot;snapshot.stockConfirmed=true;
        snapshot.stockBounds.minimum={-20,-10,-10};snapshot.stockBounds.maximum={20,10,0};
        if(!service.generate({op},post,options,snapshot).ok){std::cerr<<"valid stock and original rectangle rejected\n";ok=false;}
        snapshot.stockBounds.minimum={-30,-20,-10};
        if(service.generate({op},post,options,snapshot).ok){std::cerr<<"plunge disk intersects stock\n";ok=false;}
        auto bad=p;bad.set("stepover",20);reject(f,tool,bad,"F1 uncovered bands accepted");
        bad=p;bad.set("stockToLeave",-1);reject(f,tool,bad,"F3 negative stock accepted");
        bad=p;bad.set("feedRate",0.5);reject(f,tool,bad,"F3 zero integer feed accepted");
        auto high=f;high.center.setZ(60);reject(high,tool,p,"F3 unsafe absolute height accepted");
        auto wrong=tool;wrong.type="drill";reject(f,wrong,p,"T1 incompatible tool accepted");
        wrong=tool;wrong.fluteLen=1;reject(f,wrong,p,"T1 insufficient flute accepted");
        bad=p;bad.set("stepDown",1);reject(f,tool,bad,"single-layer strategy ignored stepDown");
        for(const auto &key:{"depth","feedHeight","stepover","spindleSpeed","feedRate","plungeRate"})
            for(double v:{0.0,-1.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
                bad=p;bad.set(key,v);auto g=f;if(QString(key)=="depth")g.depth=v;
                reject(g,tool,bad,"invalid finite/positive parameter accepted");
            }
        for(const QVector3D &axis:{QVector3D(),QVector3D(0,0,-1),QVector3D(0.0001f,0,1)}) {
            auto g=f;g.axis=axis;reject(g,tool,p,"non-Z-positive plane accepted");
        }
        auto g=f;g.islands={f.points};reject(g,tool,p,"inner boundary accepted");
        g=f;g.angle=0.01;reject(g,tool,p,"real rotation accepted");
        g=f;g.points[1].setX(19);reject(g,tool,p,"nonrectangular original boundary accepted");
        g=f;g.center.setX(1000000);for(auto &v:g.points)v.setX(v.x()+1000000);
        reject(g,tool,p,"unresolvable large translation accepted");
        g=f;g.center.setX(100);for(auto &v:g.points)v.setX(v.x()+100);
        if(!strategy->generate(g,tool,p).ok){std::cerr<<"resolvable translation rejected\n";ok=false;}
        bad=p;bad.set("stockToLeave",1);wrong=tool;wrong.fluteLen=1;
        if(!strategy->generate(f,wrong,bad).ok){std::cerr<<"actual removal depth not used for flute check\n";ok=false;}
        g=f;g.length=0.4;g.width=0.2;g.points={{-0.2f,-0.1f,0},{0.2f,-0.1f,0},{0.2f,0.1f,0},{-0.2f,0.1f,0}};
        wrong=tool;wrong.diameter=0.1004;bad=p;bad.set("stepover",0.1004);
        reject(g,wrong,bad,"quantized output bands exceed effective diameter");
    }
    if(ok) std::cout<<"PASS face parameter contracts\n";
    return ok?0:1;
}
