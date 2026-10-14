#include "../src/workspaces/process_design/ui/OperationParameterGroups.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QTabWidget>

#include <iostream>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTableWidget table(2, 2);
    QTabWidget tabs;
    const QStringList keys = {QStringLiteral("chamferMode"), QStringLiteral("axialDepth")};
    for (int row = 0; row < keys.size(); ++row) {
        auto *name = new QTableWidgetItem(keys.at(row));
        name->setData(Qt::UserRole, keys.at(row));
        table.setItem(row, 0, name);
        table.setItem(row, 1, new QTableWidgetItem(row == 0 ? QStringLiteral("0")
                                                           : QStringLiteral("2")));
    }

    rebuildOperationParameterGroups(&tabs, &table, QStringLiteral("hole_chamfer"));
    const auto *mode = tabs.findChild<QComboBox *>(QStringLiteral("operationParameter_chamferMode"));
    const auto *depth = tabs.findChild<QDoubleSpinBox *>(QStringLiteral("operationParameter_axialDepth"));
    if (!mode || mode->count() != 2 ||
        !mode->itemText(0).contains(QStringLiteral("圆周")) ||
        !mode->itemText(1).contains(QStringLiteral("轴向 G81"))) {
        std::cerr << "hole chamfer UI must expose two explicit machining modes\n";
        return 1;
    }
    if (!depth || depth->suffix() != QStringLiteral(" mm") ||
        !depth->toolTip().contains(QStringLiteral("用户确认"))) {
        std::cerr << "axial countersink depth must expose operator-confirmed semantics\n";
        return 1;
    }
    std::cout << "Axial/circumferential chamfer parameter UI contract passed\n";
    return 0;
}
