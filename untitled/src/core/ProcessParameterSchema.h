#pragma once

#include "ProcessParameters.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

enum class ProcessParameterType {
    Number,
    Integer,
    Boolean,
    Text,
    Choice
};

enum class ProcessParameterUnit {
    None,
    Millimeter,
    MillimeterPerMinute,
    RevolutionPerMinute
};

enum class ProcessParameterSource {
    SystemDefault,
    MaterialTemplate,
    ToolTypeTemplate,
    ModuleRule,
    ManualOverride
};

struct ProcessParameterDefinition {
    QString id;
    ProcessParameterType type = ProcessParameterType::Number;
    ProcessParameterUnit unit = ProcessParameterUnit::None;
    QVariant defaultValue;
    ProcessParameterSource defaultSource = ProcessParameterSource::SystemDefault;
    double minimum = 0.0;
    double maximum = 0.0;
    bool hasMinimum = false;
    bool hasMaximum = false;
    bool visible = true;
    bool required = false;
};

class ProcessParameterSchema
{
public:
    static ProcessParameterSchema CommonOperation();

    void addDefinition(const ProcessParameterDefinition &definition);
    const QList<ProcessParameterDefinition> &definitions() const;
    const ProcessParameterDefinition *definition(const QString &id) const;
    StrategyParams defaultParams() const;
    QStringList validate(const StrategyParams &params) const;

private:
    QList<ProcessParameterDefinition> definitions_;
};

QString ProcessParameterSourceName(ProcessParameterSource source);
ProcessParameterSource ProcessParameterSourceFromName(const QString &name);
