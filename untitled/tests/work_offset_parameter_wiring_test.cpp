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

QString readSource(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly | QIODevice::Text)
        ? QString::fromUtf8(file.readAll())
        : QString();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const QString strategyHeader = readSource(QStringLiteral("src/ui/StrategyPanel.h"));
    const QString strategySource = readSource(QStringLiteral("src/ui/StrategyPanel.cpp"));
    const QString mainWindowSource = readSource(QStringLiteral("src/ui/MainWindow.cpp"));
    if (expect(strategyHeader.contains(QStringLiteral("setWorkOffset")),
               "the strategy panel should accept the active work offset") ||
        expect(strategySource.contains(QStringLiteral("m_workOffset")),
               "the strategy panel should retain the active work offset") ||
        expect(mainWindowSource.contains(
                   QStringLiteral("m_strategyPanel->setWorkOffset(currentWorkOffset())")),
               "changing the toolbar WCS should update new-operation parameters")) {
        return 1;
    }

    std::cout << "PASS work_offset_parameter_wiring_test\n";
    return 0;
}
