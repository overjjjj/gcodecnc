#include "../src/core/ProcessTemplateLibrary.h"
#include "../src/services/OperationFactory.h"
#include "../src/services/ProcessTemplateService.h"

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

ProcessTemplateDefinition makeTemplate(const QString &id,
                                       const QString &version,
                                       ProcessParameterSource source,
                                       const QString &parameter,
                                       double value)
{
    ProcessTemplateDefinition definition;
    definition.id = id;
    definition.version = version;
    definition.name = id;
    definition.source = source;
    definition.values.set(parameter, value);
    return definition;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    ProcessTemplateLibrary library;
    QString error;
    const QList<ProcessTemplateDefinition> definitions{
        makeTemplate(QStringLiteral("common-hole"), QStringLiteral("1"),
                     ProcessParameterSource::SystemDefault,
                     QStringLiteral("safeHeight"), 30.0),
        makeTemplate(QStringLiteral("aluminum"), QStringLiteral("1"),
                     ProcessParameterSource::MaterialTemplate,
                     QStringLiteral("feedRate"), 100.0),
        makeTemplate(QStringLiteral("drill"), QStringLiteral("1"),
                     ProcessParameterSource::ToolTypeTemplate,
                     QStringLiteral("feedRate"), 200.0),
        makeTemplate(QStringLiteral("auto-hole"), QStringLiteral("1"),
                     ProcessParameterSource::ModuleRule,
                     QStringLiteral("feedRate"), 300.0)
    };
    for (const ProcessTemplateDefinition &definition : definitions) {
        if (!expect(library.add(definition, &error),
                    "valid versioned definitions should be accepted")) {
            return 1;
        }
    }
    MachiningPlanDefinition plan;
    plan.id = QStringLiteral("blind-drill");
    plan.version = QStringLiteral("1");
    plan.name = QStringLiteral("Blind-hole peck drilling");
    plan.strategyId = QStringLiteral("hole_peck");
    plan.toolId = 7;
    plan.startExpression = QStringLiteral("AT");
    plan.depthExpression = QStringLiteral("H1-AT");
    plan.shape = QStringLiteral("countersink");
    plan.formulaVariables = {{QStringLiteral("T1"), 7.0},
                             {QStringLiteral("T2"), 8.0}};
    plan.steps = {
        {QStringLiteral("core"), QStringLiteral("hole_peck"),
         QStringLiteral("T1"), QStringLiteral("0"), QStringLiteral("H1+H2"), 1, false},
        {QStringLiteral("countersink"), QStringLiteral("hole_chamfer"),
         QStringLiteral("T2"), QStringLiteral("0"), QStringLiteral("H1"), 0, true}};
    plan.templateRefs = QStringList{
        QStringLiteral("common-hole@1"), QStringLiteral("aluminum@1"),
        QStringLiteral("drill@1"), QStringLiteral("auto-hole@1")};
    if (!expect(library.addPlan(plan, &error),
                "versioned machining plans should be accepted")) {
        return 1;
    }
    if (!expect(!library.add(definitions.first(), &error),
                "an existing template version must be immutable")) {
        return 1;
    }

    QStringList loadErrors;
    const ProcessTemplateLibrary restored = ProcessTemplateLibrary::FromJson(
        library.toJson(), &loadErrors);
    if (!expect(loadErrors.isEmpty() && restored.definitions().size() == 4 &&
                    restored.plans().size() == 1 &&
                    restored.plans().first().shape == QStringLiteral("countersink") &&
                    restored.plans().first().formulaVariables.value(
                        QStringLiteral("T2")) == 8.0 &&
                    restored.plans().first().steps.size() == 2 &&
                    restored.findPlan(QStringLiteral("blind-drill@1")) != nullptr,
                "template and machining-plan versions should survive persistence")) {
        return 1;
    }

    ProcessTemplateSelection selection;
    selection.templateRefs = QStringList{
        QStringLiteral("common-hole@1"), QStringLiteral("aluminum@1"),
        QStringLiteral("drill@1"), QStringLiteral("auto-hole@1")};
    selection.manualOverrides.set(QStringLiteral("feedRate"), 400.0);
    const ProcessTemplateResolution resolved =
        ProcessTemplateService::Resolve(restored, selection);
    if (!expect(resolved.ok &&
                    resolved.layers.systemDefaults.get(QStringLiteral("safeHeight")) == 30.0 &&
                    resolved.layers.materialTemplate.get(QStringLiteral("feedRate")) == 100.0 &&
                    resolved.layers.toolTypeTemplate.get(QStringLiteral("feedRate")) == 200.0 &&
                    resolved.layers.moduleRule.get(QStringLiteral("feedRate")) == 300.0,
                "explicit template application should preserve all five source layers")) {
        return 1;
    }

    OperationProposal proposal;
    proposal.kind = OperationProposalKind::Hole;
    proposal.strategyId = QStringLiteral("hole_spot");
    proposal.toolId = 7;
    proposal.holeFeature.radius = 3.0;
    proposal.holeFeature.depth = 10.0;
    const OperationFactoryResult confirmed =
        OperationFactory::CreateConfirmed(proposal, resolved.layers);
    if (!expect(confirmed.ok &&
                    confirmed.operation.params.get(QStringLiteral("feedRate")) == 400.0 &&
                    confirmed.operation.parameterTemplate.id.contains(QStringLiteral("auto-hole@1")),
                "resolved templates should create an immutable operation snapshot")) {
        return 1;
    }

    ProcessTemplateDefinition version2 = definitions.at(3);
    version2.version = QStringLiteral("2");
    version2.values.set(QStringLiteral("feedRate"), 900.0);
    if (!expect(library.add(version2, &error) &&
                    confirmed.operation.params.get(QStringLiteral("feedRate")) == 400.0 &&
                    confirmed.operation.parameterTemplate.values.get(
                        QStringLiteral("feedRate")) == 300.0,
                "new template versions must not mutate confirmed operations")) {
        return 1;
    }

    MachiningOperation recovered = confirmed.operation;
    OperationFactory::RestoreTemplateValues(&recovered);
    if (!expect(recovered.params.get(QStringLiteral("feedRate")) == 300.0,
                "explicit restore should use the snapshotted template values")) {
        return 1;
    }

    return 0;
}
