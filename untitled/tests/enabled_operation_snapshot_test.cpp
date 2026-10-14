#include <QCoreApplication>
#include <iostream>
#include "../src/gcode/ProgramSnapshotStatus.h"
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);int failures=0;
 auto check=[&](bool ok,const char*m){std::cout<<(ok?"PASS ":"FAIL ")<<m<<std::endl;failures+=!ok;};
 MachiningOperation a,b;a.id="a";b.id="b";QList<MachiningOperation> ops{a,b};QStringList both{"a","b"},one{"a"};
 const auto allHash=ProgramSnapshotFingerprint::calculate(ops,both);
 auto status=[&](const QStringList&ids,const QString&hash){return ProgramSnapshotStatus::evaluate(ops,ids,hash,{},{},"tool","tool");};
 check(status(both,allHash).okForOutput(),"enabled A+B matches original program");
 ops[1].enabled=false;
 check(!status(both,allHash).okForOutput(),"disabling B invalidates old A+B output");
 const auto activeHash=ProgramSnapshotFingerprint::calculate(ops,one);
 check(status(one,activeHash).okForOutput(),"regenerated active A is complete despite disabled B");
 ops[1].enabled=true;
 check(!status(one,activeHash).okForOutput(),"reenabling B invalidates A-only output");
 ops[0].enabled=false;ops[1].enabled=false;
 check(!status(both,allHash).okForOutput()&&!status({},{}).okForOutput(),"all disabled cannot produce valid empty or previous output");
 return failures?1:0;
}
