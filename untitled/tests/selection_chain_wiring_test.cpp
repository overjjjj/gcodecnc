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
    if (!source.contains(QStringLiteral("SelectionChainDialog selectionDialog")) ||
        !source.contains(QStringLiteral("selectionDialog.selection()")) ||
        source.contains(QStringLiteral("selection.explicitUserSelection = true")) ||
        !source.contains(QStringLiteral("ChainGeometrySource::Wire")) ||
        !source.contains(QStringLiteral("ChainGeometrySource::Stock"))) {
        std::cerr << "milling proposals must obtain evidence from the common confirmed selection dialog\n";
        return 1;
    }
    return 0;
}
