#include "../src/workspaces/process_design/ui/HoleSelectionDialog.h"

#include <QApplication>
#include <QTableWidget>

#include <iostream>

namespace {

int expect(bool condition, const char *message)
{
    if (condition) {
        return 0;
    }
    std::cerr << message << '\n';
    return 1;
}

HoleSelectionRecord record(const QString &id, double diameter, float x)
{
    HoleSelectionRecord value;
    value.geometryId = id;
    value.diameter = diameter;
    value.center = QVector3D(x, 0.0f, 0.0f);
    return value;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    HoleSelectionDialog dialog;
    dialog.setChineseUi(true);
    dialog.setRecords({record(QStringLiteral("hole-small"), 6.0, 0.0f),
                       record(QStringLiteral("hole-large"), 12.0, 10.0f)});

    if (expect(dialog.records().size() == 2,
               "the dialog should display every selected hole") ||
        expect(dialog.sortDiameterDescending(),
               "the dialog should expose diameter sorting") ||
        expect(dialog.records().first().geometryId == QStringLiteral("hole-large"),
               "diameter sorting should update the displayed order") ||
        expect(dialog.undo(), "the dialog should expose undo for sorting") ||
        expect(dialog.records().first().geometryId == QStringLiteral("hole-small"),
               "undo should restore the selection order") ||
        expect(dialog.confirm(), "the dialog should confirm the selected order") ||
        expect(dialog.result() == QDialog::Accepted,
               "confirming the dialog should accept the selection session")) {
        return 1;
    }

    dialog.setRecords({record("hole-small", 6, 0), record("hole-large", 12, 10)});
    const auto original = dialog.records();
    int notifications = 0;
    QList<HoleSelectionRecord> preview;
    dialog.selectionChanged = [&](const QList<HoleSelectionRecord> &value) { ++notifications; preview = value; };
    dialog.toggleGeometry("hole-small");
    if (expect(dialog.records().size() == 1, "model toggle should exclude a hole") ||
        expect(dialog.undo(), "undo must restore a model-picked hole subset") ||
        expect(dialog.records() == original && preview == original, "undo must restore selection and preview") ||
        expect(dialog.redo() && dialog.records().size() == 1, "redo must reapply hole exclusion")) return 1;
    dialog.sortDiameterDescending();
    if (expect(dialog.undo() && dialog.records().size() == 1, "undo sort must preserve subset") ||
        expect(dialog.undo() && dialog.records() == original, "second undo must restore subset")) return 1;
    auto *table = dialog.findChild<QTableWidget *>();
    table->item(1, 0)->setCheckState(Qt::Unchecked);
    if (expect(!dialog.redo(), "new checkbox edit must discard redo history") ||
        expect(dialog.records().first().geometryId == "hole-small", "checkbox must update subset") ||
        expect(dialog.undo() && dialog.records() == original, "checkbox edit must be undoable")) return 1;
    dialog.toggleGeometry("hole-small");
    dialog.toggleGeometry("hole-large");
    if (expect(!dialog.confirm(), "empty subset must not be accepted")) return 1;
    dialog.reject();
    if (expect(dialog.records() == original, "cancel must restore the opening subset and order") ||
        expect(!dialog.undo() && !dialog.redo(), "cancel must clear pending history")) return 1;
    dialog.toggleGeometry("hole-small");
    if (expect(dialog.confirm(), "nonempty subset should confirm")) return 1;
    dialog.toggleGeometry("hole-large");
    dialog.reject();
    if (expect(dialog.records().size() == 1 && dialog.records().first().geometryId == "hole-large",
               "cancel after reselection must restore last confirmed subset") ||
        expect(notifications > 0 && preview == dialog.records(), "preview must match restored subset")) return 1;
    std::cout << "PASS hole_selection_dialog_test\n";
    return 0;
}
