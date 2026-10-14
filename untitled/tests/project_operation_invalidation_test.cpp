#include "../src/core/ProjectManager.h"

#include <QCoreApplication>

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

MachiningOperation validOperation()
{
    MachiningOperation operation;
    operation.id = QStringLiteral("operation-1");
    operation.toolpathState = ToolpathState::Valid;
    return operation;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    ProjectManager project;
    project.setOperations({validOperation()});
    project.setWorkOffset(QStringLiteral("G54"));
    if (expect(project.operations().first().toolpathState == ToolpathState::Valid,
               "reapplying the same work offset must not invalidate operations")) {
        return 1;
    }

    project.setWorkOffset(QStringLiteral("G55"));
    if (expect(project.operations().first().toolpathState == ToolpathState::Stale,
               "changing the work offset must invalidate operations") ||
        expect(project.operations().first().warnings.contains(
                   QStringLiteral("work offset changed")),
               "work-offset invalidation should retain its reason")) {
        return 1;
    }

    project.setOperations({validOperation()});
    StockDefinition stock;
    stock.minusX = 1.0;
    stock.plusX = 1.0;
    stock.confirmed = true;
    project.setStockDefinition(stock);
    if (expect(project.operations().first().toolpathState == ToolpathState::Stale,
               "changing stock must invalidate operations") ||
        expect(project.operations().first().warnings.contains(QStringLiteral("stock changed")),
               "stock invalidation should retain its reason")) {
        return 1;
    }

    project.setOperations({validOperation()});
    project.setSetupRotation(QQuaternion::fromAxisAndAngle(
        QVector3D(1.0f, 0.0f, 0.0f), 90.0f));
    if (expect(project.operations().first().toolpathState == ToolpathState::Stale,
               "changing setup orientation must invalidate operations")) {
        return 1;
    }

    std::cout << "PASS project_operation_invalidation_test\n";
    return 0;
}
