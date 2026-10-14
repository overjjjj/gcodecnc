#include "../src/app/MainWindow.h"
#include "../src/app/AppController.h"
#include "../src/workspaces/process_design/ui/HoleSelectionDialog.h"
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include <QApplication>
#include <QTimer>
#include <QEventLoop>
#include <QPushButton>
#include <QToolButton>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QDir>
#include <QScreen>
#include <QMessageBox>
#include <QLabel>
#include <iostream>
static void settle() { QEventLoop loop; QTimer::singleShot(700, &loop, &QEventLoop::quit); loop.exec(); }
int main(int argc, char **argv) {
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc, argv);
    app.setOrganizationName("CNEXT-UI-Acceptance");
    app.setApplicationName("UI-suite-test");
    HoleSelectionDialog holes;
    holes.setRecords({{"a", 6, {}, 0}, {"b", 8, {}, 1}});
    holes.toggleGeometry("a");
    if (holes.records().size() != 1 || holes.records().first().geometryId != "b") return 10;
    holes.toggleGeometry("b");
    if (holes.confirm()) return 11;
    holes.toggleGeometry("a");
    if (holes.records().size() != 1) return 12;
    if (argc < 3) return 0;
    QTimer dismissErrors;
    QObject::connect(&dismissErrors, &QTimer::timeout, []() {
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *message = qobject_cast<QMessageBox *>(widget)) {
                std::cerr << message->text().toStdString() << "\n";
                message->reject();
            }
    });
    dismissErrors.start(500);
    MainWindow window;
    window.resize(1600, 980);
    window.show();
    AppController::instance().importStep(QString::fromLocal8Bit(argv[1]));
    settle();
    auto *setupProject = AppController::instance().projectManager();
    SetupOrigin origin; origin.confirmed = true;
    StockDefinition stock; stock.confirmed = true;
    setupProject->setSetupOrigin(origin);
    setupProject->setStockDefinition(stock);
    settle();
    const QString out = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(out);
    const auto *project = AppController::instance().projectManager();
    if (project->mesh().isEmpty()) return 20;
    window.grab().save(out + "/main.png");
    ViewportWidget *viewport = nullptr;
    for (auto *candidate : window.findChildren<ViewportWidget *>()) if (candidate->isVisible()) viewport = candidate;
    if (!viewport) return 27;
    for (int index = 0; index < 7; ++index) {
        viewport->setStandardView(static_cast<ViewportWidget::StandardView>(index));
        settle();
        window.grab().save(out + QString("/view-%1.png").arg(index));
        viewport->grabFramebuffer().save(out + QString("/viewport-%1.png").arg(index));
    }
    for (auto *button : window.findChildren<QToolButton *>()) {
        if (button->text() != "G83") continue;
        button->click(); settle();
        HoleSelectionDialog *selection = nullptr;
        for (auto *dialog : window.findChildren<QDialog *>())
            if (auto *candidate = dynamic_cast<HoleSelectionDialog *>(dialog)) selection = candidate;
        if (!selection || !selection->isVisible() || selection->isModal()) return 21;
        selection->grab().save(out + "/hole-selection.png");
        selection->confirm(); settle();
        auto *panel = window.findChild<StrategyPanel *>();
        if (!panel->isVisible() || selection->isVisible()) return 22;
        auto *peck = panel->findChild<QDoubleSpinBox *>("operationParameter_peckDepth");
        if (!peck) return 23;
        peck->setValue(1.375);
        panel->window()->grab().save(out + "/g83-parameters.png");
        panel->findChild<QPushButton *>("reselectGeometryButton")->click(); settle();
        if (!selection->isVisible() || panel->isVisible()) return 24;
        selection->confirm(); settle();
        peck = panel->findChild<QDoubleSpinBox *>("operationParameter_peckDepth");
        if (!peck || std::abs(peck->value() - 1.375) > 0.001) return 25;
        panel->window()->hide();
        for (const QString &label : {QString::fromUtf8("铣平面"), QString::fromUtf8("挖槽")}) {
            for (auto *command : window.findChildren<QToolButton *>()) {
                if (command->text() != label) continue;
                command->click(); settle();
                SelectionChainDialog *chain = nullptr;
                for (auto *dialog : window.findChildren<QDialog *>())
                    if (auto *candidate = dynamic_cast<SelectionChainDialog *>(dialog))
                        if (candidate->isVisible()) chain = candidate;
                if (!chain || chain->isModal()) return 28;
                auto *list = chain->findChild<QListWidget *>("selectionCandidates");
                if (!list || !list->count()) return 29;
                list->setCurrentRow(0); settle();
                chain->grab().save(out + "/chain-" + label + ".png");
                chain->accept(); settle();
                if (!panel->isVisible()) { chain->grab().save(out + "/chain-error.png"); for(auto *text : chain->findChildren<QLabel *>()) std::cerr << text->text().toStdString() << "\n"; return 30; }
                panel->window()->grab().save(out + "/parameters-" + label + ".png");
                auto *depth = panel->findChild<QDoubleSpinBox *>("operationParameter_depth");
                if (depth) depth->setValue(0.625);
                panel->findChild<QPushButton *>("reselectGeometryButton")->click(); settle();
                if (!chain->isVisible() || panel->isVisible()) return 31;
                list->setCurrentRow(0);
                chain->accept(); settle();
                depth = panel->findChild<QDoubleSpinBox *>("operationParameter_depth");
                if (depth && std::abs(depth->value() - 0.625) > 0.001) return 32;
                panel->window()->hide();
                break;
            }
        }
        std::cout << "PASS hole subset, nonmodal selection, single window, G83/face/pocket draft preservation, model import and seven views\n";
        return 0;
    }
    return 26;
}
