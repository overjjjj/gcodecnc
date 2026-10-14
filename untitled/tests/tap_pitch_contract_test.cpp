#include "../src/strategies/hole/TappingStrategy.h"
#include <QCoreApplication>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    TappingStrategy strategy;
    HoleFeature hole; hole.region = FaceRegion::Front; hole.radius = 5; hole.depth = 20; hole.center = QVector3D(0, 0, -10);
    ToolEntry tool; tool.id = 1; tool.type = "tap"; tool.diameter = 10;
    tool.pitch = 1.25; tool.fluteLen = 80; tool.totalLen = 120;
    const auto params = strategy.defaultParams();
    if (!strategy.generate(hole, tool, params).ok) return 2;
    hole.pitch = 1.5;
    if (strategy.generate(hole, tool, params).ok) {
        std::cerr << "FAIL direct strategy accepted recognized pitch mismatch\n"; return 1;
    }
    tool.pitch = 1.5;
    if (!strategy.generate(hole, tool, params).ok) return 3;
    std::cout << "PASS unknown pitch, recognized mismatch, matching pitch\n";
    return 0;
}
