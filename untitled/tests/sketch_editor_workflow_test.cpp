#include "OutputWorkflowTestSupport.h"
#include "../src/app/MainWindow.h"
#include <QToolButton>
#include "../src/app/AppController.h"
#include "../src/workspaces/process_design/ui/SketchEditorDialog.h"
#include "../src/workspaces/process_design/ui/SetupOriginDialog.h"
#include "../src/workspaces/process_design/ui/StockDefinitionDialog.h"
#include <QGraphicsView>
#include <QTableWidget>
#include <QLineEdit>
#include <QMouseEvent>
#include <functional>
#include <QComboBox>
#include <QJsonDocument>

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
    QTimer::singleShot(90000, &app, [] { std::cerr << "FAIL timeout" << std::endl; std::_Exit(99); });
    auto *project = AppController::instance().projectManager();
    QComboBox *wcs = nullptr;
    for (auto *combo : window.findChildren<QComboBox *>())
        if (combo->count() == 6 && combo->itemData(0).toString() == "G54") wcs = combo;
    check(wcs != nullptr, "actual WCS toolbar exists");
    if (wcs) wcs->setCurrentIndex(wcs->findData("G55"));
    project->saveToFile(settings.filePath("previous.cnext"));
    auto edit = [&](const std::function<void(SketchEditorDialog *)> &action) {
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
    MachiningOperation operation; operation.id = "historic-operation"; operation.markToolpathValid();
    operation.selectionEvidence.sourceFingerprint = originalFingerprint;
    project->setOperations({operation});
    ProgramEntry program; program.id = "historic-program"; program.gcodeText = "G90\nG0 Z50\n";
    program.sourceOperationIds = QStringList{operation.id};
    project->setPrograms({program}); project->setCurrentProgramId(program.id);
    edit([&](SketchEditorDialog *d) {
        d->findChild<QTableWidget *>("sketchPointsTable")->item(2, 0)->setText("60");
        click(d, "saveSketchButton");
    });
    check(project->sketchDocument().id == original.id && project->sketchDocument().contourId == original.contourId &&
          project->sketchDocument().revision == original.revision + 1, "edit keeps identities and advances revision");
    check(project->sourceFileFingerprint() != originalFingerprint && project->operations().size() == 1 &&
          project->operations().first().toolpathState == ToolpathState::Stale, "edit invalidates historical operation");
    check(project->programs().size() == 1 && project->programs().first().gcodeText == program.gcodeText &&
          project->currentProgramId() == program.id, "edit preserves historical program content and current id");
    check(!project->setupOrigin().confirmed && !project->stockDefinition().confirmed &&
          project->stockDefinition().minusZ == 15 && project->stockDefinition().plusX == 3, "edit resets confirmation without rewriting stock allowances");
    const auto edited = project->sketchDocument();
    const QString editedFingerprint = project->sourceFileFingerprint();
    check(project->saveToFile(saved), "edited project saves");
    check(project->updateSketchDocument(edited, editedFingerprint) && !project->isModified(), "unchanged draft is a clean no-op");
    QString error;
    check(!project->updateSketchDocument(original, originalFingerprint, &error) &&
          project->sourceFileFingerprint() == editedFingerprint && !project->isModified(), "stale draft is rejected atomically");
    edit([&](SketchEditorDialog *d) {
        click(d, "deleteSketchContourButton"); click(d, "fitSketchButton");
        click(d, "sketchPolylineButton"); point(d, 0, 0); point(d, 25, 0); point(d, 15, 15);
        check(!d->findChild<QPushButton *>("saveSketchButton")->isEnabled(), "unfinished polyline cannot save");
        click(d, "finishSketchContourButton"); click(d, "saveSketchButton");
    });
    check(project->sketchDocument().contourKind == "polyline" && project->sketchDocument().points.size() == 3, "explicit closure saves real drawn polyline");
    edit([&](SketchEditorDialog *d) { click(d, "deleteSketchContourButton"); click(d, "saveSketchButton"); });
    check(project->sketchDocument().isEmpty() && project->mesh().pickEdges.isEmpty() && project->programs().size() == 1, "delete clears geometry while preserving history");
    check(project->saveToFile(saved), "empty edited document saves");
    QFile serialized(saved); serialized.open(QIODevice::ReadOnly);
    const auto savedRoot = QJsonDocument::fromJson(serialized.readAll()).object();
    auto invalidSetup = [&](const char *field, const QJsonValue &value) {
        auto root = savedRoot; auto setup = root["setup"].toObject(); setup[field] = value; root["setup"] = setup;
        const QString path = settings.filePath(QString("invalid-%1.cnext").arg(field));
        QFile file(path); file.open(QIODevice::WriteOnly); file.write(QJsonDocument(root).toJson()); file.close();
        const QString before = project->sourceFileFingerprint();
        check(!project->loadFromFile(path) && project->sourceFileFingerprint() == before, "unsupported sketch orientation rejects before mutation");
    };
    invalidSetup("rotationX", 0.5); invalidSetup("activeRegion", "back");
    ProjectManager reopened;
    check(reopened.loadFromFile(saved) && reopened.sketchDocument().isEmpty() && reopened.programs().size() == 1, "empty document and history reopen");
    std::cout << "FAILURES " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
