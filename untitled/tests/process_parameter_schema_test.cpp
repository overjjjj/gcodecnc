#include "../src/core/ProcessParameterSchema.h"

#include <QCoreApplication>

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const ProcessParameterSchema schema = ProcessParameterSchema::CommonOperation();
    const ProcessParameterDefinition *safe_height = schema.definition(QStringLiteral("safeHeight"));
    if (!expect(safe_height != nullptr, "safe height should be part of the common schema") ||
        !expect(schema.definitions().size() == 9,
                "the common editor should be able to enumerate every schema field") ||
        !expect(safe_height->unit == ProcessParameterUnit::Millimeter,
                "safe height should use millimeters") ||
        !expect(safe_height->defaultSource == ProcessParameterSource::SystemDefault,
                "schema defaults should identify their source")) {
        return 1;
    }

    StrategyParams valid;
    valid.set(QStringLiteral("safeHeight"), 30.0);
    valid.set(QStringLiteral("plungeHeight"), 10.0);
    valid.set(QStringLiteral("referenceHeight"), 0.0);
    valid.set(QStringLiteral("depth"), 5.0);
    valid.set(QStringLiteral("stepOver"), 2.0);
    valid.set(QStringLiteral("stepDown"), 1.0);
    valid.set(QStringLiteral("feedRate"), 600.0);
    valid.set(QStringLiteral("plungeRate"), 150.0);
    valid.set(QStringLiteral("spindleSpeed"), 3200.0);

    if (!expect(schema.validate(valid).isEmpty(), "safe common parameters should validate")) {
        return 1;
    }

    valid.set(QStringLiteral("plungeHeight"), 35.0);
    const QStringList height_errors = schema.validate(valid);
    if (!expect(!height_errors.isEmpty(),
                "plunge height above safe height should be rejected")) {
        return 1;
    }

    valid.set(QStringLiteral("plungeHeight"), 10.0);
    valid.set(QStringLiteral("stepDown"), 0.0);
    const QStringList step_errors = schema.validate(valid);
    if (!expect(!step_errors.isEmpty(), "zero step down should be rejected")) {
        return 1;
    }

    valid.set(QStringLiteral("stepDown"), 1.0);
    valid.set(QStringLiteral("depth"), 0.0);
    if (!expect(!schema.validate(valid).isEmpty(),
                "zero cutting depth should be rejected")) {
        return 1;
    }

    return 0;
}
