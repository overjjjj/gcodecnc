#include "../src/strategies/mill/BlindSlotMillingStrategy.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <iostream>
#include <cmath>
#include <vector>
#include <iomanip>
static int failures=0;
static void check(bool ok,const QString &msg) { if(!ok) { ++failures; std::cerr<<msg.toStdString()<<'\n'; } }
static void verify(const QString &code,const QString &name, const ContourFeature &f, const ToolEntry &tool, bool helix, bool comp=false) {
 double x=0,y=0,z=50, cleared=0,layer=0,maxAngle=0; int motion=0, ramps=0; bool slope=false, rough=false, bottom=false, wall=false;
 const double a=f.angle*std::acos(-1.)/180., r=tool.diameter/2.;
 auto local=[&](double px,double py) {return std::make_pair((px-f.center.x())*std::cos(a)+(py-f.center.y())*std::sin(a),-(px-f.center.x())*std::sin(a)+(py-f.center.y())*std::cos(a));};
 auto inside=[&](double px,double py,double pz,double stock) {auto uv=local(px,py);double d=std::max(0.,-pz/f.depth);return uv.first>=-f.length/2.+f.slopeStartLength*d+r+stock-2e-6 && uv.first<=f.length/2.-f.slopeEndLength*d-r-stock+2e-6 && uv.second>=-f.width/2.+f.slopeMinWidth*d+r+stock-2e-6 && uv.second<=f.width/2.-f.slopeMaxWidth*d-r-stock+2e-6;};
 struct ClearedLine {double x,y,nx,ny,z;}; std::vector<ClearedLine> clearedLines;
 QRegularExpression word("([A-Z])(-?[0-9]+(?:\\.[0-9]+)?)"), layerRe("rough layer [0-9]+/[0-9]+ Z=(-?[0-9.]+)");
 for(QString line:code.split('\n')) {
  if(line.contains("rough layer")) { cleared=layer; layer=layerRe.match(line).captured(1).toDouble(); rough=true;slope=false;bottom=false;wall=false; }
  if(line.contains("bottom finish")) { cleared=layer; rough=false;slope=false;bottom=true;wall=false; }
  if(line.contains("slope finish")) slope=true;
  if(line.contains("wall finish")) { slope=false;rough=false;bottom=false;wall=true; }
  line=line.section(';',0,0).section('(',0,0); double nx=x,ny=y,nz=z; bool xyz=false;
  auto it=word.globalMatch(line); while(it.hasNext()) {auto m=it.next(); auto w=m.captured(1); double v=m.captured(2).toDouble(); if(w=="G" && v>=0 && v<=3)motion=int(v);if(w=="X"){nx=v;xyz=true;}if(w=="Y"){ny=v;xyz=true;}if(w=="Z"){nz=v;xyz=true;} }
  if(xyz && motion==1 && nz<z-1e-8 && nz<=0. && !slope) {
   const double xy=std::hypot(nx-x,ny-y);
   if(bottom || wall)check(xy<=2e-6,name+" finish descends diagonally instead of entering cleared bottom: "+line);
   if(xy<=2e-6 && nz< -2e-6) {
    check(nz>=cleared-2e-6,name+" vertical plunge below cleared layer");
    check(inside(nx,ny,nz,rough?.1:0.),name+" vertical approach outside previously cleared rectangle");
    if(rough) {
     bool swept=false;
     for(const auto &segment:clearedLines) {
      if(std::abs(segment.z-nz)>2e-6)continue;
      double dx=segment.nx-segment.x,dy=segment.ny-segment.y,den=dx*dx+dy*dy;
      if(den<1e-12)continue;
      double t=std::max(0.,std::min(1.,((nx-segment.x)*dx+(ny-segment.y)*dy)/den));
      swept|=std::hypot(nx-segment.x-t*dx,ny-segment.y-t*dy)<=2e-6;
     }
     check(swept,name+" vertical approach lacks a previous full cutter sweep");
    }
   }
  }
  if(xyz && (motion==1 || motion==2 || motion==3) && !comp) {
   if(z<=0.)check(inside(x,y,z,rough?.1:0.),name+" segment start outside cutter envelope: "+line);
   if(nz<=0.)check(inside(nx,ny,nz,rough?.1:0.),name+" segment end outside cutter envelope: "+line);
  }
  if(xyz && motion==1 && nz<z-1e-8 && nz<cleared-2e-6 && !slope) {
   double a=std::atan2(z-nz,std::hypot(nx-x,ny-y))*180/std::acos(-1.); maxAngle=std::max(maxAngle,a);++ramps;
   check(a<=3.+1e-8,name+" entry exceeds 3 deg: "+QString::number(a,'f',8)+" "+line);
   check(rough,name+" finish enters uncleared material vertically");
   check(inside(x,y,layer,.1)&&inside(nx,ny,layer,.1),name+" ramp leaves current shrunken layer rectangle");
   check(std::abs(z-cleared)<=2e-6,name+" ramp starts above previous cleared layer");
  }
  if(xyz && motion==0 && std::hypot(nx-x,ny-y)>1e-6)check(std::min(z,nz)>=3.-2e-6,name+" unsafe XY rapid");
  if(xyz && motion==1 && std::abs(nz-z)<1e-8 && z<0.)clearedLines.push_back({x,y,nx,ny,z});
  x=nx;y=ny;z=nz;
 }
 if(!helix)check(ramps>1,name+" missing multilayer ramp coverage");
 std::cout<<name.toStdString()<<" ramps="<<ramps<<" maxAngle="<<std::setprecision(10)<<maxAngle<<'\n';
}
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv); BlindSlotMillingStrategy strategy; Cq8PostProcessor post; QString out=app.arguments().value(1,"outputs/slot-batch2-verification/ramp-green");QDir().mkpath(out);
 int serial=0;
 for(bool real:{true,false}) for(double depth:(real?std::initializer_list<double>{.5,.3}:std::initializer_list<double>{4.,2.})) for(double angle:{-117.175,0.,45.,90.}) for(bool slopes:{false,true}) {
  if(real && slopes)continue;
  ContourFeature f;f.subType="blind_slot";f.length=real?2.776853:40;f.width=real?1.948602:12;f.depth=real?5:4;f.center=QVector3D(11,-7,0);f.angle=angle;
  if(slopes){f.slopeStartLength=2;f.slopeEndLength=1;f.slopeMinWidth=.4;f.slopeMaxWidth=.6;}
  ToolEntry t;t.id=6;t.diameter=real?1:6;
  auto p=strategy.defaultParams();p.values.remove("slotLength");p.values.remove("slotWidth");p.values.remove("angle");p.set("depth",depth);p.set("stepDown",real?.1:2.);p.set("stepover",real?.3:2.);p.set("stockToLeave",.1);
  QString name=QString("case-%1-real%2-depth%3-angle%4-slope%5").arg(serial++).arg(real).arg(depth).arg(angle).arg(slopes);
  auto r=strategy.generate(f,t,p);check(r.ok,name+" generate: "+r.errorMsg);if(!r.ok)continue;
  auto nc=post.wrapGCode({r.gcode},{});QFile file(out+"/"+name+".nc");file.open(QIODevice::WriteOnly);file.write(nc.toUtf8());file.close();verify(nc,name,f,t,false);
 }
 ContourFeature f;f.subType="blind_slot";f.length=40;f.width=12;f.depth=4;ToolEntry t;t.id=6;t.diameter=6;auto p=strategy.defaultParams();p.values.remove("slotLength");p.values.remove("slotWidth");p.set("depth",4);
 for(double comp:{-1.,-1.e-9,-.5e-9,0.,1.e-9,1.}) {p.set("compensation",comp);auto result=strategy.generate(f,t,p);if(comp<=-1.e-9)check(!result.ok && result.gcode.isEmpty() && result.errorMsg.contains("CAM"),"G42 rejected without partial G-code; use CAM offset");else check(result.ok,"CAM/G41 retained at compensation threshold");}
 for(double comp:{1.}) {p.set("compensation",comp);auto r=strategy.generate(f,t,p);check(r.ok,"comp existing supported fixture");if(r.ok){auto nc=post.wrapGCode({r.gcode},{});QFile file(out+QString("/comp%1.nc").arg(comp));file.open(QIODevice::WriteOnly);file.write(nc.toUtf8());file.close();verify(nc,QString("comp%1").arg(comp),f,t,false,true);}}
 p.set("compensation",0);p.set("helixRadius",2);auto r=strategy.generate(f,t,p);check(r.ok&&r.gcode.contains("G2 X"),"helix accepted");if(r.ok)verify(post.wrapGCode({r.gcode},{}),"helix",f,t,true);p.set("helixRadius",3);check(!strategy.generate(f,t,p).ok,"helix stock collision rejected");p.set("helixRadius",0);f.length=6.21;check(!strategy.generate(f,t,p).ok,"unrealistically short ramp rejected");
 p.set("helixRadius",0);p.set("stockToLeave",.3);t.diameter=1;f.length=1.76;f.width=2;check(!strategy.generate(f,t,p).ok,"ramp capacity below minimum step explicitly rejected");
 p.set("stockToLeave",.1);t.diameter=6;f.length=40;f.width=12;f.slopeMinWidth=.4;p.set("compensation",-1);check(!strategy.generate(f,t,p).ok,"sloped negative compensation still rejected");p.set("compensation",1);check(!strategy.generate(f,t,p).ok,"sloped positive compensation still rejected");
 std::cout<<"failures="<<failures<<'\n';return failures?1:0;
}
