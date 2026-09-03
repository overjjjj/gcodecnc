#include "../src/ui/HoleSelectionDialog.h"

#include <QApplication>

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

    std::cout << "PASS hole_selection_dialog_test\n";
    return 0;
}
