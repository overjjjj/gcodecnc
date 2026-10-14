#include "../src/strategies/mill/PocketFinishStrategy.h"
#include "../src/strategies/mill/PocketFloorFinishStrategy.h"
#include "../src/workspaces/program_verification/generation/RegionToolpathValidation.h"
#include <QCoreApplication>
#include <QTextStream>
static int failures=0;
static void check(bool ok,const char *label){QTextStream(stdout)<<(ok?"PASS ":"FAIL ")<<label<<Qt::endl;if(!ok)++failures;}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);PocketFinishStrategy wall;PocketFloorFinishStrategy floor;
    ToolEntry tool;tool.id=1;tool.type="end_mill";tool.diameter=4;tool.fluteLen=20;
    ContourFeature f;f.subType="verified_slot_opening";f.center={20,15,10};f.depth=4;
    f.axis={0,0,1};f.planarBoundaryVerified=true;
    const QVector<QVector<QVector3D>> shapes={{{0,0,10},{40,0,10},{40,30,10},{0,30,10}},
        {{5,0,10},{35,0,10},{40,5,10},{40,25,10},{35,30,10},{5,30,10},{0,25,10},{0,5,10}}};
    for(const auto &points:shapes)for(bool reverse:{false,true})for(const StrategyBase *strategy:{static_cast<const StrategyBase *>(&wall),static_cast<const StrategyBase *>(&floor)}) {
        f.points=points;if(reverse)std::reverse(f.points.begin(),f.points.end());
        auto p=strategy->defaultParams();p.set("entryMode",0);p.set("sideStockToLeave",.2);p.set("floorStockToLeave",.5004);
        if(strategy==&floor)p.set("stepover",3.9);
        auto result=strategy->generate(f,tool,p);check(result.ok,"verified convex region supports actual wall/floor finish");
        if(!result.ok)continue;
        SimulationController sim;sim.loadGCode("G17 G90 G94\n"+result.gcode,true);
        check(sim.motionError().isEmpty(),"finish NC parses without motion errors");
        int cuts=0;double lowest=50;
        for(const auto &m:sim.moves()) {
            check(regionMoveError(m,f,p,tool.diameter).isEmpty(),"entire finish move respects region and independent allowances");
            if(!m.isRapid){++cuts;lowest=std::min(lowest,double(m.to.z()));}
        }
        check(cuts>=5 && lowest>=6.5004 && lowest<6.5015,"finish reaches requested floor with safe decimal rounding");
        if(strategy==&floor) {
            auto wide=p;wide.set("stepover",tool.diameter+.1);
            check(!strategy->generate(f,tool,wide).ok,"floor finish rejects a step leaving uncut strips");
            if(points.size()==8) {
                double distance=100;
                for(const auto &m:sim.moves())if(!m.isRapid && m.from.z()<6.502 && m.to.z()<6.502)
                    distance=std::min(distance,regionPointSegmentDistance(QPointF(2.65,2.65),
                        QPointF(m.from.x(),m.from.y()),QPointF(m.to.x(),m.to.y())));
                check(distance<=tool.diameter*.5,"floor perimeter cleanup covers reachable slanted-edge material between raster rows");
            }
        }
        auto missing=p;missing.values.remove("entryMode");
        check(!strategy->generate(f,tool,missing).ok,"verified finish needs explicit entry choice");
        auto helix=p;helix.set("entryMode",1);
        check(!strategy->generate(f,tool,helix).ok,"unverified finish helix remains refused");
        auto big=tool;big.diameter=45;
        check(!strategy->generate(f,big,p).ok,"oversize finish tool rejected");
    }
    f.points=shapes.first();tool.diameter=2.0005;
    auto fractional=floor.defaultParams();fractional.set("entryMode",0);fractional.set("stepover",tool.diameter);
    auto fractionalResult=floor.generate(f,tool,fractional);
    check(fractionalResult.ok,"non-grid diameter floor scan generates");
    if(fractionalResult.ok) {
        SimulationController sim;sim.loadGCode("G17 G90 G94\n"+fractionalResult.gcode,true);
        QVector<double> rows;
        for(const auto &m:sim.moves())if(!m.isRapid && m.from.z()==6 && m.to.z()==6 &&
            m.from.y()==m.to.y() && std::abs(m.to.x()-m.from.x())>20)rows.append(m.to.y());
        std::sort(rows.begin(),rows.end());double largestGap=0;
        for(int i=1;i<rows.size();++i)largestGap=std::max(largestGap,rows[i]-rows[i-1]);
        check(rows.size()>5 && largestGap<=tool.diameter,"rounded NC scan spacing cannot exceed cutter diameter");
    }
    f.points.clear();
    const QVector<QPointF> centers={{3,3},{37,3},{37,27},{3,27}};
    for(int corner=0;corner<4;++corner)for(int sample=0;sample<=12;++sample) {
        const double angle=(180+corner*90+sample*7.5)*std::acos(-1.0)/180;
        f.points.append(QVector3D(centers[corner].x()+3*std::cos(angle),centers[corner].y()+3*std::sin(angle),10));
    }
    tool.diameter=6;
    auto rounded=wall.defaultParams();rounded.set("entryMode",0);rounded.set("sideStockToLeave",.1);
    auto roundedResult=wall.generate(f,tool,rounded);
    check(roundedResult.ok,"round corner smaller than inset still permits a nonempty interior path");
    if(roundedResult.ok) {
        SimulationController sim;sim.loadGCode("G17 G90 G94\n"+roundedResult.gcode,true);
        for(const auto &move:sim.moves())check(regionMoveError(move,f,rounded,tool.diameter).isEmpty(),"collapsed corner remains within source region");
    }
    QTextStream(stdout)<<"FAILURES="<<failures<<Qt::endl;return failures?1:0;
}
