#include "src/core/SketchSelectionBinding.h"
#include <iostream>

int main()
{
    SketchDocument document;
    document.id="10000000-0000-0000-0000-000000000001";
    document.contourId="20000000-0000-0000-0000-000000000001";
    document.contourKind="rectangle";
    document.points={{100000.001,0},{100040.001,0},{100040.001,20},{100000.001,20}};
    SelectionChain selection;
    selection.geometrySource=ChainGeometrySource::Wire;
    selection.machiningSide=ChainMachiningSide::Outside;
    selection.closed=true;selection.explicitUserSelection=true;
    selection.selectedSurfaceNormal={0,0,1};
    selection.sourceFingerprint=document.fingerprint();selection.setupFingerprint="setup";
    selection.hasStartAnchor=true;
    const QString prefix="sketch:"+document.id+":"+document.contourId;
    selection.startAnchor.topologyId=prefix+":vertex:0";
    for(int i=0;i<4;++i){
        const QString edge=prefix+":edge:"+QString::number(i);
        selection.orderedTopologyRefs.append({edge,SelectionTopologyKind::Edge,true});
        selection.orderedGeometryIds.append(edge);
    }
    StrategyParams params;params.set("compensation",0);params.set("camOffsetSide",-1);
    int failures=0;
    auto check=[&](bool ok,const char *name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;};
    auto bind=[&](const SelectionChain &value){return bindSketchSelection(document,value,params,"setup","G54");};
    auto good=bind(selection);
    check(good.error.isEmpty() && good.points.size()==4 && good.points.first().x()==100000.001,
          "complete current wire reconstructs original double points");
    auto bad=selection;bad.orderedTopologyRefs.removeLast();
    check(!bind(bad).error.isEmpty(),"partial chain rejected");
    bad=selection;bad.orderedTopologyRefs[1]=bad.orderedTopologyRefs[0];
    bad.orderedGeometryIds[1]=bad.orderedGeometryIds[0];
    check(!bind(bad).error.isEmpty(),"duplicate edge rejected");
    bad=selection;std::swap(bad.orderedTopologyRefs[1],bad.orderedTopologyRefs[2]);
    std::swap(bad.orderedGeometryIds[1],bad.orderedGeometryIds[2]);
    check(!bind(bad).error.isEmpty(),"disconnected order rejected");
    bad=selection;bad.orderedTopologyRefs[2].forward=false;
    check(!bind(bad).error.isEmpty(),"inconsistent traversal direction rejected");
    bad=selection;bad.sourceFingerprint="old";
    check(!bind(bad).error.isEmpty(),"stale document binding rejected");
    bad=selection;bad.setupFingerprint="old";
    check(!bind(bad).error.isEmpty(),"stale setup binding rejected");
    bad=selection;bad.toolAxis={.001f,0,1};
    check(!bind(bad).error.isEmpty(),"nearby axis is not fixed positive Z");
    bad=selection;bad.machiningSide=ChainMachiningSide::Inside;
    check(!bind(bad).error.isEmpty(),"only outside retained contour supported");
    bad=selection;bad.startAnchor.topologyId=prefix+":edge:0";
    check(!bind(bad).error.isEmpty(),"float edge anchor without parameter rejected");
    bad.startAnchor.hasCurveParameter=true;bad.startAnchor.curveParameter=.25;
    good=bind(bad);
    check(good.error.isEmpty() && good.points.size()==5 && good.points.first().x()==100010.001,
          "edge parameter reconstructs double start and splits the full loop");
    bad.startAnchor.curveParameter=1.1;
    check(!bind(bad).error.isEmpty(),"out-of-range edge parameter rejected");
    auto revised=document;++revised.revision;
    check(!bindSketchSelection(revised,selection,params,"setup","G54").error.isEmpty(),
          "old selection rejected after actual document revision");
    revised=document;revised.points[0].setX(100000.0011);revised.points[3].setX(100000.0011);
    check(float(revised.points[0].x())==float(document.points[0].x()) &&
          !bindSketchSelection(revised,selection,params,"setup","G54").error.isEmpty(),
          "unchanged float display cannot hide a changed original boundary");
    bad=selection;
    std::reverse(bad.orderedTopologyRefs.begin(),bad.orderedTopologyRefs.end());
    std::reverse(bad.orderedGeometryIds.begin(),bad.orderedGeometryIds.end());
    for(auto &ref:bad.orderedTopologyRefs) ref.forward=false;
    auto reverseParams=params;reverseParams.set("camOffsetSide",1);
    good=bindSketchSelection(document,bad,reverseParams,"setup","G54");
    check(good.error.isEmpty() && good.points==QVector<Coordinate3D>{{100000.001,0,0},{100000.001,20,0},
          {100040.001,20,0},{100040.001,0,0}},"complete reversed wire reconstructs full ordered double loop");
    std::cout<<"FAILURES "<<failures<<'\n';
    return failures?1:0;
}
