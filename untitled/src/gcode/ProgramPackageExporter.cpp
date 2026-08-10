#include "ProgramPackageExporter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

namespace {

static QString sha256(const QString &content)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256)
            .toHex()
            .toUpper());
}

static bool isSafeFileName(const QString &fileName)
{
    return !fileName.trimmed().isEmpty() &&
           fileName == QFileInfo(fileName).fileName() &&
           !fileName.contains(QLatin1Char('/')) &&
           !fileName.contains(QLatin1Char('\\')) &&
           fileName != QStringLiteral(".") &&
           fileName != QStringLiteral("..");
}

} // namespace

ProgramPackageExportReport ProgramPackageExporter::exportFiles(
    const QString &targetDirectory,
    const QString &mainProgramFileName,
    const QList<ProgramFileEntry> &files)
{
    ProgramPackageExportReport report;
    const QDir targetDir(targetDirectory);
    if (!targetDir.exists()) {
        report.error = QStringLiteral("Program Package target directory does not exist.");
        return report;
    }
    if (files.isEmpty()) {
        report.error = QStringLiteral("Program Package contains no files.");
        return report;
    }

    bool mainProgramFound = false;
    QSet<QString> names;
    for (const ProgramFileEntry &file : files) {
        if (!isSafeFileName(file.fileName)) {
            report.error = QStringLiteral("Program Package contains an unsafe filename: %1")
                               .arg(file.fileName);
            return report;
        }
        const QString normalizedName = file.fileName.toUpper();
        if (names.contains(normalizedName)) {
            report.error = QStringLiteral("Program Package contains a duplicate filename: %1")
                               .arg(file.fileName);
            return report;
        }
        names.insert(normalizedName);
        if (file.fileName.compare(mainProgramFileName, Qt::CaseInsensitive) == 0 &&
            file.kind == QStringLiteral("main")) {
            mainProgramFound = true;
        }
        if (file.sha256.size() != 64 ||
            sha256(file.content).compare(file.sha256, Qt::CaseInsensitive) != 0) {
            report.error = QStringLiteral("Program Package hash mismatch: %1")
                               .arg(file.fileName);
            return report;
        }
    }
    if (!mainProgramFound) {
        report.error = QStringLiteral("Program Package main MPF is missing.");
        return report;
    }

    for (const ProgramFileEntry &file : files) {
        const QString path = targetDir.filePath(file.fileName);
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly)) {
            report.error = QStringLiteral("Cannot write Program Package file: %1")
                               .arg(path);
            return report;
        }
        const QByteArray bytes = file.content.toUtf8();
        if (output.write(bytes) != bytes.size() || !output.commit()) {
            report.error = QStringLiteral("Cannot commit Program Package file: %1")
                               .arg(path);
            return report;
        }
        report.writtenFiles.append(path);
    }

    report.ok = true;
    return report;
}
