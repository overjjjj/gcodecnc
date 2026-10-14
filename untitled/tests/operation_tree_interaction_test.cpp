#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include <QApplication>
#include <QTreeWidget>
#include <iostream>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    OperationListPanel panel;
    MachiningOperation first, second;
    first.id = QStringLiteral("first");
    second.id = QStringLiteral("second");
    first.stage = OperationStage::FinishCut;
    second.stage = OperationStage::RoughCut;
    panel.setOperations({first, second});
    auto *tree = panel.findChild<QTreeWidget *>();
    if (!tree) { std::cerr << "Expected a hierarchical operation tree\n"; return 1; }
    if (!panel.selectOperationById(first.id)) return 2;
    QMetaObject::invokeMethod(&panel, "onSortByStage", Qt::DirectConnection);
    if (panel.currentOperationId() != first.id || panel.currentOperationNumber() != 2) return 3;
    auto *operation = tree->currentItem();
    if (!operation || !operation->parent() || operation->childCount() != 4) return 4;
    operation->setCheckState(2, Qt::Unchecked);
    if (!panel.operations().at(1).enabled) return 5;
    panel.setOperations(panel.operations());
    if (tree->currentItem()->checkState(2) != Qt::Unchecked) return 6;
    tree->setCurrentItem(tree->currentItem()->child(0));
    if (panel.currentOperationId() != first.id) return 7;
    QMetaObject::invokeMethod(&panel, "onMoveUp", Qt::DirectConnection);
    if (panel.currentOperationId() != first.id || panel.currentOperationNumber() != 1) return 8;
    if (argc > 1) {
        panel.resize(390, 620);
        panel.show();
        tree->expandAll();
        app.processEvents();
        panel.grab().save(QString::fromLocal8Bit(argv[1]));
    }
    return 0;
}
