#include <QApplication>
#include <QOpenGLWidget>
#include <QMatrix4x4>
#include <QSet>
#include <QVector3D>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#undef private
#include <cmath>
#include <iostream>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    ViewportWidget viewport;
    MeshData mesh;
    mesh.bbMin = QVector3D(-1500, -20, -3);
    mesh.bbMax = QVector3D(1500, 20, 3);
    Triangle triangle;
    triangle.v0 = mesh.bbMin;
    triangle.v1 = mesh.bbMax;
    triangle.v2 = QVector3D(1500, -20, -3);
    mesh.triangles.append(triangle);
    const QList<ViewportWidget::StandardView> views = {
        ViewportWidget::StandardView::Front, ViewportWidget::StandardView::Back,
        ViewportWidget::StandardView::Left, ViewportWidget::StandardView::Right,
        ViewportWidget::StandardView::Top, ViewportWidget::StandardView::Bottom,
        ViewportWidget::StandardView::Isometric
    };
    for (const QSize size : {QSize(240, 900), QSize(1600, 400)}) {
        viewport.resize(size);
        viewport.setMesh(mesh);
        for (auto view : views) {
            viewport.setStandardView(view);
            QMatrix4x4 camera;
            camera.translate(0, 0, -200.0f / viewport.m_zoom);
            camera.rotate(viewport.m_rotX, 1, 0, 0);
            camera.rotate(viewport.m_rotZ, 0, 0, 1);
            camera.translate(viewport.m_pan);
            for (int mask = 0; mask < 8; ++mask) {
                const QVector3D p(mask & 1 ? mesh.bbMax.x() : mesh.bbMin.x(),
                                  mask & 2 ? mesh.bbMax.y() : mesh.bbMin.y(),
                                  mask & 4 ? mesh.bbMax.z() : mesh.bbMin.z());
                const auto projected = viewport.m_proj.map(camera.map(p));
                if (std::abs(projected.x()) > 0.85f || std::abs(projected.y()) > 0.85f || std::abs(projected.z()) > 1) {
                    std::cerr << "Bounds clipped or missing fit margin\n";
                    return 1;
                }
            }
        }
    }
    return 0;
}
