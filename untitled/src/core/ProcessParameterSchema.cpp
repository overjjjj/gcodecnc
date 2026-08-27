#include "ProcessParameterSchema.h"

#include <cmath>

namespace {

ProcessParameterDefinition NumberDefinition(
    const QString &id,
    ProcessParameterUnit unit,
    double default_value,
    double minimum,
    bool has_minimum,
    double maximum = 0.0,
    bool has_maximum = false)
{
    ProcessParameterDefinition definition;
    definition.id = id;
    definition.type = ProcessParameterType::Number;
    definition.unit = unit;
    definition.defaultValue = default_value;
    definition.minimum = minimum;
    definition.maximum = maximum;
    definition.hasMinimum = has_minimum;
    definition.hasMaximum = has_maximum;
    definition.required = true;
    return definition;
}

void AddError(QStringList *errors, const QString &message)
{
    if (!errors->contains(message)) {
        errors->append(message);
    }
}

} // namespace

ProcessParameterSchema ProcessParameterSchema::CommonOperation()
{
    ProcessParameterSchema schema;
    schema.definitions_.append(NumberDefinition(
        QStringLiteral("safeHeight"), ProcessParameterUnit::Millimeter,
        50.0, 0.0, false));
    schema.definitions_.append(NumberDefinition(
        QStringLiteral("plungeHeight"), ProcessParameterUnit::Millimeter,
        5.0, 0.0, false));
    schema.definitions_.append(NumberDefinition(
        QStringLiteral("referenceHeight"), ProcessParameterUnit::Millimeter,
        0.0, 0.0, false));
    schema.definitions_.append(NumberDefinition(
        QStringLiteral("depth"), ProcessParameterUnit::Millimeter,
        1.0, 0.0, true));
    schema.definitions_.append(NumberDefinition(
        QStringLiteral("stepOver"), ProcessParameterUnit::Millimeter,
        1.0, 0.0, true));
    schema.definitions_.append(NumberDefinition(
        QStringLiteral("stepDown"), ProcessParameterUnit::Millimeter,
        1.0, 0.0, true));
    schema.definitions_.append(NumberDefinition(
        QStringLiteral("feedRate"), ProcessParameterUnit::MillimeterPerMinute,
        100.0, 0.0, true));
    schema.definitions_.append(NumberDefinition(
        QStringLiteral("plungeRate"), ProcessParameterUnit::MillimeterPerMinute,
        100.0, 0.0, true));
    schema.definitions_.append(NumberDefinition(
        QStringLiteral("spindleSpeed"), ProcessParameterUnit::RevolutionPerMinute,
        1000.0, 0.0, true));
    return schema;
}

void ProcessParameterSchema::addDefinition(
    const ProcessParameterDefinition &definition)
{
    definitions_.append(definition);
}

const QList<ProcessParameterDefinition> &ProcessParameterSchema::definitions() const
{
    return definitions_;
}

const ProcessParameterDefinition *ProcessParameterSchema::definition(
    const QString &id) const
{
    for (const ProcessParameterDefinition &definition : definitions_) {
        if (definition.id == id) {
            return &definition;
        }
    }
    return nullptr;
}

StrategyParams ProcessParameterSchema::defaultParams() const
{
    StrategyParams params;
    for (const ProcessParameterDefinition &definition : definitions_) {
        params.values.insert(definition.id, definition.defaultValue);
    }
    return params;
}

QStringList ProcessParameterSchema::validate(const StrategyParams &params) const
{
    QStringList errors;
    for (const ProcessParameterDefinition &definition : definitions_) {
        if (!params.values.contains(definition.id)) {
            if (definition.required) {
                AddError(&errors, QStringLiteral("Missing required parameter: %1")
                                      .arg(definition.id));
            }
            continue;
        }

        bool ok = false;
        const double value = params.values.value(definition.id).toDouble(&ok);
        if (!ok || !std::isfinite(value)) {
            AddError(&errors, QStringLiteral("Parameter %1 must be a finite number")
                                  .arg(definition.id));
            continue;
        }
        if (definition.hasMinimum && value <= definition.minimum) {
            AddError(&errors, QStringLiteral("Parameter %1 must be greater than %2")
                                  .arg(definition.id)
                                  .arg(definition.minimum));
        }
        if (definition.hasMaximum && value > definition.maximum) {
            AddError(&errors, QStringLiteral("Parameter %1 must not exceed %2")
                                  .arg(definition.id)
                                  .arg(definition.maximum));
        }
    }

    const double safe_height = params.get(QStringLiteral("safeHeight"));
    const double plunge_height = params.get(QStringLiteral("plungeHeight"));
    const double reference_height = params.get(QStringLiteral("referenceHeight"));
    const double depth = params.get(QStringLiteral("depth"));
    const double target_height = reference_height - depth;
    if (safe_height < plunge_height) {
        AddError(&errors, QStringLiteral("Safe height must not be below plunge height"));
    }
    if (plunge_height < target_height) {
        AddError(&errors, QStringLiteral("Plunge height must not be below target depth"));
    }
    return errors;
}

QString ProcessParameterSourceName(ProcessParameterSource source)
{
    switch (source) {
    case ProcessParameterSource::SystemDefault:
        return QStringLiteral("system_default");
    case ProcessParameterSource::MaterialTemplate:
        return QStringLiteral("material_template");
    case ProcessParameterSource::ToolTypeTemplate:
        return QStringLiteral("tool_type_template");
    case ProcessParameterSource::ModuleRule:
        return QStringLiteral("module_rule");
    case ProcessParameterSource::ManualOverride:
        return QStringLiteral("manual_override");
    }
    return QStringLiteral("system_default");
}

ProcessParameterSource ProcessParameterSourceFromName(const QString &name)
{
    if (name == QStringLiteral("material_template")) {
        return ProcessParameterSource::MaterialTemplate;
    }
    if (name == QStringLiteral("tool_type_template")) {
        return ProcessParameterSource::ToolTypeTemplate;
    }
    if (name == QStringLiteral("module_rule")) {
        return ProcessParameterSource::ModuleRule;
    }
    if (name == QStringLiteral("manual_override")) {
        return ProcessParameterSource::ManualOverride;
    }
    return ProcessParameterSource::SystemDefault;
}
