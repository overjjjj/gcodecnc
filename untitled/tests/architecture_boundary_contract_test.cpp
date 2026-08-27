#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QString>
#include <QTextStream>

namespace {

void require(bool condition, const QString &message)
{
    if (condition) {
        return;
    }
    QTextStream(stderr) << "FAIL: " << message << Qt::endl;
    ::exit(1);
}

QString readFile(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly | QIODevice::Text),
            QStringLiteral("cannot read %1").arg(path));
    return QString::fromUtf8(file.readAll());
}

void requireNoUiDependency(const QString &directory)
{
    QDirIterator iterator(directory,
                          QStringList{QStringLiteral("*.h"), QStringLiteral("*.cpp")},
                          QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QString source = readFile(path);
        require(!source.contains(QStringLiteral("/ui/")) &&
                    !source.contains(QStringLiteral("\\ui\\")) &&
                    !source.contains(QStringLiteral("<QWidget")) &&
                    !source.contains(QStringLiteral("<QDialog")) &&
                    !source.contains(QStringLiteral("<QMainWindow")),
                QStringLiteral("architecture boundary violation in %1").arg(path));
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    requireNoUiDependency(QStringLiteral("src/core"));
    requireNoUiDependency(QStringLiteral("src/strategies"));

    const QString autoHole = readFile(
        QStringLiteral("src/services/AutoHolePlanningService.cpp"));
    require(autoHole.contains(QStringLiteral("OperationFactory::CreateConfirmed")) &&
                !autoHole.contains(QStringLiteral("setOperations(")),
            QStringLiteral("automatic planning must create through OperationFactory without writing the operation tree"));

    const QString project = readFile(QStringLiteral("src/core/ProjectManager.cpp"));
    require(project.contains(QStringLiteral("processTemplateLibrary")) &&
                !project.contains(QStringLiteral("setProcessTemplateLibrary(") +
                                  QStringLiteral("\n{") +
                                  QStringLiteral("\n    for (")),
            QStringLiteral("template updates must not iterate over and rewrite confirmed operations"));

    const QString mainWindow = readFile(QStringLiteral("src/ui/MainWindow.cpp"));
    require(mainWindow.contains(QStringLiteral("program.expandedGcodeText.isEmpty()")) &&
                mainWindow.contains(QStringLiteral("m_simCtrl->loadGCode")),
            QStringLiteral("execution preview must load final postprocessed output or its exact expansion"));
    require(mainWindow.contains(QStringLiteral(
                "addDesignCommand(automationMenu, QStringLiteral(\"autoSlotFrame\"), false);")) &&
                mainWindow.contains(QStringLiteral(
                    "addDesignCommand(surfacesMenu, QStringLiteral(\"slopeMill3D\"), false);")),
            QStringLiteral("unverified automatic-frame and 3D strategies must remain disabled"));

    QTextStream(stdout) << "PASS architecture_boundary_contract_test" << Qt::endl;
    return 0;
}
