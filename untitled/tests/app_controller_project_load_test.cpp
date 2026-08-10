#include "../src/core/AppController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <iostream>

static int expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << "\n";
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const QString projectPath =
        QDir::temp().filePath(QStringLiteral("cnext_app_controller_missing_source_test.cnext"));
    const QString missingSource = projectPath + QStringLiteral(".missing.step");
    QFile::remove(projectPath);
    QFile::remove(missingSource);

    QJsonObject root;
    root[QStringLiteral("version")] = QStringLiteral("1.7");
    root[QStringLiteral("sourceFilePath")] = missingSource;
    root[QStringLiteral("sourceFileFingerprint")] = QStringLiteral("sha256:missing");
    root[QStringLiteral("currentProgramId")] = QString();
    root[QStringLiteral("features")] = QJsonArray();
    root[QStringLiteral("operations")] = QJsonArray();
    root[QStringLiteral("programs")] = QJsonArray();
    {
        QFile file(projectPath);
        if (expect(file.open(QIODevice::WriteOnly), "project fixture should open")) {
            return 1;
        }
        file.write(QJsonDocument(root).toJson());
    }

    AppController &controller = AppController::instance();
    controller.projectManager()->setSourceFilePath(QStringLiteral("original.step"));
    controller.projectManager()->setSourceFileFingerprint(QStringLiteral("sha256:original"));

    QString errorMessage;
    QObject::connect(&controller, &AppController::errorOccurred,
                     [&errorMessage](const QString &message) { errorMessage = message; });

    const bool loaded = controller.loadProject(projectPath);
    if (expect(!loaded, "project with a missing STEP source should fail to load")) {
        return 1;
    }
    if (expect(controller.lastProjectLoadIssue() == ProjectLoadIssue::SourceMissing,
               "missing STEP should be classified as a recoverable source issue")) {
        return 1;
    }
    if (expect(!errorMessage.isEmpty(), "failed project load should emit an error")) {
        return 1;
    }
    if (expect(controller.projectManager()->sourceFilePath() == QStringLiteral("original.step")
                   && controller.projectManager()->sourceFileFingerprint()
                          == QStringLiteral("sha256:original"),
               "failed project load should preserve the active project")) {
        return 1;
    }

    const QString changedSource = projectPath + QStringLiteral(".changed.step");
    {
        QFile sourceFile(changedSource);
        if (expect(sourceFile.open(QIODevice::WriteOnly), "changed STEP fixture should open")) {
            return 1;
        }
        sourceFile.write("changed-step-content");
    }
    root[QStringLiteral("sourceFilePath")] = changedSource;
    root[QStringLiteral("sourceFileFingerprint")] = QStringLiteral("sha256:not-the-current-file");
    {
        QFile file(projectPath);
        if (expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
                   "changed project fixture should open")) {
            return 1;
        }
        file.write(QJsonDocument(root).toJson());
    }

    errorMessage.clear();
    const bool changedLoaded = controller.loadProject(projectPath);
    if (expect(!changedLoaded, "project with a changed STEP source should require relinking")) {
        return 1;
    }
    if (expect(controller.lastProjectLoadIssue() == ProjectLoadIssue::SourceChanged,
               "changed STEP should be classified separately from a missing source")) {
        return 1;
    }
    if (expect(controller.projectManager()->sourceFilePath() == QStringLiteral("original.step"),
               "changed STEP rejection should preserve the active project")) {
        return 1;
    }

    QFile::remove(projectPath);
    QFile::remove(changedSource);
    return 0;
}
