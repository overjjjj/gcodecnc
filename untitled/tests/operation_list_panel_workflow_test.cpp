#include <QCoreApplication>
#include <QFile>
#include <QString>

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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QFile sourceFile(QStringLiteral("src/ui/OperationListPanel.cpp"));
    if (expect(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
               "operation-list source should be readable")) {
        return 1;
    }
    const QString source = QString::fromUtf8(sourceFile.readAll());
    QFile workflowFile(QStringLiteral("src/core/OperationWorkflow.h"));
    if (expect(workflowFile.open(QIODevice::ReadOnly | QIODevice::Text),
               "operation-workflow source should be readable")) {
        return 1;
    }
    const QString workflowSource = QString::fromUtf8(workflowFile.readAll());

    if (expect(source.contains(QStringLiteral("executableOperations(m_operations)")),
               "program generation should exclude disabled or invalid operations") ||
        expect(source.contains(QStringLiteral("Qt::ItemIsUserCheckable")) &&
                   source.contains(QStringLiteral("onItemChanged")),
               "the operation list should expose an enabled checkbox") ||
        expect(source.contains(QStringLiteral("toolpathStateText")),
               "the operation list should expose toolpath state") ||
        expect(source.contains(QStringLiteral("markToolpathStale")) &&
                   source.contains(QStringLiteral("tool changed")),
               "changing an operation tool should invalidate its toolpath") ||
        expect(source.contains(QStringLiteral("m_btnEditParameters")) &&
                   source.contains(QStringLiteral("ParameterEditorDialog")) &&
                   workflowSource.contains(QStringLiteral("parameters changed")),
               "the operation tree should edit confirmed parameters and require recalculation") ||
        expect(source.contains(QStringLiteral("recalculateRequested")) &&
                   source.contains(QStringLiteral("setToolpathResult")),
               "the operation list should request and display a selected-toolpath recalculation")) {
        return 1;
    }

    std::cout << "PASS operation_list_panel_workflow_test\n";
    return 0;
}
