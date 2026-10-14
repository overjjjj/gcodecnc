#include "../src/import/StepImporter.h"
#define private public
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include "../src/app/MainWindow.h"
#undef private
#include "../src/app/AppController.h"
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include "../src/workspaces/process_design/ui/SetupOriginDialog.h"
#include "../src/workspaces/process_design/ui/StockDefinitionDialog.h"
#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/tool/ToolLibrary.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QMouseEvent>
#include <QLabel>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QListWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

static void settle()
{
    QEventLoop loop;
    QTimer::singleShot(250, &loop, &QEventLoop::quit);
    loop.exec();
}

static QPoint projectPoint(ViewportWidget &viewport, const QVector3D &point)
{
    const QVector3D ndc = (viewport.m_proj * viewport.currentViewMatrix()).map(point);
    return QPoint(qRound((ndc.x() + 1.0f) * viewport.width() * 0.5f),
                  qRound((1.0f - ndc.y()) * viewport.height() * 0.5f));
}

static const MeshPickEdge *edgeForId(const MeshData &mesh, const QString &id)
{
    for (const MeshPickEdge &edge : mesh.pickEdges)
        if (edge.id == id) return &edge;
    return nullptr;
}

static bool visibleEdgePoint(ViewportWidget &viewport, const MeshPickEdge &edge,
                             bool forward, QPoint *screen)
{
    if (edge.points.size() < 2) return false;
    const int first = forward ? 0 : edge.points.size() - 1;
    const int next = forward ? 1 : edge.points.size() - 2;
    const QVector3D sample = edge.points.at(first) * 0.8f + edge.points.at(next) * 0.2f;
    const QPoint candidateScreen = projectPoint(viewport, sample);
    QString pickedId;
    QVector3D pickedPoint;
    double parameter = 0.0;
    if (!viewport.pickEdgeAt(candidateScreen, &pickedId, &pickedPoint, &parameter) ||
        pickedId != edge.id) return false;
    *screen = candidateScreen;
    return true;
}

static void clickViewport(ViewportWidget &viewport, const QPoint &screen)
{
    // Directly delivered test mouse events bypass the platform's normal focus
    // transfer, so reproduce the StrongFocus mouse-click result explicitly.
    viewport.setFocus(Qt::MouseFocusReason);
    QMouseEvent press(QEvent::MouseButtonPress, screen, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, screen, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &release);
}

static void hoverViewport(ViewportWidget &viewport, const QPoint &screen)
{
    QMouseEvent move(QEvent::MouseMove, screen, Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &move);
}

static bool captureVisibleDesktop(MainWindow &window, SelectionChainDialog &dialog,
                                  const QString &path)
{
    window.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    window.showMaximized();
    dialog.show();
    window.raise();
    window.activateWindow();
    settle();
    QScreen *screen = window.screen();
    return screen && screen->grabWindow(0).save(path);
}

