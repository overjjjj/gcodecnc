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

    QFile sourceFile(QStringLiteral("src/ui/MainWindow.cpp"));
    if (expect(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
               "main-window source should be readable")) {
        return 1;
    }
    const QString source = QString::fromUtf8(sourceFile.readAll());
    const int firstConfirmation = source.indexOf(
        QStringLiteral("const QStringList addedIds =\n            m_operationPanel->addConfirmedOperations"));

    if (expect(firstConfirmation >= 0,
               "confirmed operations should be added through the operation panel") ||
        expect(source.lastIndexOf(QStringLiteral("markToolpathValid()"), firstConfirmation) >= 0,
               "a successfully generated operation must be marked valid before it is confirmed") ||
        expect(source.contains(QStringLiteral("OperationListPanel::recalculateRequested")) &&
                   source.contains(QStringLiteral("setToolpathResult")),
               "recalculation requests should write success or error state back to each operation")) {
        return 1;
    }

    std::cout << "PASS operation_lifecycle_wiring_test\n";
    return 0;
}
