#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include "../src/workspaces/process_design/operations/SlotOperationState.h"
#include "../src/core/ProjectManager.h"
#include <QApplication>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    OperationListPanel panel; ProjectManager project; QTemporaryDir temp;
    MachiningOperation original;
    original.id = "original"; original.opType = OperationType::Contour;
    original.strategyId = "mill_blind_slot"; original.enabled = false;
    original.contourFeature.length = 40; original.contourFeature.width = 12; original.contourFeature.depth = 4;
    original.params.set("depth", 4); original.params.set("feedRate", 800); original.markToolpathValid();
    auto other = original; other.id = "other"; other.enabled = true;
    panel.setOperations({other, original}); project.setOperations(panel.operations());
    const auto path = temp.filePath("edit.cnext"); project.saveToFile(path);
    int writes = 0;
    QObject::connect(&panel, &OperationListPanel::operationsEdited, [&](const QList<MachiningOperation> &ops) {
        ++writes; project.setOperations(ops);
    });
    auto edited = original; edited.params.set("depth", 2); edited.params.set("feedRate", 456);
    if (!panel.replaceSlotOperation(original, edited) || writes != 1 ||
        !sameSlotOperation(panel.operations()[0], other) || panel.operations()[1].id != original.id ||
        panel.operations()[1].enabled || panel.operations()[1].contourFeature.depth != 4) {
        std::cerr << "slot editing must replace in place once and preserve identity/order/enabled/true geometry\n";
        return 1;
    }
    project.saveToFile(path);
    if (!panel.replaceSlotOperation(edited, edited) || project.isModified() || writes != 1) return 1;
    if (panel.replaceSlotOperation(original, edited) || writes != 1 || project.isModified()) return 1;
    auto missing = edited; missing.id = "missing";
    if (panel.replaceSlotOperation(missing, missing) || writes != 1) return 1;
    auto changed = edited; changed.enabled = true;
    panel.setOperations({other, changed});
    if (panel.replaceSlotOperation(edited, original) || writes != 1) return 1;
    ProjectManager reopened;
    if (!reopened.loadFromFile(path) || reopened.operations()[1].id != original.id ||
        reopened.operations()[1].params.get("depth") != 2 || reopened.operations()[1].enabled) return 1;
    return 0;
}
