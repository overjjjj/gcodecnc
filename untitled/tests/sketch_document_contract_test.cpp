#include "../src/core/SketchDocument.h"
#include <QCoreApplication>
#include <iostream>

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);int failures=0;
    auto check=[&](bool ok,const char *message){if(!ok){++failures;std::cerr<<"FAIL "<<message<<std::endl;}};
    SketchDocument valid;valid.id="f7c542a4-eb97-4307-9bbe-60cffd7f7e30";valid.contourId="ac7c7ca3-7a5c-40f0-9610-501ff029007d";
    valid.contourKind="rectangle";valid.points={{0,0},{40,0},{40,20},{0,20}};
    check(valid.validationError().isEmpty(),"rectangle accepted");
    auto wire=valid.wireGeometry();check(wire.triangles.isEmpty()&&wire.pickEdges.size()==4,"wire has real edges without triangles");
    auto candidate=valid;candidate.points={{100000000,0},{100000001,0},{100000001,1},{100000000,1}};
    check(!candidate.validationError().isEmpty(),"float-collapsed rectangle cannot become restored wire");
    candidate=valid;candidate.contourKind="polyline";candidate.points={{0,0},{40,20},{40,0},{0,20}};
    check(!candidate.validationError().isEmpty(),"self-intersection rejected");
    candidate.points={{0,0},{20,0},{10,0},{20,20},{0,20}};
    check(!candidate.validationError().isEmpty(),"adjacent backtracking rejected");
    candidate.points={{0,0},{20,0},{20,20},{0,20},{20,0}};
    check(!candidate.validationError().isEmpty(),"repeated vertex rejected");
    candidate=valid;candidate.points[1].setX(std::numeric_limits<double>::quiet_NaN());
    check(!candidate.validationError().isEmpty(),"NaN coordinate rejected");
    candidate=valid;candidate.planeZ=std::numeric_limits<double>::max();
    check(!candidate.validationError().isEmpty(),"unrepresentable plane rejected");
    candidate=valid;candidate.points[1].setY(1);
    check(!candidate.validationError().isEmpty(),"non-rectangle cannot claim rectangle type");
    candidate=valid;candidate.points={{.123456789,.25},{40.123456789,.25},{40.123456789,20.25},{.123456789,20.25}};
    check(candidate.validationError().isEmpty(),"non-float-exact double coordinates retain valid topology");
    SketchDocument restored;QString error;
    check(SketchDocument::fromJson(candidate.toJson(),&restored,&error)&&restored.points==candidate.points&&restored.fingerprint()==candidate.fingerprint(),
          "persistence retains original double values and identity");
    const auto oldFingerprint=candidate.fingerprint();candidate.revision++;
    check(candidate.fingerprint()!=oldFingerprint,"revision changes source identity");
    auto malformed=valid.toJson();malformed["revision"]=1.5;
    check(!SketchDocument::fromJson(malformed,&restored,&error),"fractional revision rejected");
    malformed=valid.toJson();malformed["planeZ"]="0";
    check(!SketchDocument::fromJson(malformed,&restored,&error),"string coordinate rejected without coercion");
    candidate=valid;candidate.contourId.clear();candidate.contourKind.clear();candidate.points.clear();
    check(candidate.validationError().isEmpty()&&candidate.wireGeometry().pickEdges.isEmpty(),"empty document accepted as no machining geometry");
    std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
