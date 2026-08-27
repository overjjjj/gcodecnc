#include "../src/services/OperationFactory.h"

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

    OperationProposal proposal;
    proposal.kind = OperationProposalKind::Hole;
    proposal.strategyId = QStringLiteral("hole_spot");
    proposal.toolId = 7;
    proposal.holeFeature.radius = 3.0;
    proposal.holeFeature.depth = 10.0;

    OperationParameterLayers layers;
    layers.systemDefaults.set(QStringLiteral("safeHeight"), 40.0);
    layers.materialTemplate.set(QStringLiteral("feedRate"), 100.0);
    layers.materialTemplate.set(QStringLiteral("plungeRate"), 110.0);
    layers.toolTypeTemplate.set(QStringLiteral("feedRate"), 200.0);
    layers.toolTypeTemplate.set(QStringLiteral("plungeRate"), 210.0);
    layers.moduleRule.set(QStringLiteral("feedRate"), 300.0);
    layers.moduleRule.set(QStringLiteral("spindleSpeed"), 3300.0);
    layers.manualOverrides.set(QStringLiteral("feedRate"), 400.0);
    layers.manualOverrides.set(QStringLiteral("leadLength"), 5.0);
    layers.templateId = QStringLiteral("aluminum_endmill");
    layers.templateVersion = QStringLiteral("3");

    const OperationFactoryResult created = OperationFactory::CreateConfirmed(proposal, layers);
    if (!expect(created.ok, "a confirmed valid proposal should create an operation") ||
        !expect(created.operation.params.get(QStringLiteral("feedRate")) == 400.0,
                "manual overrides should have highest priority") ||
        !expect(created.operation.parameterSources.value(QStringLiteral("feedRate")) ==
                    ProcessParameterSource::ManualOverride,
                "the winning source should be persisted with the operation") ||
        !expect(created.operation.params.get(QStringLiteral("safeHeight")) == 40.0 &&
                    created.operation.parameterSources.value(QStringLiteral("safeHeight")) ==
                        ProcessParameterSource::SystemDefault,
                "strategy defaults should remain system defaults") ||
        !expect(created.operation.params.get(QStringLiteral("plungeRate")) == 210.0 &&
                    created.operation.parameterSources.value(QStringLiteral("plungeRate")) ==
                        ProcessParameterSource::ToolTypeTemplate,
                "tool type templates should override material templates") ||
        !expect(created.operation.params.get(QStringLiteral("spindleSpeed")) == 3300.0 &&
                    created.operation.parameterSources.value(QStringLiteral("spindleSpeed")) ==
                        ProcessParameterSource::ModuleRule,
                "module rules should override lower-priority templates") ||
        !expect(created.operation.parameterTemplate.id == QStringLiteral("aluminum_endmill") &&
                    created.operation.parameterTemplate.version == QStringLiteral("3"),
                "the applied template identity should be retained") ||
        !expect(created.operation.parameterTemplate.values.get(QStringLiteral("feedRate")) == 300.0,
                "the template snapshot should exclude manual overrides")) {
        return 1;
    }

    MachiningOperation restored = created.operation;
    OperationFactory::RestoreTemplateValues(&restored);
    if (!expect(restored.params.get(QStringLiteral("feedRate")) == 300.0,
                "restoring should replace manual values with the applied template value") ||
        !expect(restored.parameterSources.value(QStringLiteral("feedRate")) ==
                    ProcessParameterSource::ModuleRule,
                "restoring should recover the template parameter source") ||
        !expect(restored.params.get(QStringLiteral("leadLength")) == 5.0,
                "restoring common template values should preserve strategy-only values")) {
        return 1;
    }

    layers.manualOverrides.set(QStringLiteral("stepDown"), 0.0);
    const OperationFactoryResult rejected = OperationFactory::CreateConfirmed(proposal, layers);
    if (!expect(!rejected.ok, "unsafe shared parameters must block operation creation")) {
        return 1;
    }

    return 0;
}
