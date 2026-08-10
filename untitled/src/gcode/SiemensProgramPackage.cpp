#include "SiemensProgramPackage.h"

#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace {

static QString normalizedProgramName(const QString &value, const QString &prefix)
{
    QString name;
    bool previousUnderscore = false;
    for (const QChar ch : value.toUpper()) {
        const bool accepted = (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
                              (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'));
        if (accepted) {
            name.append(ch);
            previousUnderscore = false;
        } else if (!previousUnderscore && !name.isEmpty()) {
            name.append(QLatin1Char('_'));
            previousUnderscore = true;
        }
    }
    while (name.endsWith(QLatin1Char('_'))) {
        name.chop(1);
    }

    const bool startsWithTwoLetters = name.size() >= 2 &&
                                      name.at(0).isLetter() &&
                                      name.at(1).isLetter();
    if (!startsWithTwoLetters) {
        name.prepend(prefix);
    }
    if (name.isEmpty()) {
        name = prefix + QStringLiteral("PROGRAM");
    }
    return name.left(24);
}

static QString normalizedLines(const QStringList &lines)
{
    QString content;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            content += trimmed + QLatin1Char('\n');
        }
    }
    return content;
}

static QString sha256(const QString &content)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256)
            .toHex()
            .toUpper());
}

static bool sameDefinition(const SiemensProgramSection &left,
                           const SiemensProgramSection &right)
{
    return left.parameterDeclarations == right.parameterDeclarations &&
           left.subprogramBodyLines == right.subprogramBodyLines;
}

static bool containsForbiddenSpfState(const QStringList &lines)
{
    static const QRegularExpression forbiddenWord(
        QStringLiteral("(^|\\s)(?:G(?:17|18|19|40|41|42|49|5[4-9]|80|90|91|94)|"
                       "M(?:3|4|5|6|7|8|9|30)|T\\d+|S\\d+)(?=\\s|$)"));
    for (QString line : lines) {
        const int commentStart = line.indexOf(QLatin1Char(';'));
        if (commentStart >= 0) {
            line.truncate(commentStart);
        }
        if (forbiddenWord.match(line.toUpper().simplified()).hasMatch()) {
            return true;
        }
    }
    return false;
}

} // namespace

SiemensProgramPackage SiemensProgramPackageBuilder::build(
    const SiemensProgramPackageRequest &request)
{
    SiemensProgramPackage package;
    const QString mainName = normalizedProgramName(request.mainProgramName,
                                                   QStringLiteral("MP_"));

    QHash<QString, int> repeatCounts;
    QHash<QString, SiemensProgramSection> definitions;
    QStringList repeatedKeys;
    for (const SiemensProgramSection &section : request.sections) {
        if (section.repeatKey.trimmed().isEmpty()) {
            continue;
        }
        const QString key = section.repeatKey.trimmed();
        ++repeatCounts[key];
        if (!definitions.contains(key)) {
            definitions.insert(key, section);
            repeatedKeys.append(key);
        } else if (!sameDefinition(definitions.value(key), section)) {
            package.error = QStringLiteral("Repeated section '%1' has conflicting SPF definitions.")
                                .arg(key);
            return package;
        }
    }

    QHash<QString, QString> subprogramNames;
    QSet<QString> usedNames;
    for (const QString &key : repeatedKeys) {
        if (repeatCounts.value(key) < 2) {
            continue;
        }
        const SiemensProgramSection definition = definitions.value(key);
        if (containsForbiddenSpfState(definition.subprogramBodyLines)) {
            package.error = QStringLiteral(
                "SPF '%1' contains tool, spindle, coolant, WCS, or safety-state commands.")
                                .arg(key);
            return package;
        }
        const QString subprogramName = normalizedProgramName(
            QStringLiteral("SP_") + definition.preferredSubprogramName,
            QStringLiteral("SP_"));
        if (usedNames.contains(subprogramName)) {
            package.error = QStringLiteral("Repeated sections resolve to duplicate SPF name '%1'.")
                                .arg(subprogramName);
            return package;
        }
        usedNames.insert(subprogramName);
        subprogramNames.insert(key, subprogramName);

        SiemensProgramFile file;
        file.fileName = subprogramName + QStringLiteral(".SPF");
        file.content = QStringLiteral("PROC %1").arg(subprogramName);
        if (!definition.parameterDeclarations.isEmpty()) {
            file.content += QLatin1Char('(') +
                            definition.parameterDeclarations.join(QStringLiteral(", ")) +
                            QLatin1Char(')');
        }
        file.content += QLatin1Char('\n');
        file.content += normalizedLines(definition.subprogramBodyLines);
        file.content += QStringLiteral("RET\n");
        file.sha256 = sha256(file.content);
        package.subprograms.append(file);
    }

    package.mainProgram.fileName = mainName + QStringLiteral(".MPF");
    package.mainProgram.content = QStringLiteral("PROC %1\n").arg(mainName);
    package.mainProgram.content += normalizedLines(request.mainPreambleLines);
    for (const SiemensProgramSection &section : request.sections) {
        const QString key = section.repeatKey.trimmed();
        if (!key.isEmpty() && subprogramNames.contains(key)) {
            package.mainProgram.content += subprogramNames.value(key) +
                                           QLatin1Char('(') +
                                           section.callArguments.join(QLatin1Char(',')) +
                                           QStringLiteral(")\n");
        } else {
            package.mainProgram.content += normalizedLines(section.inlineLines);
        }
    }
    package.mainProgram.content += normalizedLines(request.mainPostambleLines);
    package.mainProgram.sha256 = sha256(package.mainProgram.content);
    package.ok = true;
    return package;
}

SiemensProgramPackage SiemensProgramPackageBuilder::fromValidatedMainProgram(
    const QString &mainProgramName,
    const QString &validatedGCode)
{
    SiemensProgramPackage package;
    if (validatedGCode.trimmed().isEmpty()) {
        package.error = QStringLiteral("Validated main program is empty.");
        return package;
    }

    const QString normalizedName = normalizedProgramName(mainProgramName,
                                                         QStringLiteral("MP_"));
    package.mainProgram.fileName = normalizedName + QStringLiteral(".MPF");
    package.mainProgram.content = validatedGCode;
    package.mainProgram.sha256 = sha256(validatedGCode);
    package.ok = true;
    return package;
}
