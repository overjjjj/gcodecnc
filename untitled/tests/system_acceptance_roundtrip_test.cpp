#include "../src/core/OperationWorkflow.h"
#include "../src/core/ProjectManager.h"
#include "../src/gcode/GCodeSafetyValidator.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/services/OperationFactory.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/hole/PeckDrillingStrategy.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <memory>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    OperationProposal proposal;
    proposal.kind = OperationProposalKind::Hole;
    proposal.strategyId = QStringLiteral("hole_peck");
    proposal.toolId = 7;
    proposal.holeFeature.kind = FeatureKind::Hole;
    proposal.holeFeature.subType = QStringLiteral("blind_hole");
    proposal.holeFeature.center = QVector3D(10.0f, 20.0f, 0.0f);
    proposal.holeFeature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    proposal.holeFeature.radius = 3.0;
    proposal.holeFeature.depth = 12.0;
    proposal.holeFeature.region = FaceRegion::Front;

    OperationParameterLayers layers;
    layers.templateId = QStringLiteral("aluminum-drill");
    layers.templateVersion = QStringLiteral("1");
    layers.materialTemplate.set(QStringLiteral("safeHeight"), 20.0);
    layers.materialTemplate.set(QStringLiteral("plungeHeight"), 2.0);
    layers.materialTemplate.set(QStringLiteral("feedRate"), 90.0);
    layers.toolTypeTemplate.set(QStringLiteral("spindleSpeed"), 1200.0);
    layers.moduleRule.set(QStringLiteral("depth"), 12.0);
    layers.moduleRule.set(QStringLiteral("stepDown"), 3.0);
    layers.manualOverrides.set(QStringLiteral("feedRate"), 95.0);

    ProcessContext context;
    context.setup.workOffset = QStringLiteral("G54");
    context.setup.origin.confirmed = true;
    context.toolId = 7;
    context.materialId = QStringLiteral("aluminum");
    context.toolTypeId = QStringLiteral("drill");

    OperationFactoryResult created =
        OperationFactory::CreateConfirmed(proposal, layers, context);
    if (!expect(created.ok && created.operation.geometryRefs.size() == 1 &&
                    created.operation.geometryRefs.first().startsWith(
                        QStringLiteral("feature:")),
                "explicit confirmation must retain one stable GeometryRef") ||
        !expect(created.operation.parameterSources.value(
                    QStringLiteral("feedRate")) ==
                    ProcessParameterSource::ManualOverride &&
                    created.operation.parameterTemplate.id == layers.templateId,
                "formal operation must retain source priority and template snapshot")) {
        return 1;
    }
    created.operation.markToolpathValid();

    MachiningOperation disabled = created.operation;
    disabled.id = QStringLiteral("disabled-copy");
    disabled.enabled = false;
    MachiningOperation stale = created.operation;
    stale.id = QStringLiteral("stale-copy");
    stale.markToolpathStale(QStringLiteral("tool changed"));
    const QList<MachiningOperation> executable = executableOperations(
        {disabled, stale, created.operation});
    if (!expect(executable.size() == 1 &&
                    executable.first().id == created.operation.id,
                "only enabled valid operations may enter final generation")) {
        return 1;
    }

    const auto strategy = std::make_shared<PeckDrillingStrategy>();
    ProgramGenerationService generator(
        [strategy](const QString &id) {
            return id == strategy->id()
                ? std::static_pointer_cast<StrategyBase>(strategy)
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            tool.id = id;
            tool.type = QStringLiteral("drill");
            tool.diameter = 6.0;
            tool.fluteLen = 30.0;
            tool.totalLen = 60.0;
            return tool;
        });
    PostProcessorOptions options;
    options.programNumber = QStringLiteral("O2701");
    options.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
        QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    ProgramGenerationSnapshotOptions snapshotOptions;
    snapshotOptions.name = QStringLiteral("Module 7 system acceptance");
    snapshotOptions.machineProfile.id = QStringLiteral("cq8");
    snapshotOptions.machineProfile.version = QStringLiteral("1");
    snapshotOptions.machineProfile.safeStartBlocks = options.safeStartBlocks;
    const ProgramGenerationResult generated = generator.generate(
        executable, Cq8PostProcessor(), options, snapshotOptions);
    if (!expect(generated.ok && generated.snapshot.sourceOperationIds ==
                    QStringList{created.operation.id},
                "final CQ8 snapshot must trace only the executable operation") ||
        !expect(GCodeSafetyValidator::validate(
                    generated.snapshot.gcodeText).ok,
                "final CQ8 output must pass the safety boundary")) {
        return 1;
    }

    SimulationController simulation;
    QVector<QVector3D> path;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&path](const QVector<QVector3D> &points,
                             const QVector<bool> &) { path = points; });
    simulation.loadGCode(generated.snapshot.expandedGcodeText.isEmpty()
                             ? generated.snapshot.gcodeText
                             : generated.snapshot.expandedGcodeText);
    if (!expect(path.size() > 2,
                "execution preview must be derived from final postprocessed output")) {
        return 1;
    }

    QTemporaryDir temporaryDirectory;
    if (!expect(temporaryDirectory.isValid(),
                "temporary project directory must be available")) {
        return 1;
    }
    ProjectManager project;
    SetupOrigin origin;
    origin.confirmed = true;
    project.setSetupOrigin(origin);
    StockDefinition stock;
    stock.confirmed = true;
    project.setStockDefinition(stock);
    project.setOperations({disabled, stale, created.operation});
    project.upsertProgram(generated.snapshot);

    ProcessTemplateLibrary library;
    ProcessTemplateDefinition revisedTemplate;
    revisedTemplate.id = QStringLiteral("aluminum-drill");
    revisedTemplate.version = QStringLiteral("2");
    revisedTemplate.name = QStringLiteral("Revised aluminum drill");
    revisedTemplate.source = ProcessParameterSource::MaterialTemplate;
    revisedTemplate.values.set(QStringLiteral("feedRate"), 130.0);
    QString libraryError;
    library.add(revisedTemplate, &libraryError);
    project.setProcessTemplateLibrary(library);
    if (!expect(project.operations().last().params.get(
                    QStringLiteral("feedRate")) == 95.0,
                "template changes must not rewrite confirmed operation snapshots")) {
        return 1;
    }

    const QString projectPath = temporaryDirectory.filePath(
        QStringLiteral("module7.cnext"));
    if (!expect(project.saveToFile(projectPath),
                "system acceptance project must save")) {
        return 1;
    }
    ProjectManager reopened;
    if (!expect(reopened.loadFromFile(projectPath),
                "system acceptance project must reopen") ||
        !expect(reopened.operations().size() == 3 &&
                    reopened.operations().last().toolpathState ==
                        ToolpathState::Valid,
                "operation ordering, enable state, and toolpath state must round-trip") ||
        !expect(reopened.programs().size() == 1 &&
                    reopened.programs().first().sourceOperationIds ==
                        QStringList{created.operation.id} &&
                    reopened.programs().first().gcodeText ==
                        generated.snapshot.gcodeText,
                "final program traceability must round-trip")) {
        return 1;
    }

    reopened.setWorkOffset(QStringLiteral("G55"));
    for (const MachiningOperation &operation : reopened.operations()) {
        if (!expect(operation.toolpathState == ToolpathState::Stale,
                    "WCS changes must invalidate every saved operation")) {
            return 1;
        }
    }
    return 0;
}
