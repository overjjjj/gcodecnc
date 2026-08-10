#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct SiemensProgramFile {
    QString fileName;
    QString content;
    QString sha256;
};

struct SiemensProgramSection {
    QString repeatKey;
    QString preferredSubprogramName;
    QStringList parameterDeclarations;
    QStringList subprogramBodyLines;
    QStringList callArguments;
    QStringList inlineLines;
};

struct SiemensProgramPackageRequest {
    QString mainProgramName;
    QStringList mainPreambleLines;
    QList<SiemensProgramSection> sections;
    QStringList mainPostambleLines;
};

struct SiemensProgramPackage {
    bool ok = false;
    QString error;
    SiemensProgramFile mainProgram;
    QList<SiemensProgramFile> subprograms;
};

class SiemensProgramPackageBuilder
{
public:
    static SiemensProgramPackage build(const SiemensProgramPackageRequest &request);
    static SiemensProgramPackage fromValidatedMainProgram(
        const QString &mainProgramName,
        const QString &validatedGCode);
};
