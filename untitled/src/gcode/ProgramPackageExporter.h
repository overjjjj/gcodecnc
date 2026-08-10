#pragma once

#include "../core/ProjectManager.h"

struct ProgramPackageExportReport {
    bool ok = false;
    QString error;
    QStringList writtenFiles;
};

class ProgramPackageExporter
{
public:
    static ProgramPackageExportReport exportFiles(
        const QString &targetDirectory,
        const QString &mainProgramFileName,
        const QList<ProgramFileEntry> &files);
};
