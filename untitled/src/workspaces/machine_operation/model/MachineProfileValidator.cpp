#include "MachineProfileValidator.h"

#include <QSet>

MachineProfileValidationResult MachineProfileValidator::validate(const MachineProfile &profile)
{
    MachineProfileValidationResult result;
    QSet<QString> words;
    for (const QString &block : profile.safeStartBlocks) {
        const QStringList blockWords =
            block.toUpper().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &word : blockWords) {
            words.insert(word);
        }
    }

    const QStringList required = {
        QStringLiteral("G17"), QStringLiteral("G40"), QStringLiteral("G49"),
        QStringLiteral("G80"), QStringLiteral("G21"), QStringLiteral("G90"),
        QStringLiteral("G94")
    };
    for (const QString &word : required) {
        if (!words.contains(word)) {
            result.errors.append(QStringLiteral("Missing required safe-start code %1.").arg(word));
        }
    }
    if (words.contains(QStringLiteral("G91"))) {
        result.errors.append(QStringLiteral("Incremental coordinate mode G91 is not allowed."));
    }

    bool hasWorkOffset = false;
    for (int code = 54; code <= 59; ++code) {
        if (words.contains(QStringLiteral("G%1").arg(code))) {
            hasWorkOffset = true;
            break;
        }
    }
    if (!hasWorkOffset) {
        result.errors.append(QStringLiteral("Missing work offset G54-G59."));
    }
    if (profile.id.trimmed().isEmpty()) {
        result.errors.append(QStringLiteral("Machine Profile ID is required."));
    }
    if (profile.version.trimmed().isEmpty()) {
        result.errors.append(QStringLiteral("Machine Profile version is required."));
    }
    if (profile.controllerId.trimmed().isEmpty()) {
        result.errors.append(QStringLiteral("Controller ID is required."));
    }

    result.ok = result.errors.isEmpty();
    return result;
}
