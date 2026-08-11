#include "../src/services/ProgramGenerationService.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/strategies/hole/PeckDrillingStrategy.h"

#include <QCoreApplication>
#include <QDebug>

#include <iostream>
#include <memory>
#include <utility>

namespace {

class FixedStrategy final : public StrategyBase
{
public:
    FixedStrategy(QString strategyId, ToolpathResult result)
        : m_id(std::move(strategyId)), m_result(std::move(result))
    {
    }

    QString id() const override { return m_id; }
    QString displayName() const override { return m_id; }
    StrategyParams defaultParams() const override { return {}; }

    ToolpathResult generate(const HoleFeature &,
                            const ToolEntry &,
                            const StrategyParams &) const override
    {
        return m_result;
    }

    ToolpathResult generate(const QVector<HoleFeature> &,
                            const ToolEntry &,
                            const StrategyParams &) const override
    {
        return m_result;
    }

private:
    QString m_id;
    ToolpathResult m_result;
};

bool expect(bool condition, const QString &message)
{
    if (!condition) {
        std::cerr << message.toStdString() << '\n';
    }
    return condition;
}

MachiningOperation holeOperation(const QString &id,
                                 const QString &strategyId,
                                 int toolId)
{
    MachiningOperation operation;
    operation.id = id;
    operation.opType = OperationType::Hole;
    operation.strategyId = strategyId;
    operation.toolId = toolId;
    operation.holeFeature.center = QVector3D(10.0f, 20.0f, 0.0f);
    operation.holeFeature.radius = 3.0;
    operation.holeFeature.depth = 5.0;
    return operation;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    ToolpathResult safeToolpath{
        QStringLiteral("T1 M6\nS1200 M3\nM8\nG0 Z5\nG0 X10 Y20\nG1 Z-5 F100\nG0 Z5"),
        true,
        QString(),
        1.0};
    safeToolpath.parametricProgram.routineName = QStringLiteral("TEST_LAYER");
    safeToolpath.parametricProgram.parameterNames =
        QStringList{QStringLiteral("DEPTH_Z")};
    safeToolpath.parametricProgram.bodyTemplateLines =
        QStringList{QStringLiteral("G1 Z${DEPTH_Z} F100")};
    ParametricToolpathCall safeCall;
    safeCall.arguments.insert(QStringLiteral("DEPTH_Z"), QStringLiteral("-5.000"));
    safeToolpath.parametricProgram.calls = {safeCall};
    const auto safeStrategy =
        std::make_shared<FixedStrategy>(QStringLiteral("safe_hole"), safeToolpath);

    ProgramGenerationService service(
        [safeStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == safeStrategy->id() ? safeStrategy : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.diameter = 6.0;
            }
            return tool;
        });

