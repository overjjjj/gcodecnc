#include "../src/core/SelectionChainController.h"

#include <QCoreApplication>

#include <iostream>

namespace {

int expect(bool condition, const char *message)
{
    if (condition) {
        return 0;
    }
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    SelectionChainController controller;
    controller.begin();
    if (expect(controller.state() == SelectionChainState::Picking,
               "begin should enter picking state")) {
        return 1;
    }

    controller.setGeometrySource(ChainGeometrySource::Wire);
    controller.setSelectionMode(ChainSelectionMode::PartialChain);
    controller.setMachiningSide(ChainMachiningSide::Mixed);
    controller.setCoordinateSystemId(QStringLiteral("G55"));
    controller.setClosed(true);
    controller.setSourceContext(QStringLiteral("STEP-ABC"),
                                QStringLiteral("SETUP-123"));
    controller.setSelectedSurfaceNormal(QVector3D(0, 0, 1));
    controller.setToolAxis(QVector3D(0, 0, 1));
    controller.setTopologyEvidence(4, 1, 0);

    if (expect(controller.addGeometry(QStringLiteral("edge-B"), QVector3D(10, 0, 0)),
               "the first edge should be selectable") ||
        expect(controller.addGeometry(QStringLiteral("edge-A"), QVector3D(0, 0, 0)),
               "a second edge should be selectable") ||
        expect(controller.addGeometry(QStringLiteral("edge-C"), QVector3D(3, 0, 0)),
               "a third edge should be selectable") ||
        expect(!controller.addGeometry(QStringLiteral("edge-A"), QVector3D(0, 0, 0)),
               "the same stable geometry id must not be selected twice") ||
        expect(controller.state() == SelectionChainState::ChainBuilding,
               "selecting geometry should build a chain")) {
        return 1;
    }

    controller.setBranchChoices({QStringLiteral("edge-left"),
                                 QStringLiteral("edge-straight"),
                                 QStringLiteral("edge-right")},
                                1);
    if (expect(controller.previousBranch() == QStringLiteral("edge-left"),
               "previous branch should navigate left") ||
        expect(controller.nextBranch() == QStringLiteral("edge-straight"),
               "next branch should navigate right") ||
        expect(controller.switchBranch() == QStringLiteral("edge-right"),
               "switch branch should select the next alternative")) {
        return 1;
    }

    if (expect(controller.finishChain(), "a non-empty chain should be finishable") ||
        expect(controller.state() == SelectionChainState::Ordered,
               "finishing should enter ordered state") ||
        expect(controller.sort(ChainSortStrategy::PointToPoint),
               "point-to-point sorting should optimize the chain") ||
        expect(controller.geometryIds() ==
                   QStringList{QStringLiteral("edge-B"),
                               QStringLiteral("edge-C"),
                               QStringLiteral("edge-A")},
               "point-to-point sorting should retain the first selected edge")) {
        return 1;
    }

    controller.setStartPoint(QVector3D(10, 0, 0));
    if (expect(controller.reverse(), "a multi-edge chain should be reversible") ||
        expect(controller.geometryIds().first() == QStringLiteral("edge-A") &&
                   controller.reversed(),
               "reverse should invert order and remember path direction") ||
        expect(controller.undo(), "reverse should be undoable") ||
        expect(controller.geometryIds().first() == QStringLiteral("edge-B") &&
                   !controller.reversed(),
               "undo should restore path order and direction") ||
        expect(controller.redo(), "an undone reverse should be recoverable")) {
        return 1;
    }

    const SelectionChain committed = controller.commit();
    if (expect(controller.state() == SelectionChainState::Confirmed,
               "commit should confirm the chain") ||
        expect(committed.geometrySource == ChainGeometrySource::Wire &&
                   committed.selectionMode == ChainSelectionMode::PartialChain &&
                   committed.machiningSide == ChainMachiningSide::Mixed,
               "commit should retain common selector choices") ||
        expect(committed.coordinateSystemId == QStringLiteral("G55") &&
                   committed.closed && committed.hasStartPoint,
               "commit should retain coordinate system, closure and start point") ||
        expect(committed.orderedGeometryIds == controller.geometryIds(),
               "commit should expose the current geometry order") ||
        expect(committed.explicitUserSelection &&
                   committed.sourceFingerprint == QStringLiteral("STEP-ABC") &&
                   committed.setupFingerprint == QStringLiteral("SETUP-123"),
               "commit should retain versioned user-selection evidence") ||
        expect(committed.id.startsWith(QStringLiteral("selection:v1:")),
               "commit should assign a deterministic versioned selection id")) {
        return 1;
    }

    SelectionChainController equivalent;
    equivalent.begin();
    equivalent.setGeometrySource(ChainGeometrySource::Wire);
    equivalent.setSelectionMode(ChainSelectionMode::PartialChain);
    equivalent.setMachiningSide(ChainMachiningSide::Mixed);
    equivalent.setCoordinateSystemId(QStringLiteral("G55"));
    equivalent.setClosed(true);
    equivalent.setSourceContext(QStringLiteral("STEP-ABC"),
                                QStringLiteral("SETUP-123"));
    equivalent.setSelectedSurfaceNormal(QVector3D(0, 0, 1));
    equivalent.setToolAxis(QVector3D(0, 0, 1));
    equivalent.setTopologyEvidence(4, 1, 0);
    equivalent.setStartPoint(QVector3D(10, 0, 0));
    equivalent.addGeometry(QStringLiteral("edge-B"), QVector3D(10, 0, 0));
    equivalent.addGeometry(QStringLiteral("edge-C"), QVector3D(3, 0, 0));
    equivalent.addGeometry(QStringLiteral("edge-A"), QVector3D(0, 0, 0));
    equivalent.finishChain();
    equivalent.sort(ChainSortStrategy::PointToPoint);
    equivalent.reverse();
    const SelectionChain equivalentCommitted = equivalent.commit();
    if (expect(equivalentCommitted.id == committed.id,
               "equivalent selection evidence should produce the same stable id")) {
        return 1;
    }

    if (expect(evaluateSelectionAccess(committed,
                                       QStringLiteral("STEP-ABC"),
                                       QStringLiteral("SETUP-123"),
                                       QStringLiteral("G55"),
                                       true).status == SetupAccessStatus::Ready,
               "a closed explicitly selected Z-facing chain should be accessible") ||
        expect(evaluateSelectionAccess(committed,
                                       QStringLiteral("STEP-CHANGED"),
                                       QStringLiteral("SETUP-123"),
                                       QStringLiteral("G55"),
                                       true).status == SetupAccessStatus::StaleSource,
               "changed source geometry should stale selection evidence")) {
        return 1;
    }

    controller.begin();
    if (expect(controller.removeGeometry(QStringLiteral("edge-C")),
               "a committed edge should be removable while reselecting") ||
        expect(controller.isDirty(), "editing a confirmed chain should dirty the session")) {
        return 1;
    }
    controller.cancel();
    if (expect(controller.state() == SelectionChainState::Confirmed &&
                   controller.geometryIds() == committed.orderedGeometryIds &&
                   !controller.isDirty(),
               "cancel should restore the last confirmed chain")) {
        return 1;
    }

    std::cout << "PASS selection_chain_controller_test\n";
    return 0;
}
