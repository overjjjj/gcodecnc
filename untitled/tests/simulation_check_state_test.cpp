#include <QCoreApplication>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#undef private
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x << std::endl; return 1; } } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc,argv);
    MeshData mesh;mesh.bbMin={-2,-2,-1};mesh.bbMax={2,2,10};
    Triangle triangle;triangle.v0={-2,-2,10};triangle.v1={2,-2,10};triangle.v2={0,2,10};
    mesh.triangles={triangle};
    ToolMove retract;retract.isRapid=true;retract.toolDiameter=2;retract.from={0,0,10};retract.to={0,0,50};
    auto result=SimulationController::checkMotion(mesh,{retract});
    CHECK(result.state==SimulationCheckState::Limited && !result.blocksOutput() && result.message.contains("contact"));
    retract.to={0,0,9};
    CHECK(SimulationController::checkMotion(mesh,{retract}).state==SimulationCheckState::Collision);
    retract.from={0,0,9};retract.to={0,0,50};
    CHECK(SimulationController::checkMotion(mesh,{retract}).state==SimulationCheckState::Collision);
    ToolMove move;move.from={-100,0,9};move.to={100,0,9};move.isRapid=true;move.toolDiameter=2;
    result=SimulationController::checkMotion(mesh,{move});
    CHECK(result.state==SimulationCheckState::Collision && result.blocksOutput());
    mesh.triangles.fill(triangle,13202);
    result=SimulationController::checkMotion(mesh,{move});
    CHECK(result.state==SimulationCheckState::Incomplete && result.blocksOutput());
    CHECK(result.message.contains("budget"));
    move.from.setZ(50);move.to.setZ(50);
    CHECK(SimulationController::checkMotion(mesh,{move}).state==SimulationCheckState::Complete);
    ToolMove deep=move;deep.isRapid=false;deep.from={100,0,50};deep.to={100,0,-7};deep.sourceLine=9;
    auto budgetMove=move;budgetMove.from.setZ(9);budgetMove.to.setZ(9);
    result=SimulationController::checkMotion(mesh,{budgetMove,deep});
    CHECK(result.state==SimulationCheckState::Collision && result.message.contains("bottom"));
    CHECK(result.message.contains("10") && result.limitations.join(' ').contains("budget"));
    deep.fromKnownAxes=0;deep.toKnownAxes=4;
    result=SimulationController::checkMotion(mesh,{deep});
    CHECK(result.state==SimulationCheckState::Collision && result.message.contains("bottom"));
    mesh.triangles={triangle};
    result=SimulationController::checkMotion(mesh,{move});
    CHECK(result.state==SimulationCheckState::Complete && !result.blocksOutput());
    auto pointOnly=move;pointOnly.toolDiameter=0;
    result=SimulationController::checkMotion(mesh,{pointOnly});
    CHECK(result.state==SimulationCheckState::Incomplete && result.message.contains("diameter"));
    ToolMove initial=move;initial.fromKnownAxes=0;initial.toKnownAxes=4;initial.to={0,0,50};
    ToolMove positioning=move;positioning.fromKnownAxes=4;positioning.toKnownAxes=7;positioning.to=move.from;
    result=SimulationController::checkMotion(mesh,{initial,positioning,move});
    CHECK(result.state==SimulationCheckState::Limited && !result.blocksOutput());
    auto discontinuity=move;discontinuity.toKnownAxes=4;
    result=SimulationController::checkMotion(mesh,{move,discontinuity});
    CHECK(result.state==SimulationCheckState::Incomplete);
    CHECK(SimulationController::checkMotion(MeshData(),{move}).state==SimulationCheckState::Incomplete);
    CHECK(SimulationController::checkMotion(mesh,{}).state==SimulationCheckState::NotChecked);
    SimulationController parser;parser.setMesh(mesh);parser.loadGCode("G0 Z50\nG0 X-5 Y0\nG2 X5 Y0 R5",false);
    CHECK(parser.checkResult().state==SimulationCheckState::InvalidMotion && parser.moves().isEmpty());
    parser.loadGCode("G0 Z50\nG0 X-5 Y0\nG1 Z9",false);
    CHECK(parser.checkResult().state==SimulationCheckState::Incomplete);
    CHECK(parser.checkMatches(parser.sourceText(),{}));
    parser.setMesh(mesh);CHECK(parser.checkResult().state==SimulationCheckState::NotChecked);
    CHECK(!parser.checkMatches(parser.sourceText(),{}));
    for (const QString &unsupported : {QString("M98 P100"), QString("M 0098 P100"), QString("M99"),
             QString("M17"), QString("L123"), QString("#1=2"), QString("G1 X[#1]"), QString("IF [1 EQ 1] GOTO 10"), QString("G0 A90"), QString("G0 U10")}) {
        parser.loadGCode("G0 Z50\nG0 X-5 Y0\n"+unsupported,false);
        CHECK(parser.checkResult().state==SimulationCheckState::InvalidMotion && parser.moves().isEmpty());
    }
    parser.loadGCode("G0 Z50\nG0 X-5 Y0\nG1 X5 ; M98 #1\n(M99 A90)",false);
    CHECK(parser.motionError().isEmpty() && !parser.moves().isEmpty());
    parser.loadGCode("G0 Z50.\nG0 X-5. Y.0\nG1 X5.",false);
    CHECK(parser.motionError().isEmpty() && parser.moves().last().to.x()==5);
    std::cout<<"PASS collision, budget exhaustion, subsequent/partial-axis deep endpoint, initial-position limitation and result reset\n";
    return 0;
}
