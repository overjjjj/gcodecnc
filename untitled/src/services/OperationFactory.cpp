#include "OperationFactory.h"

namespace {

void ApplyLayer(const StrategyParams &layer,
                ProcessParameterSource source,
                StrategyParams *params,
                QMap<QString, ProcessParameterSource> *sources)
{
    for (auto it = layer.values.cbegin(); it != layer.values.cend(); ++it) {
        params->values.insert(it.key(), it.value());
        sources->insert(it.key(), source);
    }
}

} // namespace

OperationFactoryResult OperationFactory::CreateConfirmed(
    const OperationProposal &proposal,
    const OperationParameterLayers &layers)
{
    OperationFactoryResult result;
    const ProcessParameterSchema schema = ProcessParameterSchema::CommonOperation();
    StrategyParams merged = schema.defaultParams();
    QMap<QString, ProcessParameterSource> sources;
    for (auto it = merged.values.cbegin(); it != merged.values.cend(); ++it) {
        sources.insert(it.key(), ProcessParameterSource::SystemDefault);
    }

    ApplyLayer(layers.systemDefaults, ProcessParameterSource::SystemDefault,
               &merged, &sources);
    ApplyLayer(layers.materialTemplate, ProcessParameterSource::MaterialTemplate,
               &merged, &sources);
    ApplyLayer(layers.toolTypeTemplate, ProcessParameterSource::ToolTypeTemplate,
               &merged, &sources);
    ApplyLayer(layers.moduleRule, ProcessParameterSource::ModuleRule,
               &merged, &sources);
    const StrategyParams template_values = merged;
    const QMap<QString, ProcessParameterSource> template_sources = sources;
    ApplyLayer(proposal.params, ProcessParameterSource::ManualOverride,
               &merged, &sources);
    ApplyLayer(layers.manualOverrides, ProcessParameterSource::ManualOverride,
               &merged, &sources);

    result.errors = schema.validate(merged);
    if (!result.errors.isEmpty()) {
        return result;
    }

    OperationProposal resolved_proposal = proposal;
    resolved_proposal.params = merged;
    const OperationConfirmationResult confirmation = confirmOperationProposal(
        resolved_proposal, OperationConfirmationIntent::ExplicitUser);
    if (!confirmation.ok) {
        result.errors.append(confirmation.error);
        return result;
    }

    result.ok = true;
    result.operation = confirmation.operation;
    result.operation.parameterSources = sources;
    result.operation.parameterTemplate.id = layers.templateId;
    result.operation.parameterTemplate.version = layers.templateVersion;
    result.operation.parameterTemplate.values = template_values;
    result.operation.parameterTemplate.sources = template_sources;
    return result;
}

OperationFactoryResult OperationFactory::CreateConfirmed(
    const OperationProposal &proposal,
    const OperationParameterLayers &layers,
    const ProcessContext &context)
{
    OperationFactoryResult result;
    result.errors = context.validate();
    if (!result.errors.isEmpty()) {
        return result;
    }

    OperationProposal resolved_proposal = proposal;
    resolved_proposal.toolId = context.toolId;
    OperationParameterLayers resolved_layers = layers;
    resolved_layers.manualOverrides.set(
        QStringLiteral("workOffset"), context.setup.workOffsetNumber());
    return CreateConfirmed(resolved_proposal, resolved_layers);
}

void OperationFactory::RestoreTemplateValues(MachiningOperation *operation)
{
    if (!operation) {
        return;
    }
    for (auto it = operation->parameterTemplate.values.values.cbegin();
         it != operation->parameterTemplate.values.values.cend(); ++it) {
        operation->params.values.insert(it.key(), it.value());
        operation->parameterSources.insert(
            it.key(), operation->parameterTemplate.sources.value(
                          it.key(), ProcessParameterSource::SystemDefault));
    }
}
