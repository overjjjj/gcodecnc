#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/tool/ToolLibrary.h"
#include <iostream>
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);QTemporaryDir temp;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
 ToolEntry tool;tool.id=987;tool.diameter=2;tool.fluteLen=8;tool.totalLen=12;tool.shankDia=3;ToolLibrary::instance().removeTool(tool.id);ToolLibrary::instance().addTool(tool);
 auto second=tool;second.id=986;second.diameter=3;second.fluteLen=9;ToolLibrary::instance().removeTool(second.id);ToolLibrary::instance().addTool(second);
 int failures=0;auto check=[&](bool ok,const QString &message){if(!ok){++failures;std::cerr<<"FAIL "<<message.toStdString()<<std::endl;}};
 const QString prefix="T987 M6\nG0 X0 Y0 Z50\n";
 for(int id:{998,986})for(const QString &block:{QString("G91 G28 Z0 T%1 M6").arg(id),QString("T%1 M6 G91 G28 Z0").arg(id)}){
  SimulationController sim;sim.loadGCode(prefix+block+"\nG90 G0 Z10\nG1 Z-5\nG0 Z10",true);
  check(sim.motionError().isEmpty()&&!sim.moves().isEmpty(),"G28/T fixture parses");
  if(!sim.moves().isEmpty())check(sim.moves().last().toolDiameter==(id==986?3:0)&&sim.moves().last().fluteLength==(id==986?9:0),"G28 block must process known/unknown T independent of word order");
 }
 for(const QString &number:{QString("987.9"),QString("-1"),QString("2147483648"),QString(400,'9'),QString("nan")}){
  const QString t="T"+number;
  const QStringList branches={t+" G1 Z-5",t+" G98 G81 X0 Y0 Z-5 R3 F40",QString("G98 G81 X0 Y0 Z-5 R3 F40\n")+t+" X1 Y0",QString("MCALL CYCLE81(50,0,3,-5,)\n")+t+" X1 Y0",QString("G91 G28 Z0 ")+t+" M6\nG90 G0 Z10"};
  for(const auto &branch:branches)for(bool projectTools:{true,false}){
   SimulationController sim;sim.loadGCode(prefix+branch,projectTools);
   check(!sim.motionError().isEmpty()&&sim.moves().isEmpty(),"invalid tool must reject all branches: "+number);
  }
 }
 SimulationController external;external.loadGCode(prefix+"G1 Z-5",false);
 check(external.motionError().isEmpty(),"external valid T parses");for(const auto &m:external.moves())check(m.toolDiameter==0&&m.fluteLength==0,"external parsing must not read project tool dimensions");
 std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
