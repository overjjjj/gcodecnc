#include "src/core/SetupOrigin.cpp"
#include "src/core/StockDefinition.cpp"
#include <iostream>

int main()
{
    int failures=0;
    auto check=[&](bool ok,const char *name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;};
    SetupOrigin origin;
    origin.anchor=SetupOriginAnchor::TopMinXMinY;
    const auto point=origin.resolvedPoint({100000.001,200000.003,1.000001},{100040.001,200020.003,1.000001});
    check(point.x()==100000.001 && point.y()==200000.003 && point.z()==1.000001,
          "original double sketch bounds survive origin resolution");
    StockDefinition stock;stock.minusX=.25;stock.minusZ=5;
    const auto bounds=stock.resolvedBounds({100000.001,200000.003,1.000001},{100040.001,200020.003,1.000001});
    check(bounds.minimum.x()==100000.001-.25 && bounds.minimum.z()==1.000001-5,
          "stock allowances expand original double bounds");
    const QVector3D low(1,2,3),high(11,22,33);
    check(origin.resolvedPoint(low,high)==Coordinate3D(1,2,33),"STEP float bounds retain anchor behavior");
    std::cout<<"FAILURES "<<failures<<'\n';
    return failures?1:0;
}
