#include "../src/workspaces/program_verification/simulation/SketchMotionCheck.h"
#include <iostream>

int main()
{
    SketchVerificationContext context;
    context.document.id = "10000000-0000-0000-0000-000000000001";
    context.document.contourId = "20000000-0000-0000-0000-000000000001";
    context.document.contourKind = "rectangle";
    context.document.points = {{0,0},{40,0},{40,20},{0,20}};
    context.sourceFingerprint = context.document.fingerprint();
    context.setupFingerprint = "current-confirmed-setup";
    context.stock.confirmed = true;
    context.stock.minusX = context.stock.plusX = context.stock.minusY = context.stock.plusY = 2;
    context.stock.minusZ = 10;
    SketchOperationCheck operation;
    operation.id = "op"; operation.depth = 5;
    operation.selectionFingerprint="current-selection";operation.parameterFingerprint="current-parameters";
    operation.tool.id = 42; operation.tool.type = "end_mill";
    operation.tool.diameter = 4; operation.tool.fluteLen = 20;
    operation.tool.totalLen = 40; operation.tool.shankDia = 6;
    context.operations = {operation};
    const QString nc = "G21 G17 G40 G49 G80 G90 G94 G54\n([op:op])\nT42 M6\nS3000 M3\n"
        "G0 Z20\nG0 X-10 Y-2.001\nG1 Z-4.999 F100\nG1 X42.001 F500\nG1 X-10\nG0 Z20\nM30\n";
    QVector<ToolMove> moves;
    auto move = [&](Coordinate3D from, Coordinate3D to, bool rapid, int line, int fromAxes=7, int toAxes=7) {
        ToolMove m; m.from=from; m.to=to; m.isRapid=rapid; m.toolId=42;
        m.sourceLine=line; m.fromKnownAxes=fromAxes; m.toKnownAxes=toAxes;
        m.commandedAxes=fromAxes==0?4:7;
        moves.append(m);
    };
    move({0,0,0},{0,0,20},true,4,0,4);
    move({0,0,20},{-10,-2.001,20},true,5,4,7);
    move({-10,-2.001,20},{-10,-2.001,-4.999},false,6);
    move({-10,-2.001,-4.999},{42.001,-2.001,-4.999},false,7);
    move({42.001,-2.001,-4.999},{-10,-2.001,-4.999},false,8);
    move({-10,-2.001,-4.999},{-10,-2.001,20},true,9);
    int failures=0;
    auto check=[&](bool ok,const char *name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;};
    auto result=checkSketchMotion(context,moves,nc,{});
    check(result.state==SimulationCheckState::Limited,"known safe motions after explicit initial-position limitation");
    if(result.blocksOutput())std::cout<<result.message.toStdString()<<'\n';
    auto bad=moves; bad[2].to.setX(-9.999);
    check(checkSketchMotion(context,bad,nc,{}).blocksOutput(),"low G1 sloping entry rejected even outside stock");
    bad=moves; bad[3].isRapid=true;
    check(checkSketchMotion(context,bad,nc,{}).blocksOutput(),"low G0 horizontal traversal rejected");
    bad=moves; bad[2].from.setX(-2.001);bad[2].to.setX(-2.001);
    check(checkSketchMotion(context,bad,nc,{}).blocksOutput(),"Outside retained contour is insufficient for vertical entry through stock");
    bad=moves; bad[3].from={-3,10,-4.999};bad[3].to={43,10,-4.999};
    check(checkSketchMotion(context,bad,nc,{}).blocksOutput(),"full segment crossing retained interior rejected");
    bad=moves; bad[3].from.setY(-2);bad[3].to.setY(-2);
    check(checkSketchMotion(context,bad,nc,{}).blocksOutput(),"exact nominal cutter contact is not accepted");
    bad=moves;bad[2].to.setZ(-10.001);
    check(checkSketchMotion(context,bad,nc,{}).blocksOutput(),"stock and requested depth bounds reject overcut");
    auto changed=context; changed.operations[0].tool.fluteLen=3;
    check(checkSketchMotion(changed,moves,nc,{}).blocksOutput(),"low shank cannot rely on presumed removed stock");
    changed=context;changed.operations[0].tool.modelPath="custom.stl";
    check(checkSketchMotion(changed,moves,nc,{}).blocksOutput(),"custom tool geometry unsupported by nominal check");
    bad=moves;bad[3].toolId=43;
    check(checkSketchMotion(context,bad,nc,{}).blocksOutput(),"actual parsed tool must match owner operation");
    check(checkSketchMotion(context,moves,QString(nc).replace("[op:op]","[op:unknown]"),{}).blocksOutput(),"unknown owner cannot claim geometry permission");
    check(checkSketchMotion(context,moves,QString(nc).replace("G1 X42.001","G2 X42.001"),{}).blocksOutput(),"original arc mode cannot masquerade as expanded straight segments");
    check(checkSketchMotion(context,moves,QString(nc).replace("G40","G41"),{}).blocksOutput(),"machine compensation is outside supported sketch mode");
    check(checkSketchMotion(context,moves,QString(nc).replace("X42.001","X9007199254740993"),{}).blocksOutput(),"unrepresentable decimal coordinate is rejected before checking rounded motion");
    changed=context;changed.sourceFingerprint="stale";
    check(checkSketchMotion(changed,moves,nc,{}).blocksOutput(),"stale source binding rejected");
    check(checkSketchMotion(context,{moves.first()},nc,{}).blocksOutput(),"only unknown initial positioning cannot obtain output permission");
    changed=context;changed.modelOrigin={9007199254740992.0,0,0};
    bad=moves;bad[2].from={0,-100,-1};bad[2].to={.001,-100,-2};
    check(checkSketchMotion(changed,bad,nc,changed.modelOrigin).blocksOutput(),"origin addition cannot hide low G1 XY movement");
    bad[2].isRapid=true;
    check(checkSketchMotion(changed,bad,nc,changed.modelOrigin).blocksOutput(),"origin addition cannot hide low G0 XY movement");
    bad=moves; bad.removeFirst(); bad[0].fromKnownAxes=0;
    check(checkSketchMotion(context,bad,nc,{}).blocksOutput(),"unknown initial XYZ move cannot substitute for safe Z establishment");
    for (const QString &mode : {QString("G90"),QString("G21"),QString("G40"),QString("G54")})
        check(checkSketchMotion(context,moves,QString(nc).replace(mode,""),{}).blocksOutput(),
              "required fixed mode must be established before motion");
    changed=context; changed.workOffset="G91";
    check(checkSketchMotion(changed,moves,QString(nc).replace("G54","G91"),{}).blocksOutput(),
          "work offset cannot authorize incremental mode");
    bad=moves; bad[0].commandedAxes=7;
    check(checkSketchMotion(context,bad,nc,{}).blocksOutput(),"initial safe Z block cannot command unknown XY");
    for (const QString &end : {QString("M2"),QString("M30")})
        check(checkSketchMotion(context,moves,QString(nc).replace("M30","G1 X20 Y10 "+end),{}).blocksOutput(),
              "program end cannot conceal an unsimulated same-block motion");
    check(checkSketchMotion(context,moves,nc+"G1 X20 Y10\n",{}).blocksOutput(),
          "executable content after program end cannot escape checking");
    changed=context;changed.operations[0].selectionFingerprint.clear();
    check(checkSketchMotion(changed,moves,nc,{}).blocksOutput(),"missing current selection evidence rejected");
    std::cout<<"FAILURES "<<failures<<'\n';
    return failures?1:0;
}
