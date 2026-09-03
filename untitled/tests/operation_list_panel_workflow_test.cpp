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

    if (expect(source.contains(QStringLiteral("enabledOperations(m_operations)")),
               "program generation should exclude disabled operations") ||
        expect(source.contains(QStringLiteral("Qt::ItemIsUserCheckable")) &&
                   source.contains(QStringLiteral("onItemChanged")),
               "the operation list should expose an enabled checkbox") ||
        expect(source.contains(QStringLiteral("toolpathStateText")),
               "the operation list should expose toolpath state") ||
        expect(source.contains(QStringLiteral("markToolpathStale")) &&
                   source.contains(QStringLiteral("tool changed")),
               "changing an operation tool should invalidate its toolpath") ||
        expect(source.contains(QStringLiteral("recalculateRequested")) &&
                   source.contains(QStringLiteral("setToolpathResult")),
               "the operation list should request and display a selected-toolpath recalculation")) {
        return 1;
    }

    std::cout << "PASS operation_list_panel_workflow_test\n";
    return 0;
}
