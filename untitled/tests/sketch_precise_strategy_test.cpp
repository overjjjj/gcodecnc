#include "src/strategies/mill/ClosedContourMillingStrategy.cpp"
#include <iostream>

int main()
{
    ClosedContourMillingStrategy strategy;
    ContourFeature feature;
    feature.center={100000.001,0,0};feature.depth=5;
    feature.precisePoints={{100000.001,0,0},{100040.001,0,0},{100040.001,20,0},{100000.001,20,0}};
    ToolEntry tool;tool.id=42;tool.diameter=4;
    auto params=strategy.defaultParams();params.set("compensation",0);params.set("camOffsetSide",-1);
    int failures=0;
    auto check=[&](bool ok,const char *name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;};
    const auto result=strategy.generate(feature,tool,params);
    if(!result.ok) std::cout<<result.errorMsg.toStdString()<<'\n';
    check(result.ok,"same strategy entry consumes authoritative precise contour");
    check(result.gcode.contains("X99997.999 Y-2.002"),"outside offset preserves original large-coordinate double boundary");
    check(result.gcode.contains("G1 Z-4.999"),"last depth remains strictly above declared bottom after quantization");
    params.set("compensation",1);
    check(!strategy.generate(feature,tool,params).ok,"precise sketch branch refuses machine compensation");
    params.set("compensation",0);
    for(const QString &key:{QString("feedRate"),QString("plungeRate"),QString("spindleSpeed")}) {
        auto invalid=params;invalid.set(key,.5);
        check(!strategy.generate(feature,tool,invalid).ok,"fractional output rate cannot silently become zero");
        invalid.set(key,1e30);
        check(!strategy.generate(feature,tool,invalid).ok,"output rate beyond integer range is rejected");
    }
    std::cout<<"FAILURES "<<failures<<'\n';
    return failures?1:0;
}
