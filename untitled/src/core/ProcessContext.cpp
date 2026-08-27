#include "ProcessContext.h"

int SetupContext::workOffsetNumber() const
{
    bool ok = false;
    const QString normalized = workOffset.trimmed().toUpper();
    const int number = normalized.mid(1).toInt(&ok);
    return ok && normalized.startsWith(QLatin1Char('G')) ? number : -1;
}

QStringList SetupContext::validate() const
{
    QStringList errors;
    const int number = workOffsetNumber();
    if (number < 54 || number > 59) {
        errors.append(QStringLiteral("Work offset must be G54 through G59"));
    }
    if (!origin.confirmed) {
        errors.append(QStringLiteral("Machining setup must be confirmed"));
    }
    return errors;
}

QStringList ProcessContext::validate() const
{
    QStringList errors = setup.validate();
    if (toolId <= 0) {
        errors.append(QStringLiteral("A valid tool must be selected"));
    }
    return errors;
}
