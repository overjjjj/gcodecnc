#include "../src/gcode/GCodeSafetyValidator.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/postprocessor/FanucPostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/services/SlotFramePlanningService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/mill/PocketRoughingStrategy.h"
#include "../src/strategies/mill/SlotMillingStrategy.h"

#include <QCoreApplication>

#include <cmath>
#include <iostream>
#include <memory>

namespace {

bool Expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

ProcessContext Context()
{
    ProcessContext context;
    context.setup.workOffset = QStringLiteral("G54");
    context.setup.origin.confirmed = true;
    context.toolId = 5;
    context.toolTypeId = QStringLiteral("end_mill");
    context.materialId = QStringLiteral("aluminum");
    return context;
}

OperationParameterLayers Layers()
{
    OperationParameterLayers layers;
    layers.moduleRule.set(QStringLiteral("safeHeight"), 20.0);
    layers.moduleRule.set(QStringLiteral("plungeHeight"), 3.0);
    layers.moduleRule.set(QStringLiteral("referenceHeight"), 0.0);
    layers.moduleRule.set(QStringLiteral("depth"), 6.0);
    layers.moduleRule.set(QStringLiteral("stepOver"), 3.0);
    layers.moduleRule.set(QStringLiteral("stepDown"), 2.0);
    layers.moduleRule.set(QStringLiteral("feedRate"), 500.0);
    layers.moduleRule.set(QStringLiteral("plungeRate"), 150.0);
    layers.moduleRule.set(QStringLiteral("spindleSpeed"), 1800.0);
    layers.moduleRule.set(QStringLiteral("stockToLeave"), 0.5);
    layers.moduleRule.set(QStringLiteral("entryMode"), 0.0);
    return layers;
}

SlotFrameFeature ClosedFeature()
{
    SlotFrameFeature feature;
    feature.geometryRef = QStringLiteral("slot-frame:integration-pocket");
    feature.setup = Context().setup;
    feature.kind = SlotFrameKind::ClosedPocket;
    feature.closed = true;
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.region = FaceRegion::Front;
    feature.depth = 6.0;
    feature.outerBoundary = {
        QVector3D(0.0f, 0.0f, 0.0f), QVector3D(60.0f, 0.0f, 0.0f),
        QVector3D(60.0f, 40.0f, 0.0f), QVector3D(0.0f, 40.0f, 0.0f)};
    feature.protectionRegions = {{
        QVector3D(25.0f, 15.0f, 0.0f), QVector3D(35.0f, 15.0f, 0.0f),
        QVector3D(35.0f, 25.0f, 0.0f), QVector3D(25.0f, 25.0f, 0.0f)}};
    feature.machiningGeometry.subType = QStringLiteral("irregular_pocket");
    feature.machiningGeometry.center = QVector3D(30.0f, 20.0f, 0.0f);
    feature.machiningGeometry.depth = feature.depth;
    feature.machiningGeometry.axis = feature.axis;
    feature.machiningGeometry.region = feature.region;
    feature.machiningGeometry.points = feature.outerBoundary;
    return feature;
}

SlotFrameFeature OpenFeature()
{
    SlotFrameFeature feature;
    feature.geometryRef = QStringLiteral("slot-frame:integration-open-slot");
    feature.setup = Context().setup;
    feature.kind = SlotFrameKind::OpenSlot;
    feature.closed = false;
    feature.complementType = SlotFrameComplementType::DirectBridge;
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.region = FaceRegion::Front;
    feature.depth = 6.0;
    feature.outerBoundary = {
        QVector3D(-20.0f, -6.0f, 0.0f), QVector3D(20.0f, -6.0f, 0.0f),
        QVector3D(20.0f, 6.0f, 0.0f), QVector3D(-20.0f, 6.0f, 0.0f)};
    feature.freeEdges = {
        {QVector3D(-20.0f, -6.0f, 0.0f), QVector3D(-20.0f, 0.0f, 0.0f), 1},
        {QVector3D(-20.0f, 0.0f, 0.0f), QVector3D(-20.0f, 6.0f, 0.0f), 1}};
    feature.machiningGeometry.subType = QStringLiteral("open_slot");
    feature.machiningGeometry.center = QVector3D();
    feature.machiningGeometry.length = 40.0;
    feature.machiningGeometry.width = 12.0;
    feature.machiningGeometry.depth = feature.depth;
    feature.machiningGeometry.openSide = -1.0;
    feature.machiningGeometry.axis = feature.axis;
    feature.machiningGeometry.region = feature.region;
    feature.machiningGeometry.points = feature.outerBoundary;
    return feature;
}

ProgramGenerationResult Generate(
    MachiningOperation operation,
    const PostProcessorBase &post_processor,
    const QString &program_number)
{
    operation.markToolpathValid();
    const auto pocket = std::make_shared<PocketRoughingStrategy>();
    const auto slot = std::make_shared<SlotMillingStrategy>();
    ProgramGenerationService generator(
        [pocket, slot](const QString &id) -> std::shared_ptr<StrategyBase> {
            if (id == pocket->id()) {
                return pocket;
            }
            return id == slot->id() ? slot : std::shared_ptr<StrategyBase>();
        },
        [](int id) {
            ToolEntry tool;
            if (id == 5) {
                tool.id = 5;
                tool.type = QStringLiteral("end_mill");
                tool.diameter = 6.0;
                tool.fluteLen = 20.0;
                tool.totalLen = 60.0;
            }
            return tool;
        });
    PostProcessorOptions options;
    options.programNumber = program_number;
    options.workOffset = QStringLiteral("G54");
    options.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
        QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    ProgramGenerationSnapshotOptions snapshot;
    snapshot.name = QStringLiteral("slot-frame-integration");
    snapshot.machineProfile.id = QStringLiteral("verified-3axis");
    return generator.generate({operation}, post_processor, options, snapshot);
}

bool SimulatesSafely(const QString &gcode, double target_depth)
{
    SimulationController simulation;
    QVector<QVector3D> path;
    QVector<bool> rapid;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&path, &rapid](const QVector<QVector3D> &new_path,
                                     const QVector<bool> &new_rapid) {
        path = new_path;
        rapid = new_rapid;
    });
    simulation.loadGCode(gcode);
    bool reaches_bottom = false;
    for (int index = 0; index < rapid.size(); ++index) {
        const QVector3D &from = path.at(index);
        const QVector3D &to = path.at(index + 1);
        if (rapid.at(index) &&
            (std::abs(from.x() - to.x()) > 1.0e-3f ||
             std::abs(from.y() - to.y()) > 1.0e-3f) &&
            std::min(from.z(), to.z()) < 3.0f - 1.0e-3f) {
            return false;
        }
        reaches_bottom = reaches_bottom ||
            (!rapid.at(index) && std::abs(to.z() + target_depth) < 1.0e-3f);
    }
    return reaches_bottom;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    SlotFrameToolInfo tool;
    tool.id = 5;
    tool.type = QStringLiteral("end_mill");
    tool.diameter = 6.0;
    tool.fluteLength = 20.0;
    tool.totalLength = 60.0;
    const QMap<int, SlotFrameToolInfo> tools{{5, tool}};

    const SlotFrameConfirmationResult closed = SlotFramePlanningService::Confirm(
        ClosedFeature(), tools, Context(), Layers());
    const SlotFrameConfirmationResult opened = SlotFramePlanningService::Confirm(
        OpenFeature(), tools, Context(), Layers());
    if (!Expect(closed.ok && opened.ok,
                "recognized closed and open candidates should confirm atomically")) {
        return 1;
    }

    const Cq8PostProcessor cq8;
    const FanucPostProcessor fanuc;
    const SiemensPostProcessor siemens;
    const ProgramGenerationResult closed_cq8 = Generate(
        closed.operations.first(), cq8, QStringLiteral("O3101"));
    const ProgramGenerationResult open_cq8 = Generate(
        opened.operations.first(), cq8, QStringLiteral("O3102"));
    const ProgramGenerationResult closed_fanuc = Generate(
        closed.operations.first(), fanuc, QStringLiteral("O3103"));
    const ProgramGenerationResult closed_siemens = Generate(
        closed.operations.first(), siemens, QStringLiteral("POCKET_FRAME"));
    const ProgramGenerationResult open_siemens = Generate(
        opened.operations.first(), siemens, QStringLiteral("SLOT_FRAME"));
    if (!Expect(closed_cq8.ok && open_cq8.ok &&
                    closed_fanuc.ok && closed_siemens.ok && open_siemens.ok,
                "CQ8, Fanuc, and Siemens should post verified slot-frame paths")) {
        return 1;
    }
    if (!Expect(GCodeSafetyValidator::validate(
                    closed_cq8.snapshot.gcodeText).ok &&
                    GCodeSafetyValidator::validate(
                        open_cq8.snapshot.gcodeText).ok &&
                    GCodeSafetyValidator::validate(
                        closed_fanuc.snapshot.gcodeText).ok &&
                    GCodeSafetyValidator::validate(
                        closed_siemens.snapshot.gcodeText).ok &&
                    GCodeSafetyValidator::validate(
                        open_siemens.snapshot.gcodeText).ok,
                "all final controller programs should pass the G-code safety gate")) {
        return 1;
    }
    const bool fanuc_header = closed_fanuc.snapshot.gcodeText.contains(
        QStringLiteral("%\nO3103"));
    const bool siemens_header = open_siemens.snapshot.gcodeText.contains(
        QStringLiteral("Post-processor: Siemens 840D"));
    if (!fanuc_header || !siemens_header) {
        std::cerr << "Fanuc header=" << fanuc_header
                  << " Siemens header=" << siemens_header << '\n';
    }
    if (!Expect(fanuc_header && siemens_header,
                "controller-specific wrappers should remain in the postprocessor boundary")) {
        return 1;
    }
    const bool closed_simulation = SimulatesSafely(
        closed_cq8.snapshot.expandedGcodeText.isEmpty()
            ? closed_cq8.snapshot.gcodeText
            : closed_cq8.snapshot.expandedGcodeText,
        6.0);
    const bool open_simulation = SimulatesSafely(
        open_cq8.snapshot.expandedGcodeText.isEmpty()
            ? open_cq8.snapshot.gcodeText
            : open_cq8.snapshot.expandedGcodeText,
        6.0);
    if (!closed_simulation || !open_simulation) {
        std::cerr << "Closed simulation=" << closed_simulation
                  << " Open simulation=" << open_simulation << '\n';
    }
    if (!Expect(closed_simulation && open_simulation,
                "final CQ8 code should drive safe simulation to every confirmed bottom")) {
        return 1;
    }
    if (!Expect(open_cq8.snapshot.gcodeText.contains(
                    QStringLiteral("Open slot entry ramp from open side")),
                "the final open-slot program should retain the verified entry decision")) {
        return 1;
    }
    return 0;
}
