#include "../src/import/SlotFrameRecognizer.h"
#include "../src/services/SlotFramePlanningService.h"

#include <QCoreApplication>

#include <iostream>

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

SlotFrameToolInfo Tool()
{
    SlotFrameToolInfo tool;
    tool.id = 5;
    tool.type = QStringLiteral("end_mill");
    tool.diameter = 6.0;
    tool.fluteLength = 20.0;
    tool.totalLength = 60.0;
    return tool;
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
    feature.geometryRef = QStringLiteral("slot-frame:closed-1");
    feature.setup = Context().setup;
    feature.kind = SlotFrameKind::ClosedPocket;
    feature.closed = true;
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.region = FaceRegion::Front;
    feature.depth = 6.0;
    feature.outerBoundary = {
        QVector3D(0.0f, 0.0f, 0.0f), QVector3D(60.0f, 0.0f, 0.0f),
        QVector3D(60.0f, 40.0f, 0.0f), QVector3D(0.0f, 40.0f, 0.0f)};
    feature.islands = {{
        QVector3D(25.0f, 15.0f, 0.0f), QVector3D(35.0f, 15.0f, 0.0f),
        QVector3D(35.0f, 25.0f, 0.0f), QVector3D(25.0f, 25.0f, 0.0f)}};
    feature.machiningGeometry.subType = QStringLiteral("irregular_pocket");
    feature.machiningGeometry.center = QVector3D(30.0f, 20.0f, 0.0f);
    feature.machiningGeometry.depth = feature.depth;
    feature.machiningGeometry.axis = feature.axis;
    feature.machiningGeometry.region = feature.region;
    feature.machiningGeometry.points = feature.outerBoundary;
    feature.machiningGeometry.islands = feature.islands;
    return feature;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const QMap<int, SlotFrameToolInfo> tools{{5, Tool()}};
    const SlotFrameConfirmationResult closed =
        SlotFramePlanningService::Confirm(
            ClosedFeature(), tools, Context(), Layers());
    if (!Expect(closed.ok && closed.operations.size() == 1 &&
                    closed.operations.first().strategyId ==
                        QStringLiteral("mill_pocket_rough") &&
                    closed.operations.first().geometryRefs ==
                        QStringList{QStringLiteral("slot-frame:closed-1")},
                "explicit confirmation should create one traceable pocket operation")) {
        return 1;
    }

    SlotFrameFeature open = ClosedFeature();
    open.geometryRef = QStringLiteral("slot-frame:open-1");
    open.kind = SlotFrameKind::OpenSlot;
    open.closed = false;
    open.complementType = SlotFrameComplementType::DirectBridge;
    open.islands.clear();
    open.outerBoundary = {
        QVector3D(-20.0f, -6.0f, 0.0f), QVector3D(20.0f, -6.0f, 0.0f),
        QVector3D(20.0f, 6.0f, 0.0f), QVector3D(-20.0f, 6.0f, 0.0f)};
    open.freeEdges = {
        {QVector3D(-20.0f, -6.0f, 0.0f), QVector3D(-20.0f, 0.0f, 0.0f), 1},
        {QVector3D(-20.0f, 0.0f, 0.0f), QVector3D(-20.0f, 6.0f, 0.0f), 1}};
    open.machiningGeometry.subType = QStringLiteral("open_slot");
    open.machiningGeometry.center = QVector3D();
    open.machiningGeometry.length = 40.0;
    open.machiningGeometry.width = 12.0;
    open.machiningGeometry.depth = 6.0;
    open.machiningGeometry.openSide = -1.0;
    open.machiningGeometry.points = open.outerBoundary;
    open.machiningGeometry.islands.clear();
    const SlotFrameConfirmationResult opened =
        SlotFramePlanningService::Confirm(open, tools, Context(), Layers());
    if (!Expect(opened.ok && opened.operations.size() == 1 &&
                    opened.operations.first().strategyId ==
                        QStringLiteral("mill_slot"),
                "an explicitly confirmed open slot should reuse the slot strategy")) {
        return 1;
    }

    SlotFrameFeature narrow = open;
    narrow.machiningGeometry.width = 7.0;
    if (!Expect(!SlotFramePlanningService::Confirm(
                    narrow, tools, Context(), Layers()).ok,
                "a channel narrower than the tool envelope must be rejected")) {
        return 1;
    }

    SlotFrameFeature protectedCollision = ClosedFeature();
    protectedCollision.protectionRegions = {{
        QVector3D(0.0f, 10.0f, 0.0f), QVector3D(12.0f, 10.0f, 0.0f),
        QVector3D(12.0f, 30.0f, 0.0f), QVector3D(0.0f, 30.0f, 0.0f)}};
    if (!Expect(!SlotFramePlanningService::Confirm(
                    protectedCollision, tools, Context(), Layers()).ok,
                "a protection region touching the outer boundary must block generation")) {
        return 1;
    }

    OperationParameterLayers excessiveStepOver = Layers();
    excessiveStepOver.manualOverrides.set(QStringLiteral("stepOver"), 6.5);
    if (!Expect(!SlotFramePlanningService::Confirm(
                    ClosedFeature(), tools, Context(), excessiveStepOver).ok,
                "stepOver greater than tool diameter must be rejected")) {
        return 1;
    }

    if (!Expect(!SlotFramePlanningService::Confirm(
                    ClosedFeature(), {}, Context(), Layers()).ok,
                "missing tools must leave no formal operations")) {
        return 1;
    }

    return 0;
}
