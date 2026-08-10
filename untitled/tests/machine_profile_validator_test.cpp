#include "../src/core/MachineProfileValidator.h"

#include <QCoreApplication>
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

    const MachineProfile defaultProfile;
    if (expect(MachineProfileValidator::validate(defaultProfile).ok,
               "default Siemens Machine Profile should be valid")) {
        return 1;
    }

    MachineProfile missingCycleCancel = defaultProfile;
    missingCycleCancel.safeStartBlocks.removeFirst();
    missingCycleCancel.safeStartBlocks.prepend(QStringLiteral("G17 G40 G49"));
    const MachineProfileValidationResult missingG80 =
        MachineProfileValidator::validate(missingCycleCancel);
    if (expect(!missingG80.ok && missingG80.errors.join('\n').contains(QStringLiteral("G80")),
               "profile missing G80 should be rejected")) {
        return 1;
    }

    MachineProfile incremental = defaultProfile;
    incremental.safeStartBlocks.replace(2, QStringLiteral("G91"));
    const MachineProfileValidationResult incrementalResult =
        MachineProfileValidator::validate(incremental);
    if (expect(!incrementalResult.ok
                   && incrementalResult.errors.join('\n').contains(QStringLiteral("G91")),
               "profile containing G91 should be rejected")) {
        return 1;
    }

    return 0;
}
