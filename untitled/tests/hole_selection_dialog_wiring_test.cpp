#include <QCoreApplication>
#include <QFile>
#include <QString>

#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QFile sourceFile(QStringLiteral("src/ui/MainWindow.cpp"));
    if (!sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "main-window source should be readable\n";
        return 1;
    }
    const QString source = QString::fromUtf8(sourceFile.readAll());
    if (!source.contains(QStringLiteral("HoleSelectionDialog")) ||
        !source.contains(QStringLiteral("stableFeatureId"))) {
        std::cerr << "batch hole confirmation should use the hole selection dialog\n";
        return 1;
    }
    std::cout << "PASS hole_selection_dialog_wiring_test\n";
    return 0;
}
