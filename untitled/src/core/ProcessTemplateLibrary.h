#pragma once

#include "ProcessParameterSchema.h"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>

struct ProcessTemplateDefinition {
    QString id;
    QString version;
    QString name;
    ProcessParameterSource source = ProcessParameterSource::SystemDefault;
    StrategyParams values;

    QString ref() const;
};

struct MachiningPlanStepDefinition {
    QString id;
    QString strategyId;
    QString toolExpression;
    QString startExpression;
    QString depthExpression;
    int layerIndex = 0;
    bool countersinkStep = false;
};

struct MachiningPlanDefinition {
    QString id;
    QString version;
    QString name;
    QString strategyId;
    int toolId = -1;
    QString startExpression;
    QString depthExpression;
    QStringList templateRefs;
    QString shape = QStringLiteral("any");
    QMap<QString, double> formulaVariables;
    QList<MachiningPlanStepDefinition> steps;

    QString ref() const;
};

class ProcessTemplateLibrary
{
public:
    bool add(const ProcessTemplateDefinition &definition, QString *error = nullptr);
    const QList<ProcessTemplateDefinition> &definitions() const;
    const ProcessTemplateDefinition *find(const QString &ref) const;
    bool addPlan(const MachiningPlanDefinition &plan, QString *error = nullptr);
    const QList<MachiningPlanDefinition> &plans() const;
    const MachiningPlanDefinition *findPlan(const QString &ref) const;

    QJsonObject toJson() const;
    static ProcessTemplateLibrary FromJson(const QJsonObject &object,
                                           QStringList *errors = nullptr);

private:
    QList<ProcessTemplateDefinition> definitions_;
    QList<MachiningPlanDefinition> plans_;
};
