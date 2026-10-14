#include <QApplication>
#include <QOpenGLWidget>
#include <QOpenGLFunctions_1_1>
#include <QMatrix4x4>
#include <QSet>
#include "../src/import/StepImporter.h"
#include "../src/core/SketchDocument.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#define private public
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#undef private
#include <cmath>
#include <iostream>

static int failures = 0;
static void check(bool ok, const char *message) {
    std::cout << (ok ? "PASS " : "FAIL ") << message << std::endl;
    if (!ok) ++failures;
}
static bool contains(const ViewportWidget &v, const QVector<QVector3D> &points) {
    for (const auto &p : points) {
        const QVector4D clip = v.m_proj * v.currentViewMatrix() * QVector4D(p, 1);
        if (!(clip.w() > 0)) return false;
        const auto ndc = clip.toVector3DAffine();
        if (!std::isfinite(ndc.x()) || !std::isfinite(ndc.y()) || !std::isfinite(ndc.z())
            || std::abs(ndc.x()) > .85 || std::abs(ndc.y()) > .85 || std::abs(ndc.z()) > 1) return false;
    }
    return true;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    ViewportWidget v;
    MeshData wire;
    wire.bbMin = {-20,-10,1}; wire.bbMax = {20,10,1};
    MeshPickEdge edge; edge.id="edge:1"; edge.points={wire.bbMin, wire.bbMax}; wire.pickEdges.append(edge);
    MeshData mesh = wire;
    Triangle t; t.v0=wire.bbMin; t.v1=wire.bbMax; t.v2={20,-10,1}; mesh.triangles.append(t);
    const QVector<QVector3D> path={{25,10,1},{30,5,30},{10,0,9000},{12,1,8000}};
    const QVector<QVector3D> starts={{25,10,1},{-300,10,1},{10,0,9000}};
    check(starts.size()==path.size()-1,"independent starts match actual rendered segment contract");
    const auto all = QVector<QVector3D>{wire.bbMin,wire.bbMax} + path + starts;
    for (const QSize size : {QSize(240,900), QSize(1600,400)}) {
        v.resize(size);
        for (const auto &source : {wire, mesh}) {
            v.clearToolPath(); v.setMesh(source);
            v.setStandardView(ViewportWidget::StandardView::Top);
            v.setToolPath(path, {false,true,true}, starts);
            check(contains(v,all), "new path auto-fit contains source, independent starts and high Z");
            for (auto view : {ViewportWidget::StandardView::Top, ViewportWidget::StandardView::Isometric}) {
                v.setStandardView(view);
                check(contains(v,all), "explicit fit contains complete source and path in wide/narrow viewport");
            }
        }
    }
    v.setToolPosition({9000,0,0}); v.setToolPathVisibility({false,false,false});
    v.clearToolPath();
    ViewportWidget sourceOnly; sourceOnly.resize(v.size()); sourceOnly.setMesh(mesh);
    sourceOnly.setStandardView(ViewportWidget::StandardView::Isometric);
    check(v.m_toolPath.isEmpty() && v.m_toolPathStarts.isEmpty() && v.m_toolPathVisible.isEmpty() && !v.m_hasToolPos,
          "clear removes path starts visibility and marker");
    check(v.m_pan == sourceOnly.m_pan && std::abs(v.m_zoom-sourceOnly.m_zoom)<1e-6,
          "clear restores source-only fit instead of old far-path range");
    v.setToolPath(path,{false,true,true},starts);
    v.setToolPath({{20,0,1},{21,0,1}},{false},{{20,0,1}});
    check(v.m_pan.x()>-30 && v.m_pan.z()>-2,"path A to small path B discards A range");
    v.m_autoFit=false; v.m_pan={-5,-6,-7}; v.m_zoom=.77f;
    v.setToolPath(path,{false,true,true},starts);
    check(v.m_pan==QVector3D(-5,-6,-7) && v.m_zoom==.77f && !v.m_autoFit,
          "new path preserves manually positioned camera");
    v.clearToolPath();
    check(v.m_pan==QVector3D(-5,-6,-7) && v.m_zoom==.77f && !v.m_autoFit,
          "clear preserves manually positioned camera");
    v.setToolPosition({1,2,3}); v.setToolPath({},{});
    check(!v.m_hasToolPos,"empty controller path removes previous tool marker");
    SimulationController sim;
    QObject::connect(&sim,&SimulationController::toolPathReady,&v,&ViewportWidget::setToolPath);
    sim.loadGCode("G90 G21\nG0 Z20\nX-300 Y10\nG1 X25 Y10 Z1 F100\nM30",false,0,{.1,.2,.3});
    check(!v.m_toolPath.isEmpty() && (v.m_toolPath.last()-QVector3D(25.1,10.2,1.3)).length()<.00001,
          "parser model-space path receives nonzero origin exactly once");
    v.fitAll(); check(contains(v,v.m_toolPath+v.m_toolPathStarts),"fit contains real parser path and starts");
    v.setMesh({}); v.clearToolPath();
    check(v.m_pan.isNull() && v.m_zoom==1.0f && v.m_toolPath.isEmpty(),
          "cleared empty source does not retain distant previous path bounds");
    std::cout<<"FAILURES "<<failures<<std::endl; return failures ? 1 : 0;
}
