#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include "../src/core/ProjectManager.h"
#include <QApplication>
#include "../src/core/Settings.h"
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QTableWidget>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    app.setFont(QFont("Microsoft YaHei", 9));
    QTemporaryDir directory;
    OperationListPanel panel;
    int failures = 0;
    for (const QString &strategy : {QString("mill_slot"), QString("mill_blind_slot"), QString("mill_tapered_slot")}) {
        MachiningOperation op;
        op.id = strategy; op.strategyId = strategy; op.opType = OperationType::Contour;
        op.contourFeature.length = 40; op.contourFeature.width = 12; op.contourFeature.depth = 4;
        op.contourFeature.radius = 2; // Not an authority for final slot dimensions.
        if (strategy == "mill_slot") op.params.set("depth", 4);
        if (strategy == "mill_blind_slot") op.params.set("depth", 2);
        panel.addConfirmedOperations({op});
    }
    const QString dimensions = "L40.0 W12.0";
    auto verify = [&](const OperationListPanel &value) {
        auto *table = value.findChild<QTableWidget *>();
        for (int i = 0; i < value.operations().size(); ++i) {
            const auto &op = value.operations().at(i);
            const bool zh = Settings::instance().language() == "zh_CN";
            const QString expected = (zh ? QString::fromUtf8("L40.0 W12.0 · 槽深 4.0 · 加工深度 %1")
                : QString("L40.0 W12.0 · Slot depth 4.0 · Cutting depth %1"))
                .arg(op.params.get("depth", 4), 0, 'f', 1);
            if (!op.featureRef.contains(dimensions) || table->item(i, 3)->text() != expected ||
                op.contourFeature.length != 40 || op.contourFeature.width != 12) {
                std::cerr << "final slot dimensions must agree in featureRef/table/geometry: "
                          << op.featureRef.toStdString() << '\n';
                ++failures;
            }
        }
    };
    for (const auto &language : {QString("zh_CN"), QString("en_US")}) {
        Settings::instance().setLanguage(language); panel.retranslateUi(); verify(panel);
    }
    panel.resize(1100, 420);
    QDir().mkpath("../outputs/slot-b2u/ui-fixtures");
    panel.grab().save("../outputs/slot-b2u/ui-fixtures/" + qEnvironmentVariable("SLOT_UI_PHASE", "green") + "-depth-list.png");
    ProjectManager project;
    project.setOperations(panel.operations());
    const auto path = directory.filePath("slots.cnext");
    if (!project.saveToFile(path)) return 1;
    ProjectManager reopened;
    if (!reopened.loadFromFile(path) || reopened.operations().size() != 3) return 1;
    panel.setOperations(reopened.operations());
    verify(panel);
    // Old parameter remnants must not override the confirmed final geometry.
    auto stale = panel.operations().first(); stale.id = "stale-params"; stale.featureRef.clear();
    stale.params.set("slotLength", 99); stale.params.set("slotWidth", 88);
    panel.addConfirmedOperations({stale});
    verify(panel);
    return failures ? 1 : 0;
}
