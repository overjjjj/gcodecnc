#include "../src/services/ProgramGenerationService.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
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

    ToolpathResult generate(const ContourFeature &,
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
    operation.markToolpathValid();
    return operation;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    ToolpathResult safeToolpath{
        QStringLiteral("T1 M6\nS1200 M3\nM8\nG0 Z5\nG0 X10 Y20\n"
                       "G1 Z-5.000 F100\nG1 X15 Y20 F100\nG0 Z5"),
        true,
        QString(),
        1.0};
    safeToolpath.parametricProgram.routineName = QStringLiteral("TEST_LAYER");
    safeToolpath.parametricProgram.parameterNames =
        QStringList{QStringLiteral("DEPTH_Z")};
    safeToolpath.parametricProgram.prefixLines = QStringList{
        QStringLiteral("T1 M6"), QStringLiteral("S1200 M3"), QStringLiteral("M8"),
        QStringLiteral("G0 Z5"), QStringLiteral("G0 X10 Y20")};
    safeToolpath.parametricProgram.bodyTemplateLines =
        QStringList{QStringLiteral("G1 Z${DEPTH_Z} F100"),
                    QStringLiteral("G1 X15 Y20 F100")};
    safeToolpath.parametricProgram.suffixLines = QStringList{QStringLiteral("G0 Z5")};
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

    MachiningOperation valid =
        holeOperation(QStringLiteral("op-valid"), QStringLiteral("safe_hole"), 1);
    valid.markToolpathValid();
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
    if (!expect(success.snapshot.gcodeText.contains(
                    QStringLiteral("G1 Z-5.000 F100\nX15 Y20\nG0 Z5")),
                QStringLiteral("final program should omit only repeated modal words"))) {
        return 1;
    }

    MachiningOperation staleOperation = valid;
    staleOperation.id = QStringLiteral("op-stale");
    staleOperation.markToolpathStale(QStringLiteral("tool changed"));
    const ProgramGenerationResult staleFailure =
        service.generate({staleOperation}, postProcessor, options, snapshotOptions);
    if (!expect(!staleFailure.ok &&
                    staleFailure.errors.join(QLatin1Char('\n')).contains(
                        QStringLiteral("valid recalculated toolpath")),
                QStringLiteral("stale operations must not enter final generation"))) {
        return 1;
    }

    MachiningOperation disabledOperation = valid;
    disabledOperation.id = QStringLiteral("op-disabled");
    disabledOperation.enabled = false;
    const ProgramGenerationResult disabledFailure =
        service.generate({disabledOperation}, postProcessor, options, snapshotOptions);
    if (!expect(!disabledFailure.ok &&
                    disabledFailure.errors.join(QLatin1Char('\n')).contains(
                        QStringLiteral("disabled")),
                QStringLiteral("disabled operations must not enter final generation"))) {
        return 1;
    }

    MachiningOperation offsetOperation = valid;
    offsetOperation.id = QStringLiteral("op-g56");
    offsetOperation.params.set(QStringLiteral("workOffset"), 56.0);
    const ProgramGenerationResult offsetSuccess =
        service.generate({offsetOperation}, postProcessor, options, snapshotOptions);
    if (!expect(offsetSuccess.ok,
                QStringLiteral("a valid per-operation work offset should generate")) ||
        !expect(offsetSuccess.snapshot.gcodeText.contains(
                    QStringLiteral("[op:op-g56] ----\n; safe_hole | D6.000 Z-5.000 | T1\nG56")),
                QStringLiteral("each operation should emit its configured G54-G59 work offset"))) {
        return 1;
    }

    MachiningOperation invalidOffsetOperation = valid;
    invalidOffsetOperation.params.set(QStringLiteral("workOffset"), 60.0);
    const ProgramGenerationResult invalidOffset =
        service.generate({invalidOffsetOperation}, postProcessor, options, snapshotOptions);
    if (!expect(!invalidOffset.ok,
                QStringLiteral("an invalid per-operation work offset must be rejected")) ||
        !expect(!invalidOffset.errors.isEmpty() &&
                    invalidOffset.errors.first().contains(QStringLiteral("G54-G59")),
                QStringLiteral("the invalid work-offset error should identify the supported range"))) {
        return 1;
    }

    MachiningOperation incrementalDepthOperation = valid;
    incrementalDepthOperation.params.set(QStringLiteral("depthMode"), 1.0);
    const ProgramGenerationResult incrementalDepth =
        service.generate({incrementalDepthOperation}, postProcessor, options, snapshotOptions);
    if (!expect(!incrementalDepth.ok,
                QStringLiteral("incremental depth mode must be rejected until it is generated safely")) ||
        !expect(!incrementalDepth.errors.isEmpty() &&
                    incrementalDepth.errors.first().contains(QStringLiteral("depth mode")),
                QStringLiteral("the incremental-depth error should identify the unsupported mode"))) {
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
    if (!expect(success.snapshot.macroText.contains(QStringLiteral("O9001")) &&
                    success.snapshot.macroText.contains(QStringLiteral("M98 P9001")) &&
                    success.snapshot.macroText.contains(QStringLiteral("M99")),
                QStringLiteral("snapshot should retain CQ8 Macro-B-compatible routine output"))) {
        return 1;
    }

    MachiningOperation sideHole = valid;
    sideHole.id = QStringLiteral("op-side-hole");
    sideHole.holeFeature.region = FaceRegion::Side;
    const ProgramGenerationResult sideHoleFailure =
        service.generate({sideHole}, postProcessor, options, snapshotOptions);
    if (!expect(!sideHoleFailure.ok,
                QStringLiteral("side holes must not generate in the front-face Z workflow")) ||
        !expect(sideHoleFailure.errors.join('\n').contains(QStringLiteral("Side-face")),
                QStringLiteral("side-hole rejection should identify the required Setup change"))) {
        return 1;
    }

    MachiningOperation sideSlot = valid;
    sideSlot.id = QStringLiteral("op-side-slot");
    sideSlot.opType = OperationType::Contour;
    sideSlot.strategyId = QStringLiteral("mill_blind_slot");
    sideSlot.contourFeature.subType = QStringLiteral("blind_slot");
    sideSlot.contourFeature.length = 40.0;
    sideSlot.contourFeature.width = 8.0;
    sideSlot.contourFeature.depth = 5.0;
    sideSlot.contourFeature.region = FaceRegion::Side;
    const ProgramGenerationResult sideSlotFailure =
        service.generate({sideSlot}, postProcessor, options, snapshotOptions);
    if (!expect(!sideSlotFailure.ok,
                QStringLiteral("side slots must not generate in the front-face Z workflow")) ||
        !expect(sideSlotFailure.errors.join('\n').contains(QStringLiteral("Side-face slot")),
                QStringLiteral("side-slot rejection should identify the required Setup change"))) {
        return 1;
    }

    MachiningOperation mislabelledSideSlot = sideSlot;
    mislabelledSideSlot.id = QStringLiteral("op-mislabelled-side-slot");
    mislabelledSideSlot.contourFeature.region = FaceRegion::Front;
    mislabelledSideSlot.contourFeature.axis = QVector3D(1.0f, 0.0f, 0.0f);
    const ProgramGenerationResult mislabelledSideSlotFailure =
        service.generate({mislabelledSideSlot}, postProcessor, options, snapshotOptions);
    if (!expect(!mislabelledSideSlotFailure.ok,
                QStringLiteral("a horizontal slot axis must not generate in the Z workflow")) ||
        !expect(mislabelledSideSlotFailure.errors.join('\n').contains(QStringLiteral("slot axis")),
                QStringLiteral("slot-axis rejection should identify the required Setup change"))) {
        return 1;
    }

    Cq8PostProcessor cq8PostProcessor;
    const ProgramGenerationResult cq8Success =
        service.generate({valid}, cq8PostProcessor, options, snapshotOptions);
    if (!expect(cq8Success.ok,
                QStringLiteral("CQ8 Macro-B-compatible program should generate")) ||
        !expect(cq8Success.snapshot.gcodeText.contains(QStringLiteral("M98 P9001")) &&
                    !cq8Success.snapshot.gcodeText.contains(
                        QStringLiteral("G1 Z-5.000 F100\nG1 X15")),
                QStringLiteral("CQ8 main program should call the compact macro instead of expanding it")) ||
        !expect(!cq8Success.snapshot.expandedGcodeText.isEmpty() &&
                    cq8Success.snapshot.expandedGcodeText.contains(
                        QStringLiteral("G1 Z-5.000 F100\nX15 Y20")),
                QStringLiteral("CQ8 snapshot should retain expanded program for simulation and review")) ||
        !expect(cq8Success.snapshot.expandedLineCount ==
                    cq8Success.snapshot.expandedGcodeText.count(QLatin1Char('\n')) + 1,
                QStringLiteral("CQ8 snapshot should retain the expanded line count for compression review")) ||
        !expect(cq8Success.snapshot.packageFiles.size() == 2 &&
                    cq8Success.snapshot.packageFiles.at(1).kind == QStringLiteral("macro") &&
                    cq8Success.snapshot.packageFiles.at(1).content.contains(
                        QStringLiteral("O9001")),
                QStringLiteral("CQ8 export should package the main program and macro library separately"))) {
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
                       ";CNEXT_HOLE_CYCLE code=G76 rtp=5 rfp=0 sdis=2 x=10 y=20 z=-5 f=100\n"
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
        !expect(unsupportedCycleFailure.errors.join('\n').contains(QStringLiteral("G76")),
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

    ProgramGenerationService wrongToolPeckService(
        [peckStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == peckStrategy->id()
                ? peckStrategy
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.type = QStringLiteral("end_mill");
                tool.diameter = 6.0;
            }
            return tool;
        });
    const ProgramGenerationResult wrongToolPeckFailure =
        wrongToolPeckService.generate({firstHole},
                                      postProcessor,
                                      options,
                                      holeSnapshotOptions);
    if (!expect(!wrongToolPeckFailure.ok,
                QStringLiteral("a peck-drilling operation must reject a non-drill tool in the generation service")) ||
        !expect(wrongToolPeckFailure.errors.join('\n').contains(QStringLiteral("drill")),
                QStringLiteral("the generation-service tool rejection should name the required drill type")) ||
        !expect(wrongToolPeckFailure.snapshot.gcodeText.isEmpty(),
                QStringLiteral("a rejected tool type must not expose a partial G-code snapshot"))) {
        return 1;
    }

    ProgramGenerationService oversizedPeckService(
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
                tool.diameter = 8.0;
            }
            return tool;
        });
    const ProgramGenerationResult oversizedPeckFailure =
        oversizedPeckService.generate({firstHole},
                                      postProcessor,
                                      options,
                                      holeSnapshotOptions);
    if (!expect(!oversizedPeckFailure.ok,
                QStringLiteral("a peck-drilling operation must reject a drill larger than the target hole")) ||
        !expect(oversizedPeckFailure.errors.join('\n').contains(QStringLiteral("diameter")),
                QStringLiteral("the oversize-drill rejection should explain the diameter conflict")) ||
        !expect(oversizedPeckFailure.snapshot.gcodeText.isEmpty(),
                QStringLiteral("an oversize drill must not expose a partial G-code snapshot"))) {
        return 1;
    }

    const auto reamingStrategy = std::make_shared<FixedStrategy>(
        QStringLiteral("hole_reaming"), safeToolpath);
    ProgramGenerationService wrongDiameterReamingService(
        [reamingStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == reamingStrategy->id()
                ? reamingStrategy
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.type = QStringLiteral("reamer");
                tool.diameter = 5.0;
            }
            return tool;
        });
    MachiningOperation reamingHole = firstHole;
    reamingHole.id = QStringLiteral("op-wrong-reamer-diameter");
    reamingHole.strategyId = QStringLiteral("hole_reaming");
    const ProgramGenerationResult wrongDiameterReamingFailure =
        wrongDiameterReamingService.generate({reamingHole},
                                             postProcessor,
                                             options,
                                             holeSnapshotOptions);
    if (!expect(!wrongDiameterReamingFailure.ok,
                QStringLiteral("a reaming operation must reject a reamer with a different diameter from the target hole")) ||
        !expect(wrongDiameterReamingFailure.errors.join('\n').contains(QStringLiteral("diameter")),
                QStringLiteral("the reamer rejection should explain the diameter conflict")) ||
        !expect(wrongDiameterReamingFailure.snapshot.gcodeText.isEmpty(),
                QStringLiteral("a mismatched reamer must not expose a partial G-code snapshot"))) {
        return 1;
    }

    const auto tappingStrategy = std::make_shared<FixedStrategy>(
        QStringLiteral("hole_tapping"), safeToolpath);
    ProgramGenerationService wrongPitchTappingService(
        [tappingStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == tappingStrategy->id()
                ? tappingStrategy
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.type = QStringLiteral("tap");
                tool.diameter = 6.0;
                tool.pitch = 1.25;
            }
            return tool;
        });
    MachiningOperation tappingHole = firstHole;
    tappingHole.id = QStringLiteral("op-wrong-tap-pitch");
    tappingHole.strategyId = QStringLiteral("hole_tapping");
    tappingHole.holeFeature.pitch = 1.0;
    const ProgramGenerationResult wrongPitchTappingFailure =
        wrongPitchTappingService.generate({tappingHole},
                                          postProcessor,
                                          options,
                                          holeSnapshotOptions);
    if (!expect(!wrongPitchTappingFailure.ok,
                QStringLiteral("a tapping operation must reject a tap with a different pitch from the target thread")) ||
        !expect(wrongPitchTappingFailure.errors.join('\n').contains(QStringLiteral("pitch")),
                QStringLiteral("the tap rejection should explain the pitch conflict")) ||
        !expect(wrongPitchTappingFailure.snapshot.gcodeText.isEmpty(),
                QStringLiteral("a mismatched tap must not expose a partial G-code snapshot"))) {
        return 1;
    }

    ProgramGenerationService shortFluteService(
        [safeStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == safeStrategy->id() ? safeStrategy : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.diameter = 6.0;
                tool.fluteLen = 4.0;
            }
            return tool;
        });
    const ProgramGenerationResult shortFluteFailure =
        shortFluteService.generate({valid}, postProcessor, options, snapshotOptions);
    if (!expect(!shortFluteFailure.ok,
                QStringLiteral("a hole operation must reject a tool whose flute length is shorter than the cutting depth")) ||
        !expect(shortFluteFailure.errors.join('\n').contains(QStringLiteral("flute length")),
                QStringLiteral("the flute-length rejection should explain the depth conflict")) ||
        !expect(shortFluteFailure.snapshot.gcodeText.isEmpty(),
                QStringLiteral("a short flute must not expose a partial G-code snapshot"))) {
        return 1;
    }

    ProgramGenerationService shortTotalLengthService(
        [safeStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == safeStrategy->id() ? safeStrategy : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.diameter = 6.0;
                tool.totalLen = 5.0;
            }
            return tool;
        });
    const ProgramGenerationResult shortTotalLengthFailure =
        shortTotalLengthService.generate({valid}, postProcessor, options, snapshotOptions);
    if (!expect(!shortTotalLengthFailure.ok,
                QStringLiteral("a hole operation must reject a tool whose total length cannot provide safe stick-out")) ||
        !expect(shortTotalLengthFailure.errors.join('\n').contains(QStringLiteral("total length")),
                QStringLiteral("the total-length rejection should explain the depth conflict")) ||
        !expect(shortTotalLengthFailure.snapshot.gcodeText.isEmpty(),
                QStringLiteral("an insufficient total length must not expose a partial G-code snapshot"))) {
        return 1;
    }

    const auto blindSlotStrategy = std::make_shared<FixedStrategy>(
        QStringLiteral("mill_blind_slot"), safeToolpath);
    ProgramGenerationService oversizedSlotToolService(
        [blindSlotStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == blindSlotStrategy->id()
                ? blindSlotStrategy
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.type = QStringLiteral("end_mill");
                tool.diameter = 8.0;
            }
            return tool;
        });
    MachiningOperation oversizedSlot = sideSlot;
    oversizedSlot.id = QStringLiteral("op-oversized-slot-tool");
    oversizedSlot.contourFeature.region = FaceRegion::Front;
    oversizedSlot.contourFeature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    const ProgramGenerationResult oversizedSlotToolFailure =
        oversizedSlotToolService.generate({oversizedSlot},
                                          postProcessor,
                                          options,
                                          snapshotOptions);
    if (!expect(!oversizedSlotToolFailure.ok,
                QStringLiteral("a slot operation must reject a tool that is not narrower than the slot")) ||
        !expect(oversizedSlotToolFailure.errors.join('\n').contains(QStringLiteral("diameter")),
                QStringLiteral("the slot rejection should explain the width and diameter conflict")) ||
        !expect(oversizedSlotToolFailure.snapshot.gcodeText.isEmpty(),
                QStringLiteral("an oversized slot tool must not expose a partial G-code snapshot"))) {
        return 1;
    }

    ProgramGenerationService shortFluteSlotService(
        [blindSlotStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == blindSlotStrategy->id()
                ? blindSlotStrategy
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.type = QStringLiteral("end_mill");
                tool.diameter = 6.0;
                tool.fluteLen = 4.0;
            }
            return tool;
        });
    const ProgramGenerationResult shortFluteSlotFailure =
        shortFluteSlotService.generate({oversizedSlot},
                                       postProcessor,
                                       options,
                                       snapshotOptions);
    if (!expect(!shortFluteSlotFailure.ok,
                QStringLiteral("a slot operation must reject a tool whose flute length is shorter than the slot depth")) ||
        !expect(shortFluteSlotFailure.errors.join('\n').contains(QStringLiteral("flute length")),
                QStringLiteral("the slot flute-length rejection should explain the depth conflict")) ||
        !expect(shortFluteSlotFailure.snapshot.gcodeText.isEmpty(),
                QStringLiteral("a short slot flute must not expose a partial G-code snapshot"))) {
        return 1;
    }

    ProgramGenerationService shortTotalLengthSlotService(
        [blindSlotStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == blindSlotStrategy->id()
                ? blindSlotStrategy
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.type = QStringLiteral("end_mill");
                tool.diameter = 6.0;
                tool.fluteLen = 8.0;
                tool.totalLen = 5.0;
            }
            return tool;
        });
    const ProgramGenerationResult shortTotalLengthSlotFailure =
        shortTotalLengthSlotService.generate({oversizedSlot},
                                              postProcessor,
                                              options,
                                              snapshotOptions);
    if (!expect(!shortTotalLengthSlotFailure.ok,
                QStringLiteral("a slot operation must reject a tool whose total length reaches slot depth")) ||
        !expect(shortTotalLengthSlotFailure.errors.join('\n').contains(QStringLiteral("total length")),
                QStringLiteral("the slot total-length rejection should explain the depth conflict")) ||
        !expect(shortTotalLengthSlotFailure.snapshot.gcodeText.isEmpty(),
                QStringLiteral("an insufficient slot total length must not expose a partial G-code snapshot"))) {
        return 1;
    }

    const auto pocketStrategy = std::make_shared<FixedStrategy>(
        QStringLiteral("mill_pocket_rough"), safeToolpath);
    ProgramGenerationService oversizedPocketToolService(
        [pocketStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == pocketStrategy->id()
                ? pocketStrategy
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.type = QStringLiteral("end_mill");
                tool.diameter = 10.0;
            }
            return tool;
        });
    MachiningOperation oversizedPocket = oversizedSlot;
    oversizedPocket.id = QStringLiteral("op-oversized-pocket-tool");
    oversizedPocket.strategyId = QStringLiteral("mill_pocket_rough");
    oversizedPocket.contourFeature.subType = QStringLiteral("rectangular_pocket");
    oversizedPocket.contourFeature.length = 20.0;
    oversizedPocket.contourFeature.width = 10.0;
    const ProgramGenerationResult oversizedPocketToolFailure =
        oversizedPocketToolService.generate({oversizedPocket},
                                            postProcessor,
                                            options,
                                            snapshotOptions);
    if (!expect(!oversizedPocketToolFailure.ok,
                QStringLiteral("a pocket operation must reject a tool that is not smaller than its limiting dimension")) ||
        !expect(oversizedPocketToolFailure.errors.join('\n').contains(QStringLiteral("diameter")),
                QStringLiteral("the pocket rejection should explain the diameter conflict")) ||
        !expect(oversizedPocketToolFailure.snapshot.gcodeText.isEmpty(),
                QStringLiteral("an oversized pocket tool must not expose a partial G-code snapshot"))) {
        return 1;
    }

    MachiningOperation sidePocket = oversizedPocket;
    sidePocket.id = QStringLiteral("op-side-pocket");
    sidePocket.contourFeature.region = FaceRegion::Side;
    const ProgramGenerationResult sidePocketFailure =
        oversizedPocketToolService.generate({sidePocket},
                                            postProcessor,
                                            options,
                                            snapshotOptions);
    if (!expect(!sidePocketFailure.ok,
                QStringLiteral("a side-face pocket must not generate in the front-face Z workflow")) ||
        !expect(sidePocketFailure.errors.join('\n').contains(QStringLiteral("Side-face pocket")),
                QStringLiteral("side-pocket rejection should identify the required Setup change"))) {
        return 1;
    }

    const auto faceStrategy = std::make_shared<FixedStrategy>(
        QStringLiteral("mill_face"), safeToolpath);
    ProgramGenerationService sideFaceMillingService(
        [faceStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == faceStrategy->id()
                ? faceStrategy
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.type = QStringLiteral("face_mill");
                tool.diameter = 40.0;
            }
            return tool;
        });
    MachiningOperation sideFaceMilling = oversizedPocket;
    sideFaceMilling.id = QStringLiteral("op-side-face-milling");
    sideFaceMilling.strategyId = QStringLiteral("mill_face");
    sideFaceMilling.contourFeature.region = FaceRegion::Side;
    sideFaceMilling.contourFeature.axis = QVector3D(1.0f, 0.0f, 0.0f);
    const ProgramGenerationResult sideFaceMillingFailure =
        sideFaceMillingService.generate({sideFaceMilling},
                                        postProcessor,
                                        options,
                                        snapshotOptions);
    if (!expect(!sideFaceMillingFailure.ok,
                QStringLiteral("side-face milling must not generate in the front-face Z workflow")) ||
        !expect(sideFaceMillingFailure.errors.join('\n').contains(QStringLiteral("transformed Setup")),
                QStringLiteral("side-face milling rejection should identify the required Setup change")) ||
        !expect(sideFaceMillingFailure.snapshot.gcodeText.isEmpty(),
                QStringLiteral("side-face milling must not expose a partial G-code snapshot"))) {
        return 1;
    }

    ProgramGenerationService shortFlutePocketService(
        [pocketStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == pocketStrategy->id()
                ? pocketStrategy
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.type = QStringLiteral("end_mill");
                tool.diameter = 6.0;
                tool.fluteLen = 4.0;
            }
            return tool;
        });
    const ProgramGenerationResult shortFlutePocketFailure =
        shortFlutePocketService.generate({oversizedPocket},
                                         postProcessor,
                                         options,
                                         snapshotOptions);
    if (!expect(!shortFlutePocketFailure.ok,
                QStringLiteral("a pocket operation must reject a tool whose flute length is shorter than pocket depth")) ||
        !expect(shortFlutePocketFailure.errors.join('\n').contains(QStringLiteral("flute length")),
                QStringLiteral("the pocket flute-length rejection should explain the depth conflict"))) {
        return 1;
    }

    ProgramGenerationService shortTotalLengthPocketService(
        [pocketStrategy](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == pocketStrategy->id()
                ? pocketStrategy
                : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 1) {
                tool.id = 1;
                tool.type = QStringLiteral("end_mill");
                tool.diameter = 6.0;
                tool.fluteLen = 8.0;
                tool.totalLen = 5.0;
            }
            return tool;
        });
    const ProgramGenerationResult shortTotalLengthPocketFailure =
        shortTotalLengthPocketService.generate({oversizedPocket},
                                                postProcessor,
                                                options,
                                                snapshotOptions);
    if (!expect(!shortTotalLengthPocketFailure.ok,
                QStringLiteral("a pocket operation must reject a tool whose total length reaches pocket depth")) ||
        !expect(shortTotalLengthPocketFailure.errors.join('\n').contains(QStringLiteral("total length")),
                QStringLiteral("the pocket total-length rejection should explain the depth conflict")) ||
        !expect(shortTotalLengthPocketFailure.snapshot.gcodeText.isEmpty(),
                QStringLiteral("an insufficient pocket total length must not expose a partial G-code snapshot"))) {
        return 1;
    }

    MachiningOperation mismatchedHole = secondHole;
    mismatchedHole.id = QStringLiteral("op-hole-different-depth");
    mismatchedHole.holeFeature.depth = 8.0;
    const ProgramGenerationResult splitHoles =
        peckService.generate({firstHole, mismatchedHole},
                             postProcessor,
                             options,
                             holeSnapshotOptions);
    if (!expect(splitHoles.ok,
                QStringLiteral("different hole groups should still generate as separate operations")) ||
        !expect(splitHoles.snapshot.gcodeText.contains(
                    QStringLiteral("---- Operation 1 [op:op-hole-1] ----")) &&
                    splitHoles.snapshot.gcodeText.contains(
                    QStringLiteral("---- Operation 2 [op:op-hole-different-depth] ----")),
                QStringLiteral("different hole geometry must not share one batch trace block"))) {
        return 1;
    }

    MachiningOperation steppedHole = secondHole;
    steppedHole.id = QStringLiteral("op-hole-different-entry");
    steppedHole.holeFeature.center.setZ(10.0f);
    const ProgramGenerationResult steppedHoles =
        peckService.generate({firstHole, steppedHole},
                             postProcessor,
                             options,
                             holeSnapshotOptions);
    if (!expect(steppedHoles.ok,
                QStringLiteral("holes on different entry planes should generate as separate operations")) ||
        !expect(steppedHoles.snapshot.gcodeText.contains(
                    QStringLiteral("---- Operation 1 [op:op-hole-1] ----")) &&
                    steppedHoles.snapshot.gcodeText.contains(
                    QStringLiteral("---- Operation 2 [op:op-hole-different-entry] ----")),
                QStringLiteral("holes with different entry Z must not share one batch trace block"))) {
        return 1;
    }

    return 0;
}
