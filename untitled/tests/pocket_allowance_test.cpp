#include "../src/strategies/mill/PocketRoughingStrategy.h"
#include "../src/workspaces/program_verification/generation/RegionToolpathValidation.h"
#include <QCoreApplication>
#include <QTextStream>
#include <limits>

static int failures=0;
static void check(bool ok,const char *message) {
    QTextStream(stdout)<<(ok?"PASS ":"FAIL ")<<message<<Qt::endl;
    if(!ok)++failures;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    PocketRoughingStrategy strategy;
    ToolEntry tool;tool.id=1;tool.type="end_mill";tool.diameter=4;tool.fluteLen=20;
    ContourFeature feature;feature.subType="rectangular_pocket";
    feature.center={20,15,10};feature.length=40;feature.width=30;feature.depth=4;
    const QVector<QVector3D> boundary={{0,0,10},{40,0,10},{40,30,10},{0,30,10}};
    auto params=strategy.defaultParams();params.set("entryMode",0);params.set("stockToLeave",.25);
    auto inspect=[&](const StrategyParams &p,double bottom,double clearance) {
        auto result=strategy.generate(feature,tool,p);
        check(result.ok,"rough path generates");if(!result.ok)return;
        SimulationController sim;sim.loadGCode("G17 G90 G94\n"+result.gcode,true);check(sim.motionError().isEmpty(),"actual NC parses");
        double minZ=50,minX=50;int cuts=0;
        for(const auto &move:sim.moves())if(!move.isRapid && (move.toKnownAxes&4)) {
            minZ=std::min(minZ,double(move.to.z()));
            if(move.to.z()<10) {minX=std::min(minX,double(move.to.x()));++cuts;}
        }
        check(cuts>0 && minZ>=bottom && minZ<bottom+.00101,"floor allowance changes actual final Z without rounding down");
        check(minX>=clearance-.00001 && minX<clearance+.002,"side allowance changes actual boundary reach");
    };
    for(bool polygon:{false,true}) {
        feature.points=polygon?boundary:QVector<QVector3D>{};
        inspect(params,6,2.25); // Legacy stock has always applied only to XY.
        auto explicitParams=params;explicitParams.set("sideStockToLeave",1);explicitParams.set("floorStockToLeave",.75);
        inspect(explicitParams,6.75,3);
        explicitParams.set("floorStockToLeave",.7504);
        inspect(explicitParams,6.7504,3);
        for(const char *key:{"sideStockToLeave","floorStockToLeave"})for(double invalid:{-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
            auto bad=explicitParams;bad.set(key,invalid);
            check(!strategy.generate(feature,tool,bad).ok,"invalid independent allowance rejected");
        }
        explicitParams.set("floorStockToLeave",4);
        check(!strategy.generate(feature,tool,explicitParams).ok,"floor allowance consuming all depth rejected");
    }
    feature.points=boundary;
    auto p=params;p.set("sideStockToLeave",1);p.set("floorStockToLeave",.75);
    ToolMove move;move.from={3,5,6.75};move.to={3,25,6.75};
    check(regionMoveError(move,feature,p,4).isEmpty(),"final motion preserves exact separate allowances");
    move.from.setX(2.9);move.to.setX(2.9);
    check(!regionMoveError(move,feature,p,4).isEmpty(),"final motion rejects side allowance consumption");
    move.from={3,5,6.749};move.to={3,25,6.749};
    check(!regionMoveError(move,feature,p,4).isEmpty(),"final motion rejects floor allowance consumption");
    QTextStream(stdout)<<"FAILURES="<<failures<<Qt::endl;return failures?1:0;
}