static QRect globalWidgetRect(QWidget &widget)
{
    return QRect(widget.mapToGlobal(QPoint(0, 0)), widget.size());
}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc, argv);
    if (argc < 3) return 1;
    const QString model = QString::fromLocal8Bit(argv[1]);
    const QString output = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(output);

    ToolEntry firstTool;
    firstTool.id = 981;
    firstTool.type = QStringLiteral("end_mill");
    firstTool.name = QStringLiteral("P1 lifecycle D2");
    firstTool.diameter = 2.0;
    firstTool.fluteLen = 50.0;
    firstTool.totalLen = 80.0;
    ToolLibrary::instance().addTool(firstTool);
    ToolEntry selectedTool = firstTool;
    selectedTool.id = 982;
    selectedTool.name = QStringLiteral("P1 lifecycle D3");
    selectedTool.diameter = 3.0;
    ToolLibrary::instance().addTool(selectedTool);

    MainWindow window;
    window.resize(1500, 900);
    window.show();
    AppController &controller = AppController::instance();
    controller.importStep(model);
    settle();
    ProjectManager *project = controller.projectManager();
    if (!project || project->mesh().isEmpty()) return 2;

    QToolButton *setupButton = nullptr;
    for (QToolButton *button : window.findChildren<QToolButton *>())
        if (button->text() == QStringLiteral("加工设置")) setupButton = button;
    ViewportWidget *viewport = nullptr;
    for (ViewportWidget *candidate : window.findChildren<ViewportWidget *>())
        if (candidate->isVisible()) { viewport = candidate; break; }
    if (!setupButton || !viewport) return 3;
    setupButton->click();
    settle();
    auto *overview = window.findChild<QDialog *>(QStringLiteral("setupOverviewDialog"));
    auto *directionAction = overview
        ? overview->findChild<QPushButton *>(QStringLiteral("setupDirectionAction")) : nullptr;
    if (!directionAction) return 3;
    directionAction->click();
    int setupFace = -1;
    QVector3D setupPoint;
    for (const MachiningFeature &feature : project->features()) {
        if (feature.kind != FeatureKind::Slot || feature.axis.normalized().z() < 0.65f) continue;
        for (const Triangle &triangle : project->mesh().triangles) {
            if ((triangle.normal.normalized() - feature.axis.normalized()).length() < 1.0e-6f) {
                setupFace = triangle.faceIndex;
                setupPoint = triangle.v0;
                break;
            }
        }
        if (setupFace >= 0) break;
    }
    if (setupFace < 0) return 3;
    emit viewport->facePointPicked(setupFace, setupPoint);
    emit viewport->facePointPicked(setupFace, setupPoint);
    settle();
    QTimer::singleShot(0, [&window]() {
        for (SetupOriginDialog *dialog : window.findChildren<SetupOriginDialog *>()) {
            if (!dialog->isVisible()) continue;
            dialog->setWorkOffset(QStringLiteral("G54"));
            static_cast<QDialog *>(dialog)->accept();
        }
    });
    QMetaObject::invokeMethod(&window, "onEditSetupOrigin", Qt::DirectConnection);
    QTimer::singleShot(0, [&window]() {
        for (StockDefinitionDialog *dialog : window.findChildren<StockDefinitionDialog *>())
            if (dialog->isVisible()) dialog->accept();
    });
    QMetaObject::invokeMethod(&window, "onEditStockDefinition", Qt::DirectConnection);
    if (!project->setupOrigin().confirmed || !project->stockDefinition().confirmed) return 3;

    const MachiningFeature *slotFeature = nullptr;
    for (const MachiningFeature &feature : project->features()) {
        if (feature.kind == FeatureKind::Slot && feature.axis.normalized().z() > 0.65f) {
            slotFeature = &feature;
            break;
        }
    }
    if (!slotFeature) { std::cerr << "BLOCKED no aligned slot feature\n"; return 3; }
    window.m_activeRegion = slotFeature->region;
    project->setActiveRegion(slotFeature->region);
    emit window.activeRegionChanged(slotFeature->region);

    const MeshBoundaryLoop *outer = nullptr;
    for (const MeshBoundaryLoop &loop : project->mesh().boundaryLoops) {
        if (loop.outer && slotFeature->faceIndices.contains(loop.faceIndex) &&
            !loop.orderedEdgeIds.isEmpty()) {
            outer = &loop;
            break;
        }
    }
    if (!outer) { std::cerr << "BLOCKED slot has no OCC boundary loop\n"; return 4; }
    QSet<QString> otherEdgeIds;
    QSet<QString> otherSlotEdgeIds;
    for (const MeshBoundaryLoop &loop : project->mesh().boundaryLoops) {
        if (slotFeature->faceIndices.contains(loop.faceIndex)) {
            continue;
        }
        for (const MachiningFeature &feature : project->features()) {
            if (&feature != slotFeature && feature.kind != FeatureKind::Hole &&
                feature.axis.z() > 0.5f && feature.faceIndices.contains(loop.faceIndex)) {
                for (const QString &id : loop.orderedEdgeIds) {
                    otherEdgeIds.insert(id);
                    if (feature.kind == FeatureKind::Slot) otherSlotEdgeIds.insert(id);
                }
                break;
            }
        }
    }

    QToolButton *slotButton = nullptr;
    for (QToolButton *button : window.findChildren<QToolButton *>())
        if (button->text() == QStringLiteral("挖槽")) { slotButton = button; break; }
    if (!slotButton) return 6;
    slotButton->click();
    settle();
    SelectionChainDialog *dialog = nullptr;
    for (SelectionChainDialog *candidate : window.findChildren<SelectionChainDialog *>())
        if (candidate->isVisible()) { dialog = candidate; break; }
    if (!dialog) { std::cerr << "BLOCKED no selection dialog\n"; return 7; }
    if (globalWidgetRect(*viewport).intersects(globalWidgetRect(*dialog))) {
        std::cerr << "FAIL selection panel overlaps the production viewport\n";
        return 52;
    }

    if (app.arguments().contains(QStringLiteral("--entry-check"))) {
        QSet<int> faces;
        for (const MeshBoundaryLoop &loop : project->mesh().boundaryLoops) faces.insert(loop.faceIndex);
        auto *choices = dialog->findChild<QListWidget *>(QStringLiteral("selectionCandidates"));
        std::cout << "ENTRY candidates=" << choices->count() << " topology-faces=" << faces.size()
                  << " topology-edges=" << project->mesh().pickEdges.size() << std::endl;
        for (const auto &feature : project->features())
            if (feature.kind == FeatureKind::Slot)
                std::cout << "SLOT faces=" << feature.faceIndices.size() << std::endl;
        if (choices->count() < faces.size()) {
            std::cerr << "FAIL real milling entry filters topology by recognition labels\n";
            return 23;
        }
        QVector<QVector3D> preview;
        bool previewClosed = false;
        const auto originalPreview = dialog->topologyPreviewChanged;
        dialog->topologyPreviewChanged = [&](const QVector<QVector3D> &points, bool closed) {
            preview = points;
            previewClosed = closed;
            if (originalPreview) originalPreview(points, closed);
        };
        auto *mode = dialog->findChild<QComboBox *>(QStringLiteral("selectionMode"));
        mode->setCurrentIndex(static_cast<int>(ChainSelectionMode::Face));
        QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, static_cast<int>(ChainSelectionMode::Face)));
        QSet<int> recognizedFaces;
        for (const auto &feature : project->features())
            if (feature.kind == FeatureKind::Slot || feature.kind == FeatureKind::Pocket)
                for (int face : feature.faceIndices) recognizedFaces.insert(face);
        int pickedFace = -1;
        for (auto view : {ViewportWidget::StandardView::Top, ViewportWidget::StandardView::Bottom,
                          ViewportWidget::StandardView::Isometric}) {
            viewport->setStandardView(view);
            viewport->fitAll();
            settle();
            for (const Triangle &triangle : project->mesh().triangles) {
                if (recognizedFaces.contains(triangle.faceIndex) || triangle.normal.z() < 0.999f ||
                    !faces.contains(triangle.faceIndex)) continue;
                const QVector3D center = (triangle.v0 + triangle.v1 + triangle.v2) / 3.0f;
                const QPoint screen = projectPoint(*viewport, center);
                QVector3D hit;
                if (viewport->pickFaceAt(screen, &hit) != triangle.faceIndex) continue;
                clickViewport(*viewport, screen);
                pickedFace = triangle.faceIndex;
                break;
            }
            if (pickedFace >= 0) break;
        }
        if (pickedFace < 0) { std::cerr << "FAIL no visible unrecognized reachable face\n"; return 24; }
        settle();
        const QString expected = QStringLiteral("topology:face:%1").arg(pickedFace);
        if (dialog->selectedCandidateGeometryId() != expected) return 25;
        auto *loops = dialog->findChild<QListWidget *>(QStringLiteral("selectionEdges"));
        if (!loops || loops->count() == 0) return 26;
        const int before = project->operations().size();
        dialog->accept();
        settle();
        bool reason = false;
        for (QLabel *label : dialog->findChildren<QLabel *>())
            reason |= label->text().contains(QStringLiteral("选区已保留"));
        if (!dialog->isVisible() || !reason || project->operations().size() != before ||
            dialog->selectedCandidateGeometryId() != expected || loops->count() == 0) return 27;
        dialog->grab().save(output + QStringLiteral("/unmapped-face.png"));
        window.grab().save(output + QStringLiteral("/entry-viewport.png"));
        std::cout << "PASS real mouse picked unrecognized face=" << pickedFace
                  << " loops=" << loops->count() << " retained with explicit machining block" << std::endl;
        mode->setCurrentIndex(static_cast<int>(ChainSelectionMode::Edge));
        QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, static_cast<int>(ChainSelectionMode::Edge)));
        const MeshPickEdge *curve = nullptr;
        const MeshBoundaryLoop *curveLoop = nullptr;
        int curveIndex = -1;
        QPoint curveScreen;
        for (auto view : {ViewportWidget::StandardView::Top, ViewportWidget::StandardView::Bottom,
                          ViewportWidget::StandardView::Isometric}) {
            viewport->setStandardView(view);
            viewport->fitAll();
            settle();
            for (const MeshBoundaryLoop &loop : project->mesh().boundaryLoops) {
                if (loop.faceIndex != pickedFace) continue;
                bool continuous = true;
                for (int i = 0; i < loop.orderedEdgeIds.size(); ++i) {
                    const int next = (i + 1) % loop.orderedEdgeIds.size();
                    const auto *a = edgeForId(project->mesh(), loop.orderedEdgeIds.at(i));
                    const auto *b = edgeForId(project->mesh(), loop.orderedEdgeIds.at(next));
                    if (!a || !b || a->points.isEmpty() || b->points.isEmpty()) {continuous=false;break;}
                    const bool af = loop.orderedForward.value(i, true), bf = loop.orderedForward.value(next, true);
                    if ((af ? a->lastVertexId : a->firstVertexId) != (bf ? b->firstVertexId : b->lastVertexId) ||
                        ((af ? a->points.last() : a->points.first()) -
                         (bf ? b->points.first() : b->points.last())).length() > 1.0e-4f) {
                        continuous=false;break;
                    }
                }
                if (!continuous) continue;
                for (int loopEdgeIndex = 0; loopEdgeIndex < loop.orderedEdgeIds.size(); ++loopEdgeIndex) {
                const QString edgeId = loop.orderedEdgeIds.at(loopEdgeIndex);
                const auto *edge = edgeForId(project->mesh(), edgeId);
                if (!edge || edge->points.size() < 8) continue;
                QPoint screen;
                bool visible = false;
                for (const QVector3D &sample : edge->points) {
                    screen = projectPoint(*viewport, sample);
                    QString id;
                    QVector3D hit;
                    double parameter;
                    if (viewport->pickEdgeAt(screen, &id, &hit, &parameter) && id == edge->id) {
                        visible = true;
                        break;
                    }
                }
                if (!visible) continue;
                clickViewport(*viewport, screen);
                curve = edge;
                curveLoop = &loop;
                curveIndex = loopEdgeIndex;
                curveScreen = screen;
                break;
                }
                if (curve) break;
            }
            if (curve) break;
        }
        settle();
        if (!curve || preview.size() < 8) {
            std::cerr << "FAIL curve=" << bool(curve) << " samples=" << preview.size()
                      << " closed=" << previewClosed << std::endl;
            return 28;
        }
        auto *selectedEdges = dialog->findChild<QListWidget *>(
            QStringLiteral("selectionEdges"));
        if (!selectedEdges) return 28;
        for (const QVector3D &point : preview) {
            bool fromCurve = false;
            for (const QVector3D &sample : curve->points)
                fromCurve |= (sample - point).length() < 1.0e-4f;
            if (!fromCurve) return 29;
        }
        dialog->grab().save(output + QStringLiteral("/real-edge.png"));
        window.grab().save(output + QStringLiteral("/real-edge-viewport.png"));
        QString pickedCurveId;
        QVector3D pickedCurvePoint;
        double pickedCurveParameter = 0.0;
        if (!curveLoop || curveIndex < 0 ||
            !viewport->pickEdgeAt(curveScreen, &pickedCurveId, &pickedCurvePoint,
                                  &pickedCurveParameter) || pickedCurveId != curve->id) return 34;
        const double firstParameter = curve->parameters.value(0, 0.0);
        const double lastParameter = curve->parameters.value(curve->parameters.size() - 1, 1.0);
        const bool nearFirst = std::abs(pickedCurveParameter - firstParameter) <=
                               std::abs(pickedCurveParameter - lastParameter);
        const bool loopForward = curveLoop->orderedForward.value(curveIndex, true);
        const int step = loopForward == nearFirst ? 1 : -1;
        const MeshPickEdge *terminal = nullptr;
        QPoint terminalScreen;
        int intervalEdgeCount = 0;
        for (int offset = 2; offset < curveLoop->orderedEdgeIds.size(); ++offset) {
            const int terminalIndex =
                (curveIndex + offset * step + curveLoop->orderedEdgeIds.size()) %
                curveLoop->orderedEdgeIds.size();
            const MeshPickEdge *candidateTerminal = edgeForId(
                project->mesh(), curveLoop->orderedEdgeIds.at(terminalIndex));
            if (!candidateTerminal) continue;
            for (const QVector3D &sample : candidateTerminal->points) {
                const QPoint screen = projectPoint(*viewport, sample);
                QString visibleId;
                QVector3D visiblePoint;
                double visibleParameter = 0.0;
                if (viewport->pickEdgeAt(screen, &visibleId, &visiblePoint, &visibleParameter) &&
                    visibleId == candidateTerminal->id) {
                    terminal = candidateTerminal;
                    terminalScreen = screen;
                    intervalEdgeCount = offset + 1;
                    break;
                }
            }
            if (terminal) break;
        }
        if (!terminal) return 35;
        hoverViewport(*viewport, terminalScreen);
        settle();
        if (selectedEdges->count() != 1 || preview.size() <= curve->points.size() ||
            !viewport->m_contourChoiceCandidate) {
            std::cerr << "FAIL real hover interval changed formal refs or did not expand preview\n";
            return 36;
        }
        dialog->grab().save(output + QStringLiteral("/interval-preview.png"));
        window.grab().save(output + QStringLiteral("/interval-preview-viewport.png"));
        clickViewport(*viewport, terminalScreen);
        settle();
        if (selectedEdges->count() != intervalEdgeCount || viewport->m_contourChoiceCandidate) {
            std::cerr << "FAIL real terminal click did not commit the previewed interval\n";
            return 37;
        }
        dialog->grab().save(output + QStringLiteral("/interval-confirmed.png"));
        if (QApplication::focusWidget() != viewport) {
            std::cerr << "FAIL real model click did not leave keyboard focus on viewport\n";
            return 40;
        }
        QKeyEvent focusUndo(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
        QApplication::sendEvent(QApplication::focusWidget(), &focusUndo);
        settle();
        if (selectedEdges->count() != 1) {
            std::cerr << "FAIL viewport-focus Ctrl+Z did not undo the real connected interval\n";
            return 38;
        }
        QKeyEvent focusRedo(QEvent::KeyPress, Qt::Key_Y, Qt::ControlModifier);
        QApplication::sendEvent(QApplication::focusWidget(), &focusRedo);
        settle();
        if (selectedEdges->count() != intervalEdgeCount) {
            std::cerr << "FAIL viewport-focus Ctrl+Y did not restore the real connected interval\n";
            return 41;
        }
        QKeyEvent focusUndoAgain(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
        QApplication::sendEvent(QApplication::focusWidget(), &focusUndoAgain);
        settle();
        hoverViewport(*viewport, terminalScreen);
        settle();
        if (!viewport->m_contourChoiceCandidate) return 42;
        QKeyEvent focusEscape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(QApplication::focusWidget(), &focusEscape);
        settle();
        if (viewport->m_contourChoiceCandidate || selectedEdges->count() != 1 ||
            !dialog->isVisible()) {
            std::cerr << "FAIL viewport-focus Esc did not cancel only the interval preview\n";
            return 43;
        }
        QLineEdit textUndoProbe(dialog);
        textUndoProbe.show();
        textUndoProbe.setFocus(Qt::OtherFocusReason);
        QKeyEvent textInput(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier,
                            QStringLiteral("x"));
        QApplication::sendEvent(&textUndoProbe, &textInput);
        QKeyEvent textUndo(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
        QApplication::sendEvent(&textUndoProbe, &textUndo);
        settle();
        if (!textUndoProbe.text().isEmpty() || selectedEdges->count() != 1) {
            std::cerr << "FAIL text input Ctrl+Z was stolen by the selection session\n";
            return 48;
        }
        textUndoProbe.hide();
        std::cout << "PASS real hover interval start=" << curve->id.toStdString()
                  << " terminal=" << terminal->id.toStdString()
                  << " committed=" << intervalEdgeCount << " undo=1" << std::endl;
        mode->setCurrentIndex(static_cast<int>(ChainSelectionMode::Chain));
        QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, static_cast<int>(ChainSelectionMode::Chain)));
        clickViewport(*viewport, curveScreen);
        settle();
        auto *useWhole = dialog->findChild<QPushButton *>(
            QStringLiteral("selectionUseFullBoundary"));
        auto *adjustBoundary = dialog->findChild<QPushButton *>(
            QStringLiteral("selectionAdjustBoundary"));
        if (!previewClosed || preview.size() < 8 || !viewport->m_contourChoiceCandidate ||
            !useWhole || useWhole->isHidden() ||
            !adjustBoundary || adjustBoundary->isHidden() || selectedEdges->count() != 0) {
            std::cerr << "FAIL chain samples=" << preview.size() << " closed=" << previewClosed << std::endl;
            for (QLabel *label : dialog->findChildren<QLabel *>())
                std::cerr << label->text().toStdString() << std::endl;
            return 30;
        }
        dialog->grab().save(output + QStringLiteral("/full-boundary-offer.png"));
        window.grab().save(output + QStringLiteral("/full-boundary-offer-viewport.png"));
        useWhole->click();
        settle();
        if (selectedEdges->count() != curveLoop->orderedEdgeIds.size() ||
            viewport->m_contourChoiceCandidate) return 39;
        auto *moreDetails = dialog->findChild<QPushButton *>(
            QStringLiteral("selectionMoreDetails"));
        auto *finishBoundary = dialog->findChild<QPushButton *>(
            QStringLiteral("selectionFinishChain"));
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        auto *nextButton = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        auto *chainStatus = dialog->findChild<QLabel *>(
            QStringLiteral("selectionChainStatus"));
        if (!choices->isHidden() || !moreDetails || moreDetails->isHidden() ||
            !moreDetails->text().contains(QStringLiteral("更换范围")) ||
            !adjustBoundary->isVisible() || !finishBoundary || !finishBoundary->isHidden() ||
            !nextButton || nextButton->isHidden() ||
            !nextButton->text().contains(QStringLiteral("加工参数")) ||
            !chainStatus || !chainStatus->text().contains(QStringLiteral("下一步：加工参数"))) {
            std::cerr << "FAIL confirmed range did not collapse candidates into one-stage UI\n";
            return 44;
        }
        const int retainedCandidateCount = choices->count();
        moreDetails->click();
        settle();
        if (choices->isHidden() || choices->count() != retainedCandidateCount) {
            std::cerr << "FAIL expanding range choices discarded topology candidates\n";
            return 45;
        }
        moreDetails->click();
        adjustBoundary->click();
        settle();
        if (finishBoundary->isHidden() || !nextButton->isHidden()) {
            std::cerr << "FAIL boundary adjustment did not expose exactly its stage action\n";
            return 46;
        }
        window.grab().save(output + QStringLiteral("/boundary-adjustment-viewport.png"));
        if (!captureVisibleDesktop(window, *dialog,
                output + QStringLiteral("/boundary-adjustment-desktop.png"))) return 49;
        finishBoundary->click();
        settle();
        if (!finishBoundary->isHidden() || nextButton->isHidden() ||
            !nextButton->text().contains(QStringLiteral("加工参数")) ||
            !chainStatus->text().contains(QStringLiteral("下一步：加工参数")) ||
            !choices->isHidden() || choices->count() != retainedCandidateCount) {
            std::cerr << "FAIL finishing adjustment did not restore the confirmed range stage\n";
            return 47;
        }
        for (const QSize &size : {QSize(1500, 900), QSize(1280, 760)}) {
            window.showNormal();
            window.resize(size);
            settle();
            viewport->fitAll();
            settle();
            if (globalWidgetRect(*viewport).intersects(globalWidgetRect(*dialog))) {
                std::cerr << "FAIL embedded selection panel overlaps viewport at "
                          << size.width() << "x" << size.height() << "\n";
                return 53;
            }
            bool reachable = false;
            for (const QVector3D &sample : curve->points) {
                const QPoint screen = projectPoint(*viewport, sample);
                QString hitId;
                QVector3D hitPoint;
                double hitParameter = 0.0;
                if (viewport->rect().contains(screen) &&
                    viewport->pickEdgeAt(screen, &hitId, &hitPoint, &hitParameter) &&
                    hitId == curve->id) {
                    reachable = true;
                    break;
                }
            }
            if (!reachable) {
                std::cerr << "FAIL selected boundary is not reachable at "
                          << size.width() << "x" << size.height() << "\n";
                return 54;
            }
            window.grab().save(output + QStringLiteral("/confirmed-%1x%2.png")
                               .arg(size.width()).arg(size.height()));
        }
        dialog->grab().save(output + QStringLiteral("/full-boundary-used.png"));
        window.grab().save(output + QStringLiteral("/full-boundary-used-viewport.png"));
        if (!captureVisibleDesktop(window, *dialog,
                output + QStringLiteral("/full-boundary-used-desktop.png"))) return 50;
        adjustBoundary->click();
        settle();
        const MeshPickEdge *otherSlotEdge = nullptr;
        QPoint otherSlotScreen;
        QStringList otherSlotIds = otherSlotEdgeIds.values();
        std::sort(otherSlotIds.begin(), otherSlotIds.end());
        const auto slotVertexDegree = [&](const QString &vertexId) {
            int degree = 0;
            for (const QString &candidateId : otherSlotIds) {
                const MeshPickEdge *candidateEdge = edgeForId(project->mesh(), candidateId);
                if (candidateEdge && (candidateEdge->firstVertexId == vertexId ||
                                      candidateEdge->lastVertexId == vertexId)) ++degree;
            }
            return degree;
        };
        for (ViewportWidget::StandardView view : {
                 ViewportWidget::StandardView::Top,
                 ViewportWidget::StandardView::Bottom,
                 ViewportWidget::StandardView::Isometric,
                 ViewportWidget::StandardView::Front,
                 ViewportWidget::StandardView::Back,
                 ViewportWidget::StandardView::Left,
                 ViewportWidget::StandardView::Right}) {
            viewport->setStandardView(view);
            viewport->fitAll();
            settle();
            for (const QString &edgeId : otherSlotIds) {
                const MeshPickEdge *edge = edgeForId(project->mesh(), edgeId);
                if (!edge || edge->adjacentFaceIndices.contains(pickedFace)) continue;
                if (slotVertexDegree(edge->firstVertexId) != 2 ||
                    slotVertexDegree(edge->lastVertexId) != 2) continue;
                for (const QVector3D &sample : edge->points) {
                    const QPoint screen = projectPoint(*viewport, sample);
                    QString hitId;
                    QVector3D hitPoint;
                    double hitParameter = 0.0;
                    if (viewport->rect().contains(screen) &&
                        viewport->pickEdgeAt(screen, &hitId, &hitPoint, &hitParameter) &&
                        hitId == edgeId) {
                        otherSlotEdge = edge;
                        otherSlotScreen = screen;
                        break;
                    }
                }
                if (otherSlotEdge) break;
            }
            if (otherSlotEdge) break;
        }
        if (!otherSlotEdge) {
            std::cerr << "FAIL no reachable opposite-side slot boundary with panel visible\n";
            return 55;
        }
        clickViewport(*viewport, otherSlotScreen);
        settle();
        auto *confirmContext = dialog->findChild<QPushButton *>(
            QStringLiteral("selectionConfirmTopologyContext"));
        if (confirmContext && confirmContext->isVisible()) {
            confirmContext->click();
            settle();
        }
        if (selectedEdges->count() != 1) {
            std::cerr << "FAIL opposite-side slot click did not start an edge chain\n";
            return 56;
        }
        auto *connectEdges = dialog->findChild<QPushButton *>(
            QStringLiteral("selectionConnectEdges"));
        if ((!connectEdges || !connectEdges->isVisible()) && adjustBoundary->isVisible()) {
            adjustBoundary->click();
            settle();
        }
        if (!connectEdges || !connectEdges->isVisible()) {
            std::cerr << "FAIL opposite-side slot did not expose boundary adjustment\n";
            return 56;
        }
        const MeshPickEdge *otherTerminal = nullptr;
        QPoint otherTerminalScreen;
        for (ViewportWidget::StandardView view : {
                 ViewportWidget::StandardView::Top,
                 ViewportWidget::StandardView::Bottom,
                 ViewportWidget::StandardView::Isometric,
                 ViewportWidget::StandardView::Front,
                 ViewportWidget::StandardView::Back,
                 ViewportWidget::StandardView::Left,
                 ViewportWidget::StandardView::Right}) {
            viewport->setStandardView(view);
            viewport->fitAll();
            settle();
            for (const MeshBoundaryLoop &loop : project->mesh().boundaryLoops) {
                if (!loop.orderedEdgeIds.contains(otherSlotEdge->id)) continue;
                for (const QString &terminalId : loop.orderedEdgeIds) {
                    if (terminalId == otherSlotEdge->id) continue;
                    const MeshPickEdge *terminal = edgeForId(project->mesh(), terminalId);
                    if (!terminal) continue;
                    for (const QVector3D &sample : terminal->points) {
                        const QPoint screen = projectPoint(*viewport, sample);
                        QString hitId;
                        QVector3D hitPoint;
                        double hitParameter = 0.0;
                        if (!viewport->rect().contains(screen) ||
                            !viewport->pickEdgeAt(screen, &hitId, &hitPoint, &hitParameter) ||
                            hitId != terminal->id) continue;
                        hoverViewport(*viewport, screen);
                        settle();
                        if (viewport->m_contourChoiceCandidate && selectedEdges->count() == 1) {
                            otherTerminal = terminal;
                            otherTerminalScreen = screen;
                        }
                        break;
                    }
                    if (otherTerminal) break;
                }
                if (otherTerminal) break;
            }
            if (otherTerminal) break;
        }
        if (!otherTerminal) {
            std::cerr << "FAIL no connected preview on opposite-side slot; connectVisible="
                      << bool(connectEdges && connectEdges->isVisible())
                      << " adjustVisible=" << adjustBoundary->isVisible()
                      << " refs=" << selectedEdges->count() << "\n";
            for (QLabel *label : dialog->findChildren<QLabel *>())
                if (!label->text().isEmpty())
                    std::cerr << label->text().toStdString() << "\n";
            window.grab().save(output + QStringLiteral("/opposite-slot-debug.png"));
            return 56;
        }
        if (!viewport->m_contourChoiceCandidate || selectedEdges->count() != 1) {
            std::cerr << "FAIL opposite-side slot hover did not preview a connected interval\n";
            return 56;
        }
        window.grab().save(output + QStringLiteral("/opposite-slot-preview.png"));
        clickViewport(*viewport, otherTerminalScreen);
        settle();
        if (viewport->m_contourChoiceCandidate || selectedEdges->count() <= 1) {
            std::cerr << "FAIL opposite-side slot interval was not confirmed\n";
            return 57;
        }
        window.grab().save(output + QStringLiteral("/opposite-slot-confirmed.png"));
        viewport->setFocus(Qt::OtherFocusReason);
        QKeyEvent oppositeUndo(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
        QApplication::sendEvent(viewport, &oppositeUndo);
        settle();
        if (selectedEdges->count() != 1 || !dialog->isVisible()) {
            std::cerr << "FAIL opposite-side slot confirmation did not undo in place\n";
            return 58;
        }
        window.grab().save(output + QStringLiteral("/opposite-slot-undone.png"));
        std::cout << "PASS opposite-side slot preview/confirm/undo start="
                  << otherSlotEdge->id.toStdString() << " terminal="
                  << otherTerminal->id.toStdString() << std::endl;
        const int hiddenSessionEdgeCount = selectedEdges->count();
        dialog->hide();
        viewport->setFocus(Qt::OtherFocusReason);
        QKeyEvent hiddenUndo(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
        QApplication::sendEvent(viewport, &hiddenUndo);
        settle();
        if (selectedEdges->count() != hiddenSessionEdgeCount) {
            std::cerr << "FAIL hidden selection session intercepted viewport Ctrl+Z\n";
            return 51;
        }
        std::cout << "PASS viewport-focus shortcuts, text-input undo, and hidden-session gate"
                  << std::endl;
        std::cout << "PASS real entry loop=" << curve->id.toStdString()
                  << " full boundary adopted once with no rectangle substitution" << std::endl;
        dialog->topologyPreviewChanged = originalPreview;
        return 0;
    }

    if (!viewport) return 8;
    QVector<const MeshPickEdge *> clickedEdges;
    QVector<bool> clickedForwards;
    QVector<QPoint> clickScreens;
    for (ViewportWidget::StandardView view : {ViewportWidget::StandardView::Top,
                                               ViewportWidget::StandardView::Isometric,
                                               ViewportWidget::StandardView::Front,
                                               ViewportWidget::StandardView::Back,
                                               ViewportWidget::StandardView::Left,
                                               ViewportWidget::StandardView::Right,
                                               ViewportWidget::StandardView::Bottom}) {
        viewport->setStandardView(view);
        viewport->fitAll();
        settle();
        const int count = outer->orderedEdgeIds.size();
        for (int pass = 0; pass < 2 && clickedEdges.isEmpty(); ++pass) {
            for (int step : {1, -1}) {
                for (int index = 0; index < count; ++index) {
                    QVector<const MeshPickEdge *> trialEdges;
                    QVector<bool> trialForwards;
                    QVector<QPoint> trialScreens;
                    for (int offset = 0; offset < count; ++offset) {
                        const int edgeIndex = (index + offset * step + count) % count;
                        const MeshPickEdge *edge = edgeForId(
                            project->mesh(), outer->orderedEdgeIds.at(edgeIndex));
                        if (!edge || (pass == 0 && otherEdgeIds.contains(edge->id))) {
                            trialEdges.clear();
                            trialForwards.clear();
                            break;
                        }
                        const bool forward = step > 0
                            ? outer->orderedForward.value(edgeIndex, true)
                            : !outer->orderedForward.value(edgeIndex, true);
                        QPoint screen;
                        if (!visibleEdgePoint(*viewport, *edge, forward, &screen)) {
                            trialEdges.clear();
                            trialForwards.clear();
                            break;
                        }
                        trialEdges.append(edge);
                        trialForwards.append(forward);
                        trialScreens.append(screen);
                    }
                    if (trialEdges.size() == count) {
                        clickedEdges = trialEdges;
                        clickedForwards = trialForwards;
                        clickScreens = trialScreens;
                        break;
                    }
                }
                if (!clickedEdges.isEmpty()) break;
            }
        }
        if (!clickedEdges.isEmpty()) break;
    }
    if (clickedEdges.size() != outer->orderedEdgeIds.size()) {
        std::cerr << "BLOCKED no view exposes the complete slot boundary for mouse picking\n";
        return 5;
    }
    auto *mode = dialog->findChild<QComboBox *>(QStringLiteral("selectionMode"));
    mode->setCurrentIndex(static_cast<int>(ChainSelectionMode::Edge));
    QMetaObject::invokeMethod(mode, "activated",
                              Q_ARG(int, static_cast<int>(ChainSelectionMode::Edge)));
    QString mousePickId;
    int mousePickKind = -1;
    QObject::connect(viewport, &ViewportWidget::topologyPicked,
                     [&](const QString &id, int kind, const QVector3D &, double) {
        mousePickId = id;
        mousePickKind = kind;
    });
    clickViewport(*viewport, clickScreens.first());
    settle();
    auto *contexts = dialog->findChild<QComboBox *>(QStringLiteral("selectionTopologyContext"));
    auto *confirmContext = dialog->findChild<QPushButton *>(
        QStringLiteral("selectionConfirmTopologyContext"));
    if (contexts && confirmContext && contexts->isVisible()) {
        int wanted = -1;
        for (int index = 0; index < contexts->count(); ++index)
            if (contexts->itemText(index).contains(outer->faceId)) { wanted = index; break; }
        if (wanted < 0) {
            std::cerr << "shared edge ambiguity did not list the expected OCC face\n";
            return 11;
        }
        contexts->setCurrentIndex(wanted);
        QMetaObject::invokeMethod(contexts, "activated", Q_ARG(int, wanted));
        confirmContext->click();
        settle();
    }
    for (int index = 1; index < clickScreens.size(); ++index) {
        clickViewport(*viewport, clickScreens.at(index));
        settle();
    }
    auto *selectedEdges = dialog->findChild<QListWidget *>(QStringLiteral("selectionEdges"));
    if (!selectedEdges || selectedEdges->count() != clickedEdges.size()) {
        std::cerr << "real boundary mouse clicks did not retain every topology edge\n";
        return 12;
    }
    dialog->grab().save(output + QStringLiteral("/p1-selection.png"));
    window.grab().save(output + QStringLiteral("/p1-selection-viewport.png"));
    dialog->accept();
    settle();
    const SelectionChain selection = dialog->selection();
    if (selection.evidenceVersion != 2 || !selection.explicitUserSelection ||
        selection.orderedTopologyRefs.isEmpty() ||
        selection.orderedTopologyRefs.size() != clickedEdges.size() ||
        clickedForwards.size() != clickedEdges.size() ||
        !selection.hasStartAnchor || selection.hasEndAnchor || !selection.closed) {
        std::cerr << "real viewport boundary clicks did not create continuous closed v2 evidence"
                  << " expected-first=" << clickedEdges.first()->id.toStdString()
                  << " expected-last=" << clickedEdges.last()->id.toStdString()
                  << " emitted=" << mousePickId.toStdString()
                  << " kind=" << mousePickKind
                  << " candidate=" << dialog->selectedCandidateGeometryId().toStdString()
                  << " version=" << selection.evidenceVersion
                  << " explicit=" << selection.explicitUserSelection
                  << " refs=" << selection.orderedTopologyRefs.size()
                  << " screen=" << clickScreens.first().x() << ','
                  << clickScreens.first().y() << '\n';
        for (QLabel *label : dialog->findChildren<QLabel *>())
            if (!label->text().isEmpty()) std::cerr << "LABEL " << label->text().toStdString() << '\n';
        return 9;
    }
    QString previousEndId;
    QVector3D previousEndPoint;
    QString firstStartId;
    QVector3D firstStartPoint;
    for (int index = 0; index < clickedEdges.size(); ++index) {
        const MeshPickEdge &edge = *clickedEdges.at(index);
        const SelectionTopologyRef &ref = selection.orderedTopologyRefs.at(index);
        if (ref.id != edge.id || ref.forward != clickedForwards.at(index)) {
            std::cerr << "directed topology ref differs from the real mouse traversal at index "
                      << index << '\n';
            return 13;
        }
        const QString startId = ref.forward ? edge.firstVertexId : edge.lastVertexId;
        const QString endId = ref.forward ? edge.lastVertexId : edge.firstVertexId;
        const QVector3D startPoint = ref.forward ? edge.points.first() : edge.points.last();
        const QVector3D endPoint = ref.forward ? edge.points.last() : edge.points.first();
        if (index == 0) {
            firstStartId = startId;
            firstStartPoint = startPoint;
            if (selection.startAnchor.topologyId != startId ||
                (selection.startAnchor.point - startPoint).length() > 1.0e-4f) {
                std::cerr << "start anchor differs from the first directed OCC endpoint\n";
                return 14;
            }
        } else if (previousEndId != startId ||
                   (previousEndPoint - startPoint).length() > 1.0e-4f) {
            std::cerr << "directed OCC endpoints are discontinuous at index " << index << '\n';
            return 15;
        }
        previousEndId = endId;
        previousEndPoint = endPoint;
    }
    if (previousEndId != firstStartId ||
        (previousEndPoint - firstStartPoint).length() > 1.0e-4f) {
        std::cerr << "directed OCC endpoints do not close independently\n";
        return 16;
    }
    QDialog *parameters = nullptr;
    for (QDialog *candidate : window.findChildren<QDialog *>()) {
        if (candidate->isVisible() && candidate != dialog &&
            candidate->windowTitle().contains(QStringLiteral("工艺参数"))) {
            parameters = candidate;
            break;
        }
    }
    if (!parameters) {
        std::cerr << "selection did not continue to the existing parameter confirmation\n";
        return 10;
    }
    StrategyPanel *panel = window.findChild<StrategyPanel *>();
    auto *depth = panel ? panel->findChild<QDoubleSpinBox *>(
        QStringLiteral("operationParameter_depth")) : nullptr;
    QComboBox *toolCombo = nullptr;
    if (panel) {
        for (QComboBox *combo : panel->findChildren<QComboBox *>()) {
            if (combo->findData(selectedTool.id) >= 0) { toolCombo = combo; break; }
        }
    }
    if (!panel || !depth || !toolCombo) {
        std::cerr << "parameter panel has no selectable P1 lifecycle tool/depth panel="
                  << bool(panel) << " depth=" << bool(depth)
                  << " tool=" << bool(toolCombo) << "\n";
        if (panel) {
            for (QDoubleSpinBox *spin : panel->findChildren<QDoubleSpinBox *>())
                std::cerr << "SPIN " << spin->objectName().toStdString() << "\n";
            for (QComboBox *combo : panel->findChildren<QComboBox *>()) {
                std::cerr << "COMBO " << combo->objectName().toStdString() << " ids=";
                for (int index = 0; index < combo->count(); ++index)
                    std::cerr << combo->itemData(index).toInt() << ',';
                std::cerr << "\n";
            }
        }
        return 17;
    }
    toolCombo->setCurrentIndex(toolCombo->findData(selectedTool.id));
    depth->setValue(0.75);
    const int operationsBeforeReselect = project->operations().size();
    panel->findChild<QPushButton *>(QStringLiteral("reselectGeometryButton"))->click();
    settle();
    if (!dialog->isVisible() || parameters->isVisible()) return 18;
    dialog->reject();
    settle();
    depth = panel->findChild<QDoubleSpinBox *>(QStringLiteral("operationParameter_depth"));
    if (!parameters->isVisible() || !depth || std::abs(depth->value() - 0.75) > 0.001 ||
        panel->currentTool().id != selectedTool.id ||
        project->operations().size() != operationsBeforeReselect) {
        std::cerr << "canceling reselection must restore draft, hand-selected tool and operation count\n";
        return 19;
    }
    panel->findChild<QPushButton *>(QStringLiteral("reselectGeometryButton"))->click();
    settle();
    if (!dialog->isVisible() || parameters->isVisible()) return 20;
    dialog->accept();
    settle();
    depth = panel->findChild<QDoubleSpinBox *>(QStringLiteral("operationParameter_depth"));
    if (!parameters->isVisible() || !depth || std::abs(depth->value() - 0.75) > 0.001 ||
        panel->currentTool().id != selectedTool.id ||
        project->operations().size() != operationsBeforeReselect) {
        std::cerr << "completed reselection must preserve draft/tool and create no operation\n";
        return 21;
    }
    QTimer confirmationResponder;
    QStringList confirmationDialogs;
    QObject::connect(&confirmationResponder, &QTimer::timeout, [&confirmationDialogs]() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *message = qobject_cast<QMessageBox *>(widget);
            if (!message || !message->isVisible()) continue;
            confirmationDialogs.append(message->windowTitle() + QStringLiteral(": ") + message->text());
            if (QAbstractButton *yes = message->button(QMessageBox::Yes)) yes->click();
            else message->accept();
        }
    });
    confirmationResponder.start(50);
    QMetaObject::invokeMethod(panel, "onConfirmOperation", Qt::DirectConnection);
    confirmationResponder.stop();
    settle();
    if (project->operations().size() != operationsBeforeReselect + 1) {
        std::cerr << "one explicit confirmation after reselection must create exactly one operation\n";
        for (const QString &message : confirmationDialogs)
            std::cerr << "DIALOG " << message.toStdString() << '\n';
        return 22;
    }
    window.grab().save(output + QStringLiteral("/p1-parameters-viewport.png"));
    parameters->grab().save(output + QStringLiteral("/p1-parameters.png"));
    std::cout << "PASS real STEP mouse edge sequence -> closed continuous topology chain -> parameter confirmation "
              << selection.id.toStdString() << '\n';
    return 0;
}
