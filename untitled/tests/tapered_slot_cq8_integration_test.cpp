#include "../src/workspaces/process_design/operations/SlotOperationState.h"
#include "../src/app/MillingBatchCommit.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/postprocessor/FanucPostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/strategies/mill/TaperedSlotMillingStrategy.h"
#include <QCoreApplication>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto strategy = std::make_shared<TaperedSlotMillingStrategy>();
    ToolEntry tool; tool.id = 6; tool.type = "end_mill"; tool.diameter = 6;
    tool.fluteLen = 20; tool.totalLen = 60;
    ProgramGenerationService generator([=](const QString &) { return strategy; }, [=](int) { return tool; }, [](const ContourFeature &candidate) {
        ContourFeature source; source.subType = "straight_slot";
        source.length = 40; source.width = 12; source.depth = 4;
        source.axis = QVector3D(0, 0, 1); source.region = FaceRegion::Front;
        for (double angle : {0., 45., 90.}) {
            source.angle = angle;
            if (sameSlotGeometry(candidate, source)) return true;
        }
        source.angle = 0; source.slopeMinWidth = .4; source.slopeMaxWidth = .6;
        return sameSlotGeometry(candidate, source);
    });
    MachiningOperation operation;
    operation.id = "linear-floor"; operation.opType = OperationType::Contour;
    operation.strategyId = strategy->id(); operation.toolId = tool.id; operation.markToolpathValid();
    operation.params = strategy->defaultParams(); operation.params.set("depth", 4);
    for (const auto &key : {"slotLength", "slotWidth", "angle"}) operation.params.values.remove(key);
    auto &feature = operation.contourFeature;
    feature.subType = "straight_slot"; feature.length = 40; feature.width = 12; feature.depth = 4;
    feature.axis = QVector3D(0, 0, 1); feature.region = FaceRegion::Front;
    auto widthProfile = operation;
    widthProfile.id = "width-profile";
    widthProfile.contourFeature.slopeMinWidth = .4;
    widthProfile.contourFeature.slopeMaxWidth = .6;
    const auto widthPath = strategy->generate(widthProfile.contourFeature, tool, widthProfile.params);
    if (!widthPath.ok || widthPath.gcode.isEmpty() || !widthPath.gcode.contains("mode=width-profile")) {
        std::cerr << "existing width-profile path must remain available\n"; return 1;
    }
    int failures = 0;
    PostProcessorOptions options;
    options.safeStartBlocks = QStringList{"G17 G40 G49 G80", "G21", "G90", "G54", "G94"};
    Cq8PostProcessor cq8; FanucPostProcessor fanuc; SiemensPostProcessor siemens;
    for (double direction : {-1., 1.}) for (double angle : {0., 45., 90.}) {
        operation.params.set("slopeDirection", direction); feature.angle = angle;
        const auto raw = strategy->generate(feature, tool, operation.params);
        if (raw.ok || !raw.gcode.isEmpty() || !raw.errorMsg.contains("cutter", Qt::CaseInsensitive)) {
            ++failures; std::cerr << "direct linear floor must reject with cutter-envelope explanation: " << direction << ',' << angle << '\n';
        }
        int commits = 0;
        const auto batch = validateAndCommitMillingBatch({widthProfile, operation}, *strategy, tool,
            [&](const QList<MachiningOperation> &) { ++commits; return QStringList{"width-profile", "linear-floor"}; });
        if (batch.ok || commits || !batch.operationIds.isEmpty()) {
            ++failures; std::cerr << "linear floor in manual batch must cause zero commits\n";
        }
        for (PostProcessorBase *post : {static_cast<PostProcessorBase *>(&cq8),
                                        static_cast<PostProcessorBase *>(&fanuc), static_cast<PostProcessorBase *>(&siemens)}) {
            ProgramGenerationSnapshotOptions snapshot; snapshot.machineProfile.id = post->id();
            const auto supported = generator.generate({widthProfile}, *post, options, snapshot);
            if (!supported.ok) { ++failures; std::cerr << "width profile final output must remain supported: " << supported.errors.join(";").toStdString() << '\n'; }
            const auto result = generator.generate({widthProfile, operation}, *post, options, snapshot);
            if (result.ok || !result.snapshot.gcodeText.isEmpty() || !result.snapshot.packageFiles.isEmpty() || !result.errors.join(";").contains("cutter", Qt::CaseInsensitive)) {
                ++failures; std::cerr << post->id().toStdString() << " must not emit a partial program for a prohibited linear floor\n";
            }
        }
    }
    std::cout << "linear-floor boundary failures=" << failures << '\n';
    return failures ? 1 : 0;
}
