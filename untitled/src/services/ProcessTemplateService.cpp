#include "ProcessTemplateService.h"

namespace {

void MergeValues(const StrategyParams &source, StrategyParams *destination)
{
    for (auto it = source.values.cbegin(); it != source.values.cend(); ++it) {
        destination->values.insert(it.key(), it.value());
    }
}

} // namespace

ProcessTemplateResolution ProcessTemplateService::Resolve(
    const ProcessTemplateLibrary &library,
    const ProcessTemplateSelection &selection)
{
    ProcessTemplateResolution result;
    QStringList versions;
    for (const QString &ref : selection.templateRefs) {
        const ProcessTemplateDefinition *definition = library.find(ref);
        if (!definition) {
            result.errors.append(QStringLiteral("Unknown template: %1").arg(ref));
            continue;
        }
        switch (definition->source) {
        case ProcessParameterSource::SystemDefault:
            MergeValues(definition->values, &result.layers.systemDefaults);
            break;
        case ProcessParameterSource::MaterialTemplate:
            MergeValues(definition->values, &result.layers.materialTemplate);
            break;
        case ProcessParameterSource::ToolTypeTemplate:
            MergeValues(definition->values, &result.layers.toolTypeTemplate);
            break;
        case ProcessParameterSource::ModuleRule:
            MergeValues(definition->values, &result.layers.moduleRule);
            break;
        case ProcessParameterSource::ManualOverride:
            result.errors.append(QStringLiteral("Manual overrides are not templates."));
            break;
        }
        versions.append(definition->version);
    }
    result.layers.templateId = selection.templateRefs.join(QLatin1Char('+'));
    result.layers.templateVersion = versions.join(QLatin1Char('+'));
    result.layers.manualOverrides = selection.manualOverrides;
    result.ok = result.errors.isEmpty();
    return result;
}