    SiemensPostProcessor postProcessor;
    PostProcessorOptions options;
    options.programNumber = QStringLiteral("O1234");
    options.workOffset = QStringLiteral("G54");
    options.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"),
        QStringLiteral("G21"),
        QStringLiteral("G90"),
        QStringLiteral("G54"),
        QStringLiteral("G94")};

    ProgramGenerationSnapshotOptions snapshotOptions;
    snapshotOptions.name = QStringLiteral("Batch Program 1");
    snapshotOptions.sourceSummary = QStringLiteral("batch");
    snapshotOptions.mainProgramName = QStringLiteral("CNEXT batch 1");
    snapshotOptions.machineProfile.id = QStringLiteral("profile-test");
    snapshotOptions.machineProfile.version = QStringLiteral("7");
    snapshotOptions.machineProfile.safeStartBlocks = options.safeStartBlocks;

    const MachiningOperation valid =
        holeOperation(QStringLiteral("op-valid"), QStringLiteral("safe_hole"), 1);
    const ProgramGenerationResult success =
        service.generate({valid}, postProcessor, options, snapshotOptions);

    if (!expect(success.ok, QStringLiteral("a valid confirmed operation should generate")) ||
        !expect(success.errors.isEmpty(), QStringLiteral("successful generation should have no errors")) ||
        !expect(!success.snapshot.id.isEmpty() && success.snapshot.createdAt.isValid(),
                QStringLiteral("successful generation should create an identifiable snapshot")) ||
        !expect(success.snapshot.name == snapshotOptions.name &&
                    success.snapshot.sourceSummary == snapshotOptions.sourceSummary,
                QStringLiteral("snapshot metadata should come from the generation request")) ||
        !expect(success.snapshot.gcodeText.contains(QStringLiteral("O1234")),
                QStringLiteral("the requested postprocessor should wrap the program")) ||
        !expect(success.snapshot.gcodeText.contains(QStringLiteral("[op:op-valid]")),
                QStringLiteral("final G-code should retain the operation trace marker")) ||
        !expect(success.snapshot.sourceOperationIds ==
                    QStringList{QStringLiteral("op-valid")} &&
                    !success.snapshot.sourceOperationFingerprint.isEmpty(),
                QStringLiteral("the snapshot should fingerprint every source operation")) ||
        !expect(success.snapshot.postProcessorId == QStringLiteral("siemens") &&
                    success.snapshot.machineProfileId == QStringLiteral("profile-test") &&
                    success.snapshot.machineProfileVersion == QStringLiteral("7") &&
                    success.snapshot.safeStartBlocks == snapshotOptions.machineProfile.safeStartBlocks,
                QStringLiteral("the snapshot should retain the controller and machine profile")) ||
        !expect(success.snapshot.mainProgramFileName ==
                    QStringLiteral("CNEXT_BATCH_1.MPF") &&
                    success.snapshot.packageFiles.size() == 1,
                QStringLiteral("Siemens output should create a deterministic main MPF")) ||
        !expect(success.snapshot.packageFiles.first().kind == QStringLiteral("main") &&
                    success.snapshot.packageFiles.first().fileName ==
                        success.snapshot.mainProgramFileName &&
                    success.snapshot.packageFiles.first().content ==
                        success.snapshot.gcodeText &&
                    success.snapshot.packageFiles.first().sha256.size() == 64,
                QStringLiteral("the stored MPF must exactly match the validated final G-code"))) {
        return 1;
    }
    if (!expect(success.snapshot.parametricPrograms.size() == 1 &&
                    success.snapshot.parametricPrograms.first().sourceOperationIds ==
                        QStringList{QStringLiteral("op-valid")} &&
                    success.snapshot.parametricPrograms.first().routineName ==
                        QStringLiteral("TEST_LAYER"),
                QStringLiteral("snapshot should retain routine metadata with operation traceability")) ||
        !expect(success.snapshot.packageFiles.size() == 1,
                QStringLiteral("routine metadata must not become a machine package file"))) {
        return 1;
    }

    const MachiningOperation invalidTool =
        holeOperation(QStringLiteral("op-invalid"), QStringLiteral("safe_hole"), 99);
    const ProgramGenerationResult atomicFailure =
        service.generate({valid, invalidTool}, postProcessor, options, snapshotOptions);

    if (!expect(!atomicFailure.ok,
                QStringLiteral("one invalid operation should fail the whole program")) ||
        !expect(atomicFailure.snapshot.gcodeText.isEmpty() &&
                    atomicFailure.snapshot.packageFiles.isEmpty(),
                QStringLiteral("an atomic failure must not expose a partial snapshot")) ||
        !expect(!atomicFailure.errors.isEmpty(),
                QStringLiteral("an atomic failure should explain the rejected operation")) ||
        !expect(atomicFailure.snapshot.sourceOperationIds.isEmpty(),
                QStringLiteral("a failed program must not claim source operations"))) {
        return 1;
    }

    const ToolpathResult unsupportedCycleToolpath{
        QStringLiteral("T1 M6\nS1200 M3\nM8\nG0 Z5\n"
                       ";CNEXT_HOLE_CYCLE code=G86 rtp=5 rfp=0 sdis=2 x=10 y=20 z=-5 f=100\n"
                       "G0 Z5"),
        true,
        QString(),
        1.0};
    const auto unsupportedCycleStrategy = std::make_shared<FixedStrategy>(
        QStringLiteral("unsupported_cycle"), unsupportedCycleToolpath);
    ProgramGenerationService unsupportedCycleService(
        [unsupportedCycleStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == unsupportedCycleStrategy->id()
                ? unsupportedCycleStrategy
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.diameter = 6.0;
            }
            return tool;
        });
    const MachiningOperation unsupportedCycleOperation =
        holeOperation(QStringLiteral("op-unsupported-cycle"),
                      QStringLiteral("unsupported_cycle"),
                      1);
    const ProgramGenerationResult unsupportedCycleFailure =
        unsupportedCycleService.generate({unsupportedCycleOperation},
                                         postProcessor,
                                         options,
                                         snapshotOptions);
    if (!expect(!unsupportedCycleFailure.ok,
                QStringLiteral("unsupported internal hole cycle must fail atomically")) ||
        !expect(unsupportedCycleFailure.snapshot.gcodeText.isEmpty(),
                QStringLiteral("unsupported cycle must not expose a partial snapshot")) ||
        !expect(unsupportedCycleFailure.errors.join('\n').contains(QStringLiteral("G86")),
                QStringLiteral("unsupported cycle error should name the rejected code"))) {
        return 1;
    }

    MachiningOperation unconfirmed = valid;
    unconfirmed.id.clear();
    const ProgramGenerationResult unconfirmedFailure =
        service.generate({unconfirmed}, postProcessor, options, snapshotOptions);
    if (!expect(!unconfirmedFailure.ok,
                QStringLiteral("an operation without a confirmed ID must be rejected")) ||
        !expect(unconfirmedFailure.snapshot.gcodeText.isEmpty(),
                QStringLiteral("an unconfirmed operation must not expose a snapshot"))) {
        return 1;
    }

    const auto peckStrategy = std::make_shared<PeckDrillingStrategy>();
    ProgramGenerationService peckService(
        [peckStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == peckStrategy->id()
                ? peckStrategy
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.type = QStringLiteral("drill");
                tool.diameter = 6.0;
            }
            return tool;
        });

    MachiningOperation firstHole =
        holeOperation(QStringLiteral("op-hole-1"), QStringLiteral("hole_peck"), 1);
    firstHole.params = peckStrategy->defaultParams();
    MachiningOperation secondHole = firstHole;
    secondHole.id = QStringLiteral("op-hole-2");
    secondHole.holeFeature.center = QVector3D(30.0f, 40.0f, 0.0f);

    ProgramGenerationSnapshotOptions holeSnapshotOptions = snapshotOptions;
    holeSnapshotOptions.name = QStringLiteral("Hole Batch 1");
    holeSnapshotOptions.mainProgramName = QStringLiteral("HOLE BATCH 1");
    const ProgramGenerationResult repeatedHoles =
        peckService.generate({firstHole, secondHole},
                             postProcessor,
                             options,
                             holeSnapshotOptions);

    if (!expect(repeatedHoles.ok,
                QStringLiteral("two compatible peck holes should generate as one batch")) ||
        !expect(repeatedHoles.snapshot.gcodeText.count(
                    QStringLiteral("MCALL CYCLE83(")) == 1,
                QStringLiteral("compatible holes should share one Siemens cycle definition")) ||
        !expect(repeatedHoles.snapshot.gcodeText.contains(
                    QStringLiteral("X10.000 Y20.000")) &&
                    repeatedHoles.snapshot.gcodeText.contains(
                        QStringLiteral("X30.000 Y40.000")),
                QStringLiteral("the shared cycle should call both hole positions")) ||
        !expect(repeatedHoles.snapshot.gcodeText.contains(
                    QStringLiteral("[op:op-hole-1]")) &&
                    repeatedHoles.snapshot.gcodeText.contains(
                        QStringLiteral("[op:op-hole-2]")),
                QStringLiteral("final G-code should trace every operation in the batch")) ||
        !expect(repeatedHoles.snapshot.packageFiles.size() == 1 &&
                    repeatedHoles.snapshot.packageFiles.first().content ==
                        repeatedHoles.snapshot.gcodeText,
                QStringLiteral("the validated cycle program should remain the exported main MPF"))) {
        return 1;
    }

    return 0;
}
