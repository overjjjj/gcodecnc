#include "../src/workspaces/process_design/ui/OperationEditDialog.h"
#include "../src/tool/ToolLibrary.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <iostream>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ToolEntry drill;
    drill.id = 913;
    drill.type = QStringLiteral("drill");
    drill.name = QStringLiteral("麻花钻");
    drill.diameter = 6;
    drill.fluteLen = 40;
    ToolLibrary::instance().addTool(drill);
    MachiningOperation operation;
    operation.id = QStringLiteral("stable-operation-id");
    operation.strategyId = QStringLiteral("hole_deephole");
    operation.toolId = 913;
    operation.holeFeature.radius = 3;
    operation.holeFeature.depth = 12;
    operation.holeFeature.axis = QVector3D(0, 0, 1);
    operation.holeFeature.region = FaceRegion::Front;
    operation.holeFeature.coordinatePrecisionVersion = 1;
    operation.params.set(QStringLiteral("depth"), 12);
    operation.params.set(QStringLiteral("peckDepth"), 2);
    operation.params.set(QStringLiteral("feedRate"), 150);
    operation.params.set(QStringLiteral("spindleSpeed"), 1800);
    operation.params.set(QStringLiteral("safeHeight"), 50);
    operation.params.set(QStringLiteral("feedHeight"), 2);
    OperationEditDialog cancelled(operation);
    auto *peck = cancelled.findChild<QDoubleSpinBox *>(QStringLiteral("operationParameter_peckDepth"));
    if (!peck || peck->value() != 2) { std::cerr << "Parameter echo missing\n"; return 1; }
    peck->setValue(3);
    cancelled.reject();
    if (cancelled.params().get(QStringLiteral("peckDepth")) != 2 || cancelled.toolId() != 913) return 2;
    OperationEditDialog accepted(operation);
    peck = accepted.findChild<QDoubleSpinBox *>(QStringLiteral("operationParameter_peckDepth"));
    peck->setValue(0);
    accepted.accept();
    if (accepted.result() == QDialog::Accepted) return 3;
    peck->setValue(3);
    accepted.accept();
    if (accepted.result() != QDialog::Accepted || accepted.params().get(QStringLiteral("peckDepth")) != 3) return 4;
    if (accepted.operationId() != operation.id || operation.params.get(QStringLiteral("peckDepth")) != 2) return 5;
    ToolEntry mill = drill;
    mill.id = 914;
    mill.type = QStringLiteral("end_mill");
    ToolLibrary::instance().addTool(mill);
    operation.toolId = 914;
    OperationEditDialog incompatible(operation);
    incompatible.accept();
    if (incompatible.result() == QDialog::Accepted) return 6;
    std::cout << "Operation edit cancellation, echo, update, validation and identity passed\n";
    return 0;
}
