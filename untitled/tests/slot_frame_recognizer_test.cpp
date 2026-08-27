#include "../src/import/SlotFrameRecognizer.h"

#include <QCoreApplication>

#include <cmath>
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

SetupContext ConfirmedSetup()
{
    SetupContext setup;
    setup.workOffset = QStringLiteral("G54");
    setup.origin.confirmed = true;
    return setup;
}

MachiningFeature RectangularPocket()
{
    MachiningFeature source;
    source.kind = FeatureKind::Pocket;
    source.subType = QStringLiteral("rectangular_pocket");
    source.center = QVector3D(20.0f, 10.0f, 0.0f);
    source.axis = QVector3D(0.0f, 0.0f, 1.0f);
    source.region = FaceRegion::Front;
    source.length = 40.0;
    source.width = 20.0;
    source.depth = 6.0;
    source.faceIndices = {4, 8, 9, 10, 11};
    source.boundaryPoints = {
        QVector3D(0.0f, 0.0f, 0.0f), QVector3D(40.0f, 0.0f, 0.0f),
        QVector3D(40.0f, 20.0f, 0.0f), QVector3D(0.0f, 20.0f, 0.0f)};
    return source;
}

SlotFrameTopologyEvidence ClosedEvidence()
{
    SlotFrameTopologyEvidence evidence;
    evidence.bottomPlanar = true;
    evidence.sideWallsPlanar = true;
    evidence.bottomBoundaryComplete = true;
    evidence.edgeFaceCounts = {2, 2, 2, 2};
    evidence.vertexEdgeCounts = {2, 2, 2, 2};
    return evidence;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const MachiningFeature rectangle = RectangularPocket();
    const SlotFrameRecognitionResult closed = SlotFrameRecognizer::Recognize(
        rectangle, ConfirmedSetup(), ClosedEvidence());
    if (!Expect(closed.ok && closed.feature.closed &&
                    closed.feature.kind == SlotFrameKind::ClosedPocket &&
                    closed.feature.outerBoundary.size() == 4 &&
                    closed.feature.freeEdges.isEmpty() &&
                    closed.feature.geometryRef.startsWith(QStringLiteral("slot-frame:")),
                "a planar front rectangular pocket should become a closed candidate")) {
        return 1;
    }

    MachiningFeature polygon = rectangle;
    polygon.subType = QStringLiteral("irregular_pocket");
    polygon.boundaryPoints = {
        QVector3D(0.0f, 0.0f, 0.0f), QVector3D(35.0f, 0.0f, 0.0f),
        QVector3D(42.0f, 14.0f, 0.0f), QVector3D(24.0f, 28.0f, 0.0f),
        QVector3D(0.0f, 20.0f, 0.0f)};
    SlotFrameTopologyEvidence polygonEvidence = ClosedEvidence();
    polygonEvidence.edgeFaceCounts = {2, 2, 2, 2, 2};
    polygonEvidence.vertexEdgeCounts = {2, 2, 2, 2, 2};
    const SlotFrameRecognitionResult convex = SlotFrameRecognizer::Recognize(
        polygon, ConfirmedSetup(), polygonEvidence);
    if (!Expect(convex.ok &&
                    convex.feature.machiningGeometry.subType ==
                        QStringLiteral("irregular_pocket"),
                "a simple convex planar polygon should remain an explicit boundary")) {
        return 1;
    }

    MachiningFeature openSlot = rectangle;
    openSlot.kind = FeatureKind::Slot;
    openSlot.subType = QStringLiteral("open_slot");
    openSlot.length = 40.0;
    openSlot.width = 12.0;
    openSlot.boundaryPoints = {
        QVector3D(-20.0f, -6.0f, 0.0f), QVector3D(20.0f, -6.0f, 0.0f),
        QVector3D(20.0f, 6.0f, 0.0f), QVector3D(-20.0f, 6.0f, 0.0f)};
    SlotFrameTopologyEvidence openEvidence = ClosedEvidence();
    openEvidence.bottomBoundaryComplete = false;
    openEvidence.edgeFaceCounts = {2, 2, 2, 1};
    openEvidence.vertexEdgeCounts = {1, 2, 2, 1};
    openEvidence.freeEdges = {
        {QVector3D(-20.0f, -6.0f, 0.0f), QVector3D(-20.0f, 0.0f, 0.0f), 1},
        {QVector3D(-20.0f, 0.0f, 0.0f), QVector3D(-20.0f, 6.0f, 0.0f), 1}};
    const SlotFrameRecognitionResult opened = SlotFrameRecognizer::Recognize(
        openSlot, ConfirmedSetup(), openEvidence);
    if (!Expect(opened.ok && !opened.feature.closed &&
                    opened.feature.complementType !=
                        SlotFrameComplementType::None &&
                    opened.feature.machiningGeometry.openSide < 0.0,
                "an open slot should retain two explainable free ends as metadata")) {
        return 1;
    }

    SlotFrameTopologyEvidence abnormalOpen = openEvidence;
    abnormalOpen.freeEdges.removeLast();
    if (!Expect(!SlotFrameRecognizer::Recognize(
                    openSlot, ConfirmedSetup(), abnormalOpen).ok,
                "an open slot with an unmatched free end must be rejected")) {
        return 1;
    }

    SlotFrameTopologyEvidence nonManifold = ClosedEvidence();
    nonManifold.edgeFaceCounts[2] = 3;
    if (!Expect(!SlotFrameRecognizer::Recognize(
                    rectangle, ConfirmedSetup(), nonManifold).ok,
                "a non-manifold boundary edge must be rejected")) {
        return 1;
    }

    SlotFrameTopologyEvidence brokenBottom = ClosedEvidence();
    brokenBottom.bottomBoundaryComplete = false;
    if (!Expect(!SlotFrameRecognizer::Recognize(
                    rectangle, ConfirmedSetup(), brokenBottom).ok,
                "a closed pocket without a complete bottom loop must be rejected")) {
        return 1;
    }

    SlotFrameTopologyEvidence curvedBottom = ClosedEvidence();
    curvedBottom.bottomPlanar = false;
    if (!Expect(!SlotFrameRecognizer::Recognize(
                    rectangle, ConfirmedSetup(), curvedBottom).ok,
                "a curved or inconsistent bottom must be rejected")) {
        return 1;
    }

    SlotFrameTopologyEvidence intersecting = ClosedEvidence();
    intersecting.intersecting = true;
    if (!Expect(!SlotFrameRecognizer::Recognize(
                    rectangle, ConfirmedSetup(), intersecting).ok,
                "intersecting slot-frame candidates must be rejected")) {
        return 1;
    }

    MachiningFeature sidePocket = rectangle;
    sidePocket.region = FaceRegion::Side;
    sidePocket.axis = QVector3D(1.0f, 0.0f, 0.0f);
    if (!Expect(!SlotFrameRecognizer::Recognize(
                    sidePocket, ConfirmedSetup(), ClosedEvidence()).ok,
                "side-face slot frames must require a dedicated setup")) {
        return 1;
    }

    const SlotFrameRecognitionResult repeated = SlotFrameRecognizer::Recognize(
        rectangle, ConfirmedSetup(), ClosedEvidence());
    if (!Expect(repeated.ok &&
                    repeated.feature.geometryRef == closed.feature.geometryRef,
                "unchanged slot-frame geometry must keep a stable identifier")) {
        return 1;
    }

    MachiningFeature changed = rectangle;
    changed.boundaryPoints[1].setX(41.0f);
    const SlotFrameRecognitionResult changedResult = SlotFrameRecognizer::Recognize(
        changed, ConfirmedSetup(), ClosedEvidence());
    if (!Expect(changedResult.ok &&
                    changedResult.feature.geometryRef != closed.feature.geometryRef,
                "a changed boundary must produce a different stable identifier")) {
        return 1;
    }

    return 0;
}
