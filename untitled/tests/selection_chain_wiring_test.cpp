#include <QCoreApplication>
#include <QFile>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile file(QStringLiteral("src/app/MainWindow.cpp"));
    if (!file.open(QIODevice::ReadOnly)) return 1;
    const QString source = QString::fromUtf8(file.readAll());
    if (!source.contains(QStringLiteral("validateAndCommitMillingBatch(confirmedOperations"))) {
        std::cerr << "milling commit must validate the final confirmed operations\n";
        return 1;
    }
    if (!source.contains(QStringLiteral("new SelectionChainDialog(m_selectionPanelHost)")) ||
        !source.contains(QStringLiteral("m_workflowSelection = dialog->selection()")) ||
        !source.contains(QStringLiteral("&QDialog::accepted")) ||
        source.contains(QStringLiteral("selectionDialog.exec()")) ||
        source.contains(QStringLiteral("selection.explicitUserSelection = true"))) {
        std::cerr << "milling proposals must obtain evidence from the common confirmed selection dialog\n";
        return 1;
    }
    return 0;
}
