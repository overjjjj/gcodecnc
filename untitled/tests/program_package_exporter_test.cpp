#include "../src/gcode/ProgramPackageExporter.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

static int expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << "\n";
        return 1;
    }
    return 0;
}

static QString sha256(const QString &content)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256)
            .toHex()
            .toUpper());
}

static ProgramFileEntry packageFile(const QString &kind,
                                    const QString &fileName,
                                    const QString &content)
{
    ProgramFileEntry file;
    file.kind = kind;
    file.fileName = fileName;
    file.content = content;
    file.sha256 = sha256(content);
    return file;
}

static QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QTemporaryDir tempDir;
    if (expect(tempDir.isValid(), "temporary directory should be available")) {
        return 1;
    }

    const ProgramFileEntry mainFile = packageFile(
        QStringLiteral("main"),
        QStringLiteral("WH250852_MAIN.MPF"),
        QStringLiteral("PROC WH250852_MAIN\nM30\n"));
    const ProgramFileEntry subprogramFile = packageFile(
        QStringLiteral("subprogram"),
        QStringLiteral("SP_HOLE.SPF"),
        QStringLiteral("PROC SP_HOLE(REAL PX)\nRET\n"));
    const QList<ProgramFileEntry> files{mainFile, subprogramFile};

    const ProgramPackageExportReport report = ProgramPackageExporter::exportFiles(
        tempDir.path(), mainFile.fileName, files);
    if (expect(report.ok && report.writtenFiles.size() == 2,
               "valid MPF/SPF package should export all files")) {
        return 1;
    }
    if (expect(readText(QDir(tempDir.path()).filePath(mainFile.fileName)) == mainFile.content &&
                   readText(QDir(tempDir.path()).filePath(subprogramFile.fileName)) ==
                       subprogramFile.content,
               "exported MPF/SPF contents should match the snapshot")) {
        return 1;
    }

    QTemporaryDir rejectedDir;
    QList<ProgramFileEntry> badHashFiles = files;
    badHashFiles[1].sha256 = QString(64, QLatin1Char('0'));
    const ProgramPackageExportReport badHashReport = ProgramPackageExporter::exportFiles(
        rejectedDir.path(), mainFile.fileName, badHashFiles);
    if (expect(!badHashReport.ok && QDir(rejectedDir.path()).entryList(QDir::Files).isEmpty(),
               "hash mismatch should reject the package before writing files")) {
        return 1;
    }

    QTemporaryDir traversalDir;
    QList<ProgramFileEntry> traversalFiles = files;
    traversalFiles[1].fileName = QStringLiteral("../SP_HOLE.SPF");
    const ProgramPackageExportReport traversalReport = ProgramPackageExporter::exportFiles(
        traversalDir.path(), mainFile.fileName, traversalFiles);
    if (expect(!traversalReport.ok && QDir(traversalDir.path()).entryList(QDir::Files).isEmpty(),
               "path traversal filename should be rejected before writing files")) {
        return 1;
    }

    QTemporaryDir cq8Dir;
    const ProgramFileEntry cq8Main = packageFile(
        QStringLiteral("main"), QStringLiteral("CQ8_MAIN.NC"),
        QStringLiteral("O1000\nM98 P9001\nM30\n"));
    const ProgramFileEntry cq8Macros = packageFile(
        QStringLiteral("macro"), QStringLiteral("CQ8_MACROS.NC"),
        QStringLiteral("O9002\nM99\n"));
    const ProgramPackageExportReport missingRoutineReport = ProgramPackageExporter::exportFiles(
        cq8Dir.path(), cq8Main.fileName, {cq8Main, cq8Macros});
    if (expect(!missingRoutineReport.ok &&
                   missingRoutineReport.error.contains(QStringLiteral("P9001")) &&
                   QDir(cq8Dir.path()).entryList(QDir::Files).isEmpty(),
               "CQ8 export must reject a main-program macro call without a matching routine")) {
        return 1;
    }

    QTemporaryDir duplicateRoutineDir;
    const ProgramFileEntry duplicateMacros = packageFile(
        QStringLiteral("macro"), QStringLiteral("CQ8_MACROS.NC"),
        QStringLiteral("O9001\nM99\nO9001\nM99\n"));
    const ProgramPackageExportReport duplicateRoutineReport = ProgramPackageExporter::exportFiles(
        duplicateRoutineDir.path(), cq8Main.fileName, {cq8Main, duplicateMacros});
    if (expect(!duplicateRoutineReport.ok &&
                   duplicateRoutineReport.error.contains(QStringLiteral("more than once")),
               "CQ8 export must reject duplicate macro routine numbers")) {
        return 1;
    }

    QTemporaryDir nestedCallDir;
    const ProgramFileEntry nestedCallMacros = packageFile(
        QStringLiteral("macro"), QStringLiteral("CQ8_MACROS.NC"),
        QStringLiteral("O9001\nM98 P9002\nM99\nO9002\nM99\n"));
    const ProgramPackageExportReport nestedCallReport = ProgramPackageExporter::exportFiles(
        nestedCallDir.path(), cq8Main.fileName, {cq8Main, nestedCallMacros});
    if (expect(!nestedCallReport.ok &&
                   nestedCallReport.error.contains(QStringLiteral("nested")),
               "CQ8 export must reject nested macro calls in the first phase")) {
        return 1;
    }

    QTemporaryDir missingReturnDir;
    const ProgramFileEntry missingReturnMacros = packageFile(
        QStringLiteral("macro"), QStringLiteral("CQ8_MACROS.NC"),
        QStringLiteral("O9001\nG1 X10.000\n"));
    const ProgramPackageExportReport missingReturnReport = ProgramPackageExporter::exportFiles(
        missingReturnDir.path(), cq8Main.fileName, {cq8Main, missingReturnMacros});
    if (expect(!missingReturnReport.ok &&
                   missingReturnReport.error.contains(QStringLiteral("M99")),
               "CQ8 export must reject a macro routine without M99 return")) {
        return 1;
    }

    return 0;
}
