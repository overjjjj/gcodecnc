#include "ProcessTemplateLibrary.h"

#include <QJsonArray>
#include <QJsonValue>

QString ProcessTemplateDefinition::ref() const
{
    return id + QLatin1Char('@') + version;
}

QString MachiningPlanDefinition::ref() const
{
    return id + QLatin1Char('@') + version;
}

bool ProcessTemplateLibrary::add(const ProcessTemplateDefinition &definition,
                                 QString *error)
{
    if (definition.id.trimmed().isEmpty() ||
        definition.version.trimmed().isEmpty() ||
        definition.values.values.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Template id, version and values are required.");
        }
        return false;
    }
    if (definition.source == ProcessParameterSource::ManualOverride) {
        if (error) {
            *error = QStringLiteral("Manual overrides cannot be stored as templates.");
        }
        return false;
    }
    if (find(definition.ref())) {
        if (error) {
            *error = QStringLiteral("Template versions are immutable: %1")
                         .arg(definition.ref());
        }
        return false;
    }
    definitions_.append(definition);
    return true;
}

const QList<ProcessTemplateDefinition> &ProcessTemplateLibrary::definitions() const
{
    return definitions_;
}

const ProcessTemplateDefinition *ProcessTemplateLibrary::find(const QString &ref) const
{
    for (const ProcessTemplateDefinition &definition : definitions_) {
        if (definition.ref() == ref) {
            return &definition;
        }
    }
    return nullptr;
}

bool ProcessTemplateLibrary::addPlan(const MachiningPlanDefinition &plan,
                                     QString *error)
{
    if (plan.id.trimmed().isEmpty() || plan.version.trimmed().isEmpty() ||
        plan.strategyId.trimmed().isEmpty() || plan.toolId <= 0 ||
        plan.startExpression.trimmed().isEmpty() ||
        plan.depthExpression.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Plan id, version, strategy, tool and formulas are required.");
        }
        return false;
    }
    if (findPlan(plan.ref())) {
        if (error) {
            *error = QStringLiteral("Machining-plan versions are immutable: %1")
                         .arg(plan.ref());
        }
        return false;
    }
    plans_.append(plan);
    return true;
}

const QList<MachiningPlanDefinition> &ProcessTemplateLibrary::plans() const
{
    return plans_;
}

const MachiningPlanDefinition *ProcessTemplateLibrary::findPlan(
    const QString &ref) const
{
    for (const MachiningPlanDefinition &plan : plans_) {
        if (plan.ref() == ref) {
            return &plan;
        }
    }
    return nullptr;
}

QJsonObject ProcessTemplateLibrary::toJson() const
{
    QJsonArray definitions;
    for (const ProcessTemplateDefinition &definition : definitions_) {
        QJsonObject object;
        object[QStringLiteral("id")] = definition.id;
        object[QStringLiteral("version")] = definition.version;
        object[QStringLiteral("name")] = definition.name;
        object[QStringLiteral("source")] =
            ProcessParameterSourceName(definition.source);
        QJsonObject values;
        for (auto it = definition.values.values.cbegin();
             it != definition.values.values.cend(); ++it) {
            values[it.key()] = QJsonValue::fromVariant(it.value());
        }
        object[QStringLiteral("values")] = values;
        definitions.append(object);
    }
    QJsonArray plans;
    for (const MachiningPlanDefinition &plan : plans_) {
        QJsonObject object;
        object[QStringLiteral("id")] = plan.id;
        object[QStringLiteral("version")] = plan.version;
        object[QStringLiteral("name")] = plan.name;
        object[QStringLiteral("strategyId")] = plan.strategyId;
        object[QStringLiteral("toolId")] = plan.toolId;
        object[QStringLiteral("startExpression")] = plan.startExpression;
        object[QStringLiteral("depthExpression")] = plan.depthExpression;
        object[QStringLiteral("shape")] = plan.shape;
        QJsonObject formulaVariables;
        for (auto it = plan.formulaVariables.cbegin();
             it != plan.formulaVariables.cend(); ++it) {
            formulaVariables[it.key()] = it.value();
        }
        object[QStringLiteral("formulaVariables")] = formulaVariables;
        QJsonArray steps;
        for (const MachiningPlanStepDefinition &step : plan.steps) {
            QJsonObject stepObject;
            stepObject[QStringLiteral("id")] = step.id;
            stepObject[QStringLiteral("strategyId")] = step.strategyId;
            stepObject[QStringLiteral("toolExpression")] = step.toolExpression;
            stepObject[QStringLiteral("startExpression")] = step.startExpression;
            stepObject[QStringLiteral("depthExpression")] = step.depthExpression;
            stepObject[QStringLiteral("layerIndex")] = step.layerIndex;
            stepObject[QStringLiteral("countersinkStep")] = step.countersinkStep;
            steps.append(stepObject);
        }
        object[QStringLiteral("steps")] = steps;
        QJsonArray templateRefs;
        for (const QString &ref : plan.templateRefs) {
            templateRefs.append(ref);
        }
        object[QStringLiteral("templateRefs")] = templateRefs;
        plans.append(object);
    }
    return QJsonObject{
        {QStringLiteral("definitions"), definitions},
        {QStringLiteral("plans"), plans}
    };
}

