#include "OutputWorkflowTestSupport.h"
#include <QOpenGLWidget>
#include <QOpenGLFunctions_1_1>
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/workspaces/process_design/ui/ContourMachiningChoiceDialog.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#undef private
#include <QToolButton>
#include "../src/app/AppController.h"
#include "../src/workspaces/process_design/ui/SketchEditorDialog.h"
#include "../src/workspaces/process_design/ui/SetupOriginDialog.h"
#include "../src/workspaces/process_design/ui/StockDefinitionDialog.h"
#include <QGraphicsView>
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include <QTableWidget>
#include <QLineEdit>
#include <QMouseEvent>
#include <functional>
#include <QComboBox>
#include <QJsonDocument>
#include <QListWidget>
#include "../src/strategies/StrategyFactory.h"

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QTemporaryDir settings;
    isolateOutputWorkflowSettings(settings.path());
    OutputWorkflowDialogs dialogs;
    app.installEventFilter(&dialogs);
    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.resize(1600, 980);
    window.show();
    auto *entry = window.findChild<QToolButton *>("sketchEditorButton");
    if (!entry || !entry->isEnabled()) {
        std::cerr << "FAIL: usable sketch drawing entry is missing" << std::endl;
        return 1;
    }
    const QString evidence = argc > 1 ? QString::fromLocal8Bit(argv[1]) : settings.path();
    QDir().mkpath(evidence);
    int failures = 0;
    auto check = [&](bool result, const char *name) {
        std::cout << (result ? "PASS " : "FAIL ") << name << std::endl;
        if (!result) ++failures;
    };
    QTimer::singleShot(240000, &app, [] { std::cerr << "FAIL timeout" << std::endl; std::_Exit(99); });
    auto *project = AppController::instance().projectManager();
    QComboBox *wcs = nullptr;
    for (auto *combo : window.findChildren<QComboBox *>())
        if (combo->count() == 6 && combo->itemData(0).toString() == "G54") wcs = combo;
    check(wcs != nullptr, "actual WCS toolbar exists");
    if (wcs) wcs->setCurrentIndex(wcs->findData("G55"));
    project->saveToFile(settings.filePath("previous.cnext"));
    auto edit = [&](const std::function<void(SketchEditorDialog *)> &action) {
        window.m_pageNav->setCurrentRow(0);
        bool opened = false;
        QTimer::singleShot(20, [&] {
            auto *d = window.findChild<SketchEditorDialog *>("sketchEditorDialog");
            if (!d) return;
            opened = true; action(d);
            if (d->isVisible()) d->reject();
        });
        entry->click(); check(opened, "real resource entry opens editor");
    };
    auto click = [&](SketchEditorDialog *d, const char *name) {
        auto *b = d->findChild<QPushButton *>(name);
        check(b && b->isEnabled(), name);
        if (b && b->isEnabled()) b->click();
    };
    auto point = [&](SketchEditorDialog *d, double x, double y) {
        auto *view = d->findChild<QGraphicsView *>("sketchCanvas");
        const QPoint pos = view->mapFromScene(QPointF(x, -y));
        check(view->viewport()->rect().contains(pos), "drawing click is inside actual canvas");
        QMouseEvent press(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &release);
    };
    edit([&](SketchEditorDialog *d) {
        auto *initialView = d->findChild<QGraphicsView *>("sketchCanvas");
        check((initialView->mapFromScene(QPointF(40, 20)) - initialView->mapFromScene(QPointF(0, 0))).manhattanLength() > 80,
              "initial automatic canvas fit provides usable drawing scale");
        click(d, "sketchRectangleButton");
        point(d, 0, 0); d->grab().save(evidence + "/first-point.png"); point(d, 40, 20);
        check(project->sourceType() == ProjectSourceType::Step && !project->isModified(), "drawing draft does not alter current project");
        auto *table = d->findChild<QTableWidget *>("sketchPointsTable");
        check(table->rowCount() == 4, "two actual clicks create rectangle");
        table->item(0, 0)->setText("0"); table->item(0, 1)->setText("0");
        table->item(2, 0)->setText("40"); table->item(2, 1)->setText("20");
        d->grab().save(evidence + "/rectangle-editor.png");
        click(d, "saveSketchButton");
    });
    check(project->sourceType() == ProjectSourceType::Sketch && project->sketchDocument().points.size() == 4, "saving creates sketch source");
    if (project->sourceType() != ProjectSourceType::Sketch) return 2;
    check(project->mesh().triangles.isEmpty() && project->mesh().pickEdges.size() == 4, "sketch remains honest wire geometry");
    check(project->workOffset() == "G54" && wcs && wcs->currentData().toString() == "G54", "new sketch resets project and displayed work offset together");
    window.grab().save(evidence + "/wire-source.png");
    const auto original = project->sketchDocument();
    const QString originalFingerprint = project->sourceFileFingerprint();
    const QString saved = settings.filePath("original.cnext");
    check(project->saveToFile(saved), "initial sketch saves");
    edit([&](SketchEditorDialog *d) {
        d->findChild<QTableWidget *>("sketchPointsTable")->item(2, 0)->setText("60");
        d->reject();
    });
    check(project->sourceFileFingerprint() == originalFingerprint && !project->isModified(), "cancel preserves fingerprint and clean state");
    edit([&](SketchEditorDialog *d) {
        auto *table = d->findChild<QTableWidget *>("sketchPointsTable");
        table->item(0, 0)->setText("40");
        check(!d->findChild<QPushButton *>("saveSketchButton")->isEnabled(), "zero-width rectangle blocks save");
        table->item(0, 0)->setText("0");
        check(d->findChild<QPushButton *>("saveSketchButton")->isEnabled() && table->item(2, 0)->text() == "40", "rectangle fixed corner pairs recover from zero width");
        table->item(0, 0)->setText("invalid");
        check(!d->findChild<QPushButton *>("saveSketchButton")->isEnabled(), "invalid coordinate blocks save");
        d->grab().save(evidence + "/invalid-coordinate.png");
        check(!d->findChild<QPushButton *>("undoSketchPointButton")->isEnabled(), "undo is unavailable without pending drawing points");
        click(d, "sketchPolylineButton"); point(d, 0, 0); point(d, 25, 0); point(d, 15, 15);
        click(d, "finishSketchContourButton");
        check(d->findChild<QPushButton *>("saveSketchButton")->isEnabled(), "new valid contour recovers from invalid prior table input");
        d->reject();
    });
    for (int shift = 0; shift < 4; ++shift) {
        for (bool reverse : {false, true}) {
            SketchDocument reordered = original;
            reordered.points.clear();
            for (int i = 0; i < 4; ++i) reordered.points.append(original.points[(shift + (reverse ? 4-i : i)) % 4]);
            SketchEditorDialog d(reordered);
            auto *table = d.findChild<QTableWidget *>("sketchPointsTable");
            const double oldX = reordered.points[0].x();
            table->item(0, 0)->setText(oldX == 0 ? "40" : "0");
            check(!d.findChild<QPushButton *>("saveSketchButton")->isEnabled(), "reordered degenerate rectangle blocks save");
            table->item(0, 0)->setText(QString::number(oldX));
            check(d.findChild<QPushButton *>("saveSketchButton")->isEnabled() && d.document().points == reordered.points,
                  "vertical horizontal reversed and rotated rectangle recover exact coordinates");
        }
    }
    bool originOpened = false;
    QTimer::singleShot(0, [&] {
        for (auto *d : window.findChildren<SetupOriginDialog *>()) if (d->isVisible()) { originOpened = true; static_cast<QDialog *>(d)->accept(); }
    });
    QMetaObject::invokeMethod(&window, "onEditSetupOrigin", Qt::DirectConnection);
    check(originOpened && project->setupOrigin().confirmed, "wire source can confirm work origin");
    bool stockOpened = false;
    QTimer::singleShot(0, [&] {
        for (auto *d : window.findChildren<StockDefinitionDialog *>()) if (d->isVisible()) {
            stockOpened = true; StockDefinition stock; stock.minusZ = 15; stock.plusX = 3;
            d->setStockDefinition(stock); d->accept();
        }
    });
    QMetaObject::invokeMethod(&window, "onEditStockDefinition", Qt::DirectConnection);
    check(stockOpened && project->stockDefinition().confirmed, "wire source can confirm explicit stock");
    auto *contour = window.findChild<QToolButton *>("designFeature_D05.closed");
    check(contour && contour->isEnabled(), "actual closed-contour command is available");
    ToolEntry mill;mill.id=904;mill.type="end_mill";mill.diameter=4;mill.fluteLen=20;mill.totalLen=40;mill.shankDia=6;
    ToolLibrary::instance().removeTool(mill.id);
    check(ToolLibrary::instance().addTool(mill),"test nominal tool is registered");
    auto selectContour=[&]() {
        window.m_pageNav->setCurrentRow(0);
        if(contour) contour->click();
        auto *dialog=dynamic_cast<SelectionChainDialog *>(window.m_geometryDialog.data());
        check(dialog && dialog->isVisible(),"drawn sketch enters the unified contour selection workflow");
        if(!dialog || !dialog->isVisible())return false;
        const auto edge=project->mesh().pickEdges.first();
        dialog->selectTopology(edge.id,int(SelectionTopologyKind::Edge),edge.points.first(),0);
        if(!dialog->m_pendingTopologyPick.isEmpty())dialog->confirmPendingTopologyMatch();
        if(dialog->m_useFullBoundary->isEnabled())dialog->m_useFullBoundary->click();
        dialog->m_side->setCurrentIndex(int(ChainMachiningSide::Outside));
        QMetaObject::invokeMethod(dialog->m_side,"activated",Q_ARG(int,int(ChainMachiningSide::Outside)));
        dialog->accept();
        check(window.m_parameterDialog->isVisible(),"complete actual wire selection opens parameters");
        return window.m_parameterDialog->isVisible();
    };
    auto checkViewport = [&](ViewportWidget *viewport) {
        bool inside=true;
        auto points=viewport->m_toolPath+viewport->m_toolPathStarts;
        points << viewport->m_mesh.bbMin << viewport->m_mesh.bbMax;
        for(const auto &point:points) {
            const auto clip=viewport->m_proj*viewport->currentViewMatrix()*QVector4D(point,1);
            const auto ndc=clip.toVector3DAffine();
            inside &= clip.w()>0 && std::isfinite(ndc.x()) && std::isfinite(ndc.y()) && std::isfinite(ndc.z())
                && std::abs(ndc.x())<=.85 && std::abs(ndc.y())<=.85 && std::abs(ndc.z())<=1;
        }
        check(inside,"actual viewport fits source and every final endpoint and independent start");
    };
    auto establish=[&](bool invalidChoice,int start) {
        auto params=StrategyFactory::instance().strategy("mill_closed_contour")->defaultParams();
        params.set("depth",5);params.set("stepDown",2);params.set("safeHeight",20);params.set("feedHeight",3);
        params.set("leadLength",12);
        window.m_strategyPanel->restoreDraft(params,mill.id);
        bool chosen=false;
        QTimer choose;
        QObject::connect(&choose,&QTimer::timeout,[&] {
            for(auto *widget:QApplication::topLevelWidgets())if(auto *choice=dynamic_cast<ContourMachiningChoiceDialog *>(widget)) {
                if(!choice->isVisible())continue;
                choose.stop();chosen=true;
                choice->m_startCombo->setCurrentIndex(choice->m_startCombo->findData(start));
                choice->m_compensationCombo->setCurrentIndex(choice->m_compensationCombo->findData(int(invalidChoice?
                    ContourCompensationChoice::LeftG41:ContourCompensationChoice::CamOffsetG40)));
                choice->accept();return;
            }
        });choose.start(10);
        window.m_strategyPanel->onConfirmOperation();
        check(chosen,"actual contour machining choice was confirmed");
    };
    if(!selectContour())return 3;
    const auto originalSelection=window.m_workflowSelection;
    establish(true,1);
    check(project->operations().isEmpty(),"unsupported G41 choice does not create operation");
    check(window.m_workflowSelection==originalSelection,"failed confirmation preserves visible original start and direction");
    establish(false,0);
    check(project->operations().size()==1,"retry G40 confirms one actual sketch operation");
    if(project->operations().size()!=1)return 4;
    const auto operation=project->operations().first();
    check(operation.selectionEvidence.startAnchor.topologyId==originalSelection.startAnchor.topologyId &&
          !operation.contourFeature.precisePoints.isEmpty() &&
          operation.contourFeature.precisePoints.first()==Coordinate3D(original.points.first().x(),original.points.first().y(),original.planeZ),
          "retry chosen visible first point matches authoritative generated start");
    window.m_operationPanel->selectOperationById(operation.id);
    QMetaObject::invokeMethod(window.m_operationPanel,"onGenerateProgram",Qt::DirectConnection);
    check(project->programs().size()==1,"confirmed sketch creates a verified final program");
    if(project->programs().size()!=1)return 5;
    const auto program=project->programs().last();
    check(window.m_simCtrl->checkResult().state==SimulationCheckState::Limited,"actual final program enters sketch simulation check");
    window.grab().save(evidence+"/verified-rectangle.png");
    checkViewport(window.m_simViewport);
    check(dialogs.exportProgram(&window,program,evidence+"/initial-export"),"actual sketch export contains exact verified final text");
    edit([&](SketchEditorDialog *d){d->findChild<QTableWidget *>("sketchPointsTable")->item(2,0)->setText("60");click(d,"saveSketchButton");});
    check(window.m_viewport->m_toolPath.isEmpty() && window.m_simViewport->m_toolPath.isEmpty() &&
          !window.m_viewport->m_hasToolPos && !window.m_simViewport->m_hasToolPos,
          "actual source edit removes old paths and tool markers in both viewports");
    check(project->programs().size()==1 && project->programs().first().gcodeText==program.gcodeText,
          "edit retains previously verified program as stale history");
    const int chooserBefore=dialogs.chooserCount, warningBefore=dialogs.warnings.size();
    dialogs.destination=evidence+"/must-not-export.nc";
    QMetaObject::invokeMethod(&window,"onExportGCode",Qt::DirectConnection);
    check(dialogs.chooserCount==chooserBefore && dialogs.warnings.size()>warningBefore && !QFileInfo::exists(dialogs.destination),
          "actual output gate blocks previously exportable program after sketch edit");
    const QString reopen=settings.filePath("machining-reopen.cnext");
    check(project->saveToFile(reopen),"modified sketch and stale program save together");
    dialogs.destination=reopen;
    window.onOpenProject();
    check(project->sourceType()==ProjectSourceType::Sketch && project->sketchDocument().points[2].x()==60 &&
          project->programs().size()==1,"actual open-project workflow restores sketch machining project");
    QTimer::singleShot(0,[&]{for(auto *d:window.findChildren<SetupOriginDialog *>())if(d->isVisible())static_cast<QDialog *>(d)->accept();});
    window.onEditSetupOrigin();
    QTimer::singleShot(0,[&]{for(auto *d:window.findChildren<StockDefinitionDialog *>())if(d->isVisible())d->accept();});
    window.onEditStockDefinition();
    check(project->setupOrigin().confirmed && project->stockDefinition().confirmed,"restored sketch settings explicitly reconfirmed");
    if(!selectContour())return 6;
    establish(false,0);
    check(project->operations().size()==2,"fresh selection creates new operation after restore");
    if(project->operations().size()!=2)return 7;
    window.m_operationPanel->selectOperationById(operation.id);
    auto *operationTree=window.m_operationPanel->findChild<QTreeWidget *>("operationTree");
    check(operationTree && operationTree->currentItem(),"old stale operation is available in the actual operation tree");
    if(operationTree && operationTree->currentItem())operationTree->currentItem()->setCheckState(1,Qt::Unchecked);
    check(!project->operations().first().enabled,"user disables old stale operation before generating fresh batch");
    auto externalOperations=project->operations();externalOperations.first().enabled=true;
    project->setOperations(externalOperations);
    window.m_operationPanel->selectOperationById(operation.id);
    check(operationTree && operationTree->currentItem() && operationTree->currentItem()->checkState(1)==Qt::Checked,
          "external project operation update still refreshes the panel");
    if(operationTree && operationTree->currentItem())operationTree->currentItem()->setCheckState(1,Qt::Unchecked);
    window.m_operationPanel->selectOperationById(project->operations().last().id);
    QMetaObject::invokeMethod(window.m_operationPanel,"onGenerateProgram",Qt::DirectConnection);
    check(project->programs().size()==2,"restored sketch regenerates current final program");
    if(project->programs().size()==2)
        check(dialogs.exportProgram(&window,project->programs().last(),evidence+"/regenerated-export"),
              "actual export recovers after reconfirmation reselection and regeneration");
    if(project->programs().size()!=2)return 8;
    const QString rectangleOperation=project->operations().last().id;
    edit([&](SketchEditorDialog *d) {
        click(d,"deleteSketchContourButton");click(d,"sketchPolylineButton");
        point(d,0,0);point(d,40,0);point(d,40,20);point(d,20,25);point(d,0,20);
        click(d,"finishSketchContourButton");
        auto *plane=d->findChild<QLineEdit *>("sketchPlaneZ");
        check(plane!=nullptr,"actual sketch plane text field exists");
        if(plane) plane->setText("1.000001");
        click(d,"saveSketchButton");
    });
    check(project->sketchDocument().contourKind=="polyline" && project->sketchDocument().points.size()==5 &&
          project->sketchDocument().planeZ==1.000001,"actual editor creates convex sloped-edge contour at noninteger plane");
    QTimer::singleShot(0,[&]{for(auto *d:window.findChildren<SetupOriginDialog *>())if(d->isVisible()){
        SetupOrigin origin;origin.anchor=SetupOriginAnchor::CustomPoint;origin.customPoint={.1,.2,.3};
        d->setOrigin(origin);static_cast<QDialog *>(d)->accept();
    }});
    window.onEditSetupOrigin();
    QTimer::singleShot(0,[&]{for(auto *d:window.findChildren<StockDefinitionDialog *>())if(d->isVisible())d->accept();});
    window.onEditStockDefinition();
    if(!selectContour())return 9;
    establish(false,0);
    check(project->operations().size()==3,"convex sloped-edge sketch confirms through same operation workflow");
    if(project->operations().size()!=3)return 10;
    window.m_operationPanel->selectOperationById(rectangleOperation);
    if(operationTree && operationTree->currentItem())operationTree->currentItem()->setCheckState(1,Qt::Unchecked);
    window.m_operationPanel->selectOperationById(project->operations().last().id);
    QMetaObject::invokeMethod(window.m_operationPanel,"onGenerateProgram",Qt::DirectConnection);
    check(project->programs().size()==3,"convex sloped-edge contour generates with noninteger plane and origin");
    if(project->programs().size()==3) {
        const auto polylineProgram=project->programs().last();
        window.grab().save(evidence+"/verified-polyline.png");
        checkViewport(window.m_simViewport);
        QVector<QVector3D> expectedEndpoints;
        for(const auto &move:window.m_simCtrl->moves())
            if(move.fromKnownAxes==7 && move.toKnownAxes==7)
                expectedEndpoints.append((move.to+polylineProgram.modelOrigin).toVector3D());
        check(window.m_simViewport->m_toolPath.mid(1)==expectedEndpoints,
              "actual noninteger-origin viewport path translates each known final move exactly once");
        window.onSimPlay();
        check(window.m_simCtrl->isRunning(),"actual sketch playback starts");
        QMetaObject::invokeMethod(window.m_simCtrl,"onTick",Qt::DirectConnection);
        check(window.m_simViewport->m_hasToolPos,"playback updates rendered tool marker");
        window.onSimStop();
        window.grab().save(evidence+"/polyline-playback.png");
        check(polylineProgram.modelOrigin==Coordinate3D(.1,.2,.3) && !window.m_simCtrl->checkResult().blocksOutput(),
              "final parser checks exact confirmed noninteger origin");
        check(dialogs.exportProgram(&window,polylineProgram,evidence+"/polyline-export"),
              "actual sloped-edge sketch export passes final motion gate");
    }
    std::cout << "FAILURES " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
