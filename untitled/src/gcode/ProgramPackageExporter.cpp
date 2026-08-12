#include "ProgramPackageExporter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QRegularExpression>

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

static QString cq8MacroReferenceError(const QList<ProgramFileEntry> &files)
{
    const ProgramFileEntry *mainFile = nullptr;
    const ProgramFileEntry *macroFile = nullptr;
    for (const ProgramFileEntry &file : files) {
        if (file.kind == QStringLiteral("main") && file.fileName == QStringLiteral("CQ8_MAIN.NC")) {
            mainFile = &file;
        } else if (file.kind == QStringLiteral("macro") &&
                   file.fileName == QStringLiteral("CQ8_MACROS.NC")) {
            macroFile = &file;
        }
    }
    if (!mainFile && !macroFile) {
        return QString();
    }
    if (!mainFile || !macroFile) {
        return QStringLiteral("CQ8 package must contain both CQ8_MAIN.NC and CQ8_MACROS.NC.");
    }

    const QRegularExpression variablePattern(QStringLiteral("#(\\d+)\\b"));
    const auto variableRangeError = [&variablePattern](const ProgramFileEntry &file) {
        QRegularExpressionMatchIterator variableIt = variablePattern.globalMatch(file.content);
        while (variableIt.hasNext()) {
            const QRegularExpressionMatch match = variableIt.next();
            const int variable = match.captured(1).toInt();
            if (variable < 100 || variable > 199) {
                return QStringLiteral("CQ8 file %1 uses %2 outside the first-phase #100-#199 variable range.")
                    .arg(file.fileName, match.captured(0));
            }
        }
        return QString();
    };
    const QString mainVariableError = variableRangeError(*mainFile);
    if (!mainVariableError.isEmpty()) {
        return mainVariableError;
    }
    const QString macroVariableError = variableRangeError(*macroFile);
    if (!macroVariableError.isEmpty()) {
        return macroVariableError;
    }

    const QRegularExpression callPattern(QStringLiteral("\\bM98\\s+P(\\d+)\\b"),
                                         QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression routinePattern(QStringLiteral("^\\s*O(\\d+)\\b"),
                                            QRegularExpression::MultilineOption |
                                            QRegularExpression::CaseInsensitiveOption);
    QSet<QString> routines;
    struct RoutineRange {
        QString number;
        int start = 0;
        int end = 0;
    };
    QList<RoutineRange> routineRanges;
    QRegularExpressionMatchIterator routineIt = routinePattern.globalMatch(macroFile->content);
    while (routineIt.hasNext()) {
        const QRegularExpressionMatch match = routineIt.next();
        const QString routine = match.captured(1);
        if (routines.contains(routine)) {
            return QStringLiteral("CQ8 macro library defines O%1 more than once.").arg(routine);
        }
        routines.insert(routine);
        routineRanges.append({routine, match.capturedStart(), macroFile->content.size()});
    }
    for (int index = 0; index + 1 < routineRanges.size(); ++index) {
        routineRanges[index].end = routineRanges.at(index + 1).start;
    }
    const QRegularExpression nestedCallPattern(QStringLiteral("\\bM98\\s+P\\d+\\b"),
                                               QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression returnPattern(QStringLiteral("^\\s*M99\\s*(?:;.*)?$"),
                                           QRegularExpression::MultilineOption |
                                           QRegularExpression::CaseInsensitiveOption);
    for (const RoutineRange &range : routineRanges) {
        const QString body = macroFile->content.mid(range.start, range.end - range.start);
        if (nestedCallPattern.match(body).hasMatch()) {
            return QStringLiteral("CQ8 macro routine O%1 contains a nested M98 call.")
                .arg(range.number);
        }
        if (!returnPattern.match(body).hasMatch()) {
            return QStringLiteral("CQ8 macro routine O%1 must terminate with M99.")
                .arg(range.number);
        }
    }
    QRegularExpressionMatchIterator callIt = callPattern.globalMatch(mainFile->content);
    while (callIt.hasNext()) {
        const QString routine = callIt.next().captured(1);
        if (!routines.contains(routine)) {
            return QStringLiteral("CQ8 main program calls P%1 without a matching O%1 routine.")
                .arg(routine);
        }
    }
    return QString();
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
    const QString macroReferenceError = cq8MacroReferenceError(files);
    if (!macroReferenceError.isEmpty()) {
        report.error = macroReferenceError;
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