ProcessTemplateLibrary ProcessTemplateLibrary::FromJson(
    const QJsonObject &object,
    QStringList *errors)
{
    ProcessTemplateLibrary library;
    const QJsonArray definitions = object.value(
        QStringLiteral("definitions")).toArray();
    for (const QJsonValue &value : definitions) {
        const QJsonObject stored = value.toObject();
        ProcessTemplateDefinition definition;
        definition.id = stored.value(QStringLiteral("id")).toString();
        definition.version = stored.value(QStringLiteral("version")).toString();
        definition.name = stored.value(QStringLiteral("name")).toString();
        definition.source = ProcessParameterSourceFromName(
            stored.value(QStringLiteral("source")).toString());
        const QJsonObject values = stored.value(QStringLiteral("values")).toObject();
        for (auto it = values.begin(); it != values.end(); ++it) {
            definition.values.values.insert(it.key(), it.value().toVariant());
        }
        QString error;
        if (!library.add(definition, &error) && errors) {
            errors->append(error);
        }
    }
    const QJsonArray plans = object.value(QStringLiteral("plans")).toArray();
    for (const QJsonValue &value : plans) {
        const QJsonObject stored = value.toObject();
        MachiningPlanDefinition plan;
        plan.id = stored.value(QStringLiteral("id")).toString();
        plan.version = stored.value(QStringLiteral("version")).toString();
        plan.name = stored.value(QStringLiteral("name")).toString();
        plan.strategyId = stored.value(QStringLiteral("strategyId")).toString();
        plan.toolId = stored.value(QStringLiteral("toolId")).toInt(-1);
        plan.startExpression = stored.value(
            QStringLiteral("startExpression")).toString();
        plan.depthExpression = stored.value(
            QStringLiteral("depthExpression")).toString();
        plan.shape = stored.value(QStringLiteral("shape")).toString(
            QStringLiteral("any"));
        const QJsonObject formulaVariables = stored.value(
            QStringLiteral("formulaVariables")).toObject();
        for (auto it = formulaVariables.constBegin(); it != formulaVariables.constEnd(); ++it) {
            plan.formulaVariables.insert(it.key(), it.value().toDouble());
        }
        for (const QJsonValue &stepValue : stored.value(
                 QStringLiteral("steps")).toArray()) {
            const QJsonObject stepObject = stepValue.toObject();
            MachiningPlanStepDefinition step;
            step.id = stepObject.value(QStringLiteral("id")).toString();
            step.strategyId = stepObject.value(QStringLiteral("strategyId")).toString();
            step.toolExpression = stepObject.value(
                QStringLiteral("toolExpression")).toString();
            step.startExpression = stepObject.value(
                QStringLiteral("startExpression")).toString();
            step.depthExpression = stepObject.value(
                QStringLiteral("depthExpression")).toString();
            step.layerIndex = stepObject.value(QStringLiteral("layerIndex")).toInt();
            step.countersinkStep = stepObject.value(
                QStringLiteral("countersinkStep")).toBool();
            plan.steps.append(step);
        }
        for (const QJsonValue &ref : stored.value(
                 QStringLiteral("templateRefs")).toArray()) {
            plan.templateRefs.append(ref.toString());
        }
        QString error;
        if (!library.addPlan(plan, &error) && errors) {
            errors->append(error);
        }
    }
    return library;
}
