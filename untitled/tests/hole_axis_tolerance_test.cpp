#include "../src/strategies/hole/HoleStrategyUtils.h"
#include <iostream>
int main(){
 HoleFeature f;f.region=FaceRegion::Front;f.depth=20;int failures=0;
 auto check=[&](bool ok,const char *label){if(!ok){++failures;std::cerr<<label<<'\n';}};
 f.axis={.5e-6f,0,1};check(holeAxisOutputError(f).isEmpty(),"numerical near-Z within displacement bound");
 check(!holeAxisOutputError(f,1e9).isEmpty(),"requested depth must also bound drift");
 f.axis={.999e-6f,0,1};check(holeAxisOutputError(f).isEmpty(),"just inside angular bound");
 f.axis={1.001e-6f,0,1};check(!holeAxisOutputError(f).isEmpty(),"just outside angular bound");
 f.axis={1.5e-6f,0,1};check(!holeAxisOutputError(f).isEmpty(),"axis beyond angular bound");
 f.axis={.5e-6f,0,1};f.depth=1e9;check(!holeAxisOutputError(f).isEmpty(),"large depth must not amplify accepted angle");
 f.depth=199;check(holeAxisOutputError(f).isEmpty(),"inside displacement bound");
 f.depth=201;check(!holeAxisOutputError(f).isEmpty(),"outside displacement bound");
 f.axis={0,0,-1};check(holeAxisOutputError(f).isEmpty(),"exact negative Z remains parallel at large depth");
 std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
