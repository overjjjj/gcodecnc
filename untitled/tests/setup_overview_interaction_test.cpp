#include "../src/app/MainWindow.h"
#include "../src/app/AppController.h"
#include <QApplication>
#include <QToolButton>
#include <QPushButton>
#include <QDialog>
#include <QLabel>
#include <QDir>
#include <QTimer>
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include "../src/workspaces/process_design/ui/SetupOriginDialog.h"
#include "../src/workspaces/process_design/ui/StockDefinitionDialog.h"
#include <iostream>
int main(int argc, char **argv) {
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc, argv);
    app.setOrganizationName("CNEXT-UI-Acceptance");
    app.setApplicationName("Setup-module-test");
    MainWindow window;
    window.show(); app.processEvents();
    QToolButton *entry = nullptr;
    for (auto *button : window.findChildren<QToolButton *>())
        if (button->text() == QStringLiteral("加工设置")) entry = button;
    if (!entry) return 1;
    entry->click(); app.processEvents();
    auto *dialog = window.findChild<QDialog *>("setupOverviewDialog");
    if (!dialog || !dialog->isVisible()) { std::cerr << "FAIL setup entry must show setup status and next actions\n"; return 2; }
    if (dialog->isModal()) return 3;
    auto *direction = dialog->findChild<QPushButton *>("setupDirectionAction");
    auto *origin = dialog->findChild<QPushButton *>("setupOriginAction");
    auto *stock = dialog->findChild<QPushButton *>("setupStockAction");
    if (!direction || !origin || !stock || direction->isEnabled() || origin->isEnabled() || stock->isEnabled()) return 4;
    entry->click();
    if (window.findChildren<QDialog *>("setupOverviewDialog").size() != 1) return 5;
    const auto *status = dialog->findChild<QLabel *>("setupOverviewStatus");
    if (!status || !status->text().contains(QStringLiteral("导入"))) return 6;
    auto *project = AppController::instance().projectManager();
    MeshData mesh;
    mesh.bbMin = QVector3D(0,0,-10); mesh.bbMax = QVector3D(40,30,0);
    Triangle triangle; triangle.v0={0,0,0}; triangle.v1={40,0,0}; triangle.v2={0,30,0}; triangle.normal={0,0,1}; triangle.faceIndex=1;
    mesh.triangles.append(triangle);
    project->setMesh(mesh); emit project->projectChanged(); app.processEvents();
    if (!direction->isEnabled() || origin->isEnabled() || stock->isEnabled()) return 7;
    project->setActiveRegion(FaceRegion::Front); emit window.activeRegionChanged(FaceRegion::Front); app.processEvents();
    if (!origin->isEnabled() || stock->isEnabled()) return 8;
    SetupOrigin setupOrigin; setupOrigin.confirmed=true;
    project->setSetupOrigin(setupOrigin); app.processEvents();
    if (!stock->isEnabled()) return 9;
    StockDefinition definition; definition.confirmed=true; definition.plusZ=2;
    project->setStockDefinition(definition); app.processEvents();
    if (!status->text().contains(QStringLiteral("可选择加工几何"))) return 10;
    if (argc > 1) { QDir().mkpath(QString::fromLocal8Bit(argv[1])); dialog->grab().save(QString::fromLocal8Bit(argv[1])+"/setup-ready-test-fixture.png"); }
    project->setSetupOrigin(SetupOrigin()); app.processEvents();
    if (stock->isEnabled() || status->text().contains(QStringLiteral("可选择加工几何"))) return 11;
    if (argc > 1) dialog->grab().save(QString::fromLocal8Bit(argv[1])+"/setup-incomplete-test-fixture.png");
    dialog->close(); app.processEvents();
    if (argc > 2) {
        app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
        AppController::instance().importStep(QString::fromLocal8Bit(argv[2])); app.processEvents();
        if (project->mesh().isEmpty()) return 20;
        entry->click(); app.processEvents();
        dialog = window.findChild<QDialog *>("setupOverviewDialog");
        dialog->findChild<QPushButton *>("setupDirectionAction")->click();
        ViewportWidget *viewport = nullptr;
        for (auto *candidate : window.findChildren<ViewportWidget *>()) if (candidate->isVisible()) viewport = candidate;
        int face = -1; QVector3D point;
        for (const auto &t : project->mesh().triangles) if (t.normal.z() > .99f) { face=t.faceIndex; point=t.v0; break; }
        if (!viewport || face <= 0) return 21;
        emit viewport->facePointPicked(face, point);
        if (project->activeRegion() != FaceRegion::Unknown) return 22;
        emit viewport->facePointPicked(face, point); app.processEvents();
        if (project->activeRegion() != FaceRegion::Front || project->setupOrigin().confirmed || project->stockDefinition().confirmed) return 23;
        bool originSeen=false;
        QTimer::singleShot(0, [&]() {
            for (auto *candidate : window.findChildren<SetupOriginDialog *>()) {
                if (candidate->isVisible()) { originSeen=true; candidate->setWorkOffset("G55"); static_cast<QDialog *>(candidate)->accept(); }
            }
        });
        QMetaObject::invokeMethod(&window, "onEditSetupOrigin", Qt::DirectConnection);
        if (!originSeen || !project->setupOrigin().confirmed || project->workOffset() != "G55") return 24;
        bool stockSeen=false;
        QTimer::singleShot(0, [&]() {
            for (auto *candidate : window.findChildren<StockDefinitionDialog *>()) {
                if (candidate->isVisible()) { stockSeen=true; candidate->setStockDefinition(definition); candidate->accept(); }
            }
        });
        QMetaObject::invokeMethod(&window, "onEditStockDefinition", Qt::DirectConnection);
        if (!stockSeen || !project->stockDefinition().confirmed || project->setupFingerprint().isEmpty()) return 25;
        app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
        entry->click(); app.processEvents();
        dialog = window.findChild<QDialog *>("setupOverviewDialog");
        if (!dialog || !dialog->findChild<QLabel *>("setupOverviewStatus")->text().contains(QStringLiteral("可选择加工几何"))) return 26;
        dialog->grab().save(QString::fromLocal8Bit(argv[1])+"/setup-real-model-ready.png");
        dialog->close();
        std::cout << "PASS real STEP direction double-pick, G55 origin and stock confirmation flow\n";
    }
    std::cout << "PASS setup overview: empty state, prerequisites, live updates, single window and readiness\n";
    return 0;
}
