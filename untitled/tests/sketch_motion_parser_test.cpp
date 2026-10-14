#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include <QCoreApplication>
#include <iostream>

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    SketchVerificationContext context;
    context.document.id="10000000-0000-0000-0000-000000000001";
    context.document.contourId="20000000-0000-0000-0000-000000000001";
    context.document.contourKind="rectangle";context.document.points={{0,0},{40,0},{40,20},{0,20}};
    context.sourceFingerprint=context.document.fingerprint();context.setupFingerprint="setup";
    context.stock.confirmed=true;context.stock.minusZ=10;
    context.stock.minusX=context.stock.plusX=context.stock.minusY=context.stock.plusY=2;
    SketchOperationCheck operation;
    operation.id="op";operation.depth=5;operation.selectionFingerprint="selection";operation.parameterFingerprint="params";
    operation.tool.id=42;operation.tool.type="end_mill";operation.tool.diameter=4;
    operation.tool.fluteLen=20;operation.tool.totalLen=40;operation.tool.shankDia=6;
    context.operations={operation};
    const QString nc="G21 G17 G40 G49 G80 G90 G94 G54\n([op:op])\nT42 M6\nS3000 M3\n"
        "G0 Z20\nG0 X-10 Y-2.001\nG1 Z-4.999 F100\nG1 X42.001 F500\nG1 X-10\nG0 Z20\nM30\n";
    int failures=0;
    auto check=[&](bool ok,const char *name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;};
    SimulationController verifier;
    verifier.setSketchVerificationContext(context);verifier.loadGCode(nc,false);
    check(verifier.checkResult().state==SimulationCheckState::Limited,"real parser and sketch checker accept safe complete program");
    if(verifier.checkResult().blocksOutput())std::cout<<verifier.checkResult().message.toStdString()<<'\n';
    check(!verifier.moves().isEmpty() && verifier.moves().last().toolId==42,"real parser retains actual tool ID without project tool lookup");
    check(verifier.checkMatches(nc,{},context.fingerprint()),"cached proof matches exact current context");
    auto translated=context;translated.modelOrigin={.1,.2,.3};
    const QString translatedNc=QString(nc).replace("Z20","Z19.700").replace("X-10","X-10.100")
        .replace("Y-2.001","Y-2.201").replace("Z-4.999","Z-5.299").replace("X42.001","X41.901");
    verifier.setSketchVerificationContext(translated);verifier.loadGCode(translatedNc,false,0,translated.modelOrigin);
    check(!verifier.checkResult().blocksOutput(),"real decimal NC and noninteger origin use bounded model transform");
    auto shortTool=context;shortTool.operations[0].tool.fluteLen=3;
    verifier.setSketchVerificationContext(shortTool);verifier.loadGCode(nc,false);
    check(verifier.checkResult().blocksOutput(),"same text with changed current tool cannot reuse previous clearance proof");
    verifier.setSketchVerificationContext(context);verifier.loadGCode(nc,false);
    auto changed=context;changed.stock.plusX=100;
    verifier.setSketchVerificationContext(changed);
    check(verifier.checkResult().state==SimulationCheckState::NotChecked && !verifier.checkMatches(nc,{},context.fingerprint()),
          "context change invalidates cached result before reloading");
    verifier.setSketchVerificationContext(context);
    for(const QString &end:{QString("M2"),QString("M30")}) {
        const QString mixed=QString(nc).replace("G0 Z20\nM30", "G1 X20 Y10 "+end);
        verifier.loadGCode(mixed,false);
        check(verifier.checkResult().blocksOutput(),"real parser cannot authorize unrepresented motion sharing end block");
    }
    verifier.loadGCode(nc+"G1 X20 Y10\n",false);
    check(verifier.checkResult().blocksOutput(),"real parser cannot authorize trailing unrepresented motion");
    verifier.loadGCode(QString(nc).replace("G0 Z20\nG0 X-10 Y-2.001", "G0 X-10 Y-2.001 Z20"),false);
    check(verifier.checkResult().blocksOutput(),"real parser unknown initial simultaneous XYZ movement rejected");
    verifier.loadGCode(nc,false,10);
    check(verifier.checkResult().blocksOutput(),"sketch rejects external segment offset rather than misbinding source lines");
    verifier.setMesh({});verifier.loadGCode(nc,false);
    check(verifier.checkResult().blocksOutput(),"mesh change clears sketch permission for ordinary external program");
    std::cout<<"FAILURES "<<failures<<'\n';
    return failures?1:0;
}
