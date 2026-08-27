#include "../src/core/SelectionChainController.h"
#include "../src/services/OperationFactory.h"

#include <QCoreApplication>

#include <iostream>

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

    SelectionChainController controller;
    controller.begin();
    controller.setGeometrySource(ChainGeometrySource::Wire);
    controller.setSelectionMode(ChainSelectionMode::PartialChain);
    controller.setMachiningSide(ChainMachiningSide::Inside);
    controller.setCoordinateSystemId(QStringLiteral("G55"));
    controller.setClosed(true);
    controller.addGeometry(QStringLiteral("edge-b"), QVector3D(10, 0, 0));
    controller.addGeometry(QStringLiteral("edge-a"), QVector3D(0, 0, 0));
    controller.finishChain();
    controller.sort(ChainSortStrategy::UnidirectionalX);
    controller.reverse();
    controller.setStartPoint(QVector3D(10, 0, 0));
    controller.setBranchChoices({QStringLiteral("branch-left"),
                                 QStringLiteral("branch-right")});
    controller.nextBranch();
    const SelectionChain chain = controller.commit();

    OperationProposal proposal;
    proposal.kind = OperationProposalKind::Contour;
    proposal.strategyId = QStringLiteral("mill_closed_contour");
    proposal.toolId = 5;
    proposal.contourFeature.subType = QStringLiteral("closed_contour");
    proposal.contourFeature.depth = 2.0;
    proposal.contourFeature.points = {
        QVector3D(0, 0, 0), QVector3D(10, 0, 0),
        QVector3D(10, 10, 0), QVector3D(0, 10, 0)};
    proposal.selectionChain = chain;

    OperationParameterLayers layers;
    layers.systemDefaults.set(QStringLiteral("safeHeight"), 20.0);
    layers.systemDefaults.set(QStringLiteral("plungeHeight"), 2.0);
    layers.systemDefaults.set(QStringLiteral("referenceHeight"), 0.0);
    layers.systemDefaults.set(QStringLiteral("depth"), 2.0);
    layers.systemDefaults.set(QStringLiteral("stepOver"), 1.0);
    layers.systemDefaults.set(QStringLiteral("stepDown"), 1.0);
    layers.systemDefaults.set(QStringLiteral("spindleSpeed"), 2000.0);
    layers.systemDefaults.set(QStringLiteral("feedRate"), 300.0);
    layers.systemDefaults.set(QStringLiteral("plungeRate"), 100.0);

    const OperationFactoryResult created = OperationFactory::CreateConfirmed(
        proposal, layers);
    if (!expect(created.ok, "a confirmed contour chain must create an operation") ||
        !expect(created.operation.selectionChain.geometrySource ==
                    ChainGeometrySource::Wire &&
                    created.operation.selectionChain.selectionMode ==
                        ChainSelectionMode::PartialChain &&
                    created.operation.selectionChain.machiningSide ==
                        ChainMachiningSide::Inside,
                "source, mode and machining side must survive confirmation") ||
        !expect(created.operation.selectionChain.closed &&
                    created.operation.selectionChain.reversed &&
                    created.operation.selectionChain.hasStartPoint &&
                    created.operation.selectionChain.sortStrategy ==
                        ChainSortStrategy::UnidirectionalX &&
                    created.operation.selectionChain.selectedBranchGeometryId ==
                        QStringLiteral("branch-right"),
                "closed, start, reverse, sort and branch must survive confirmation") ||
        !expect(created.operation.geometryRefs == chain.orderedGeometryIds,
                "the confirmed operation must trace the ordered selected geometry")) {
        return 1;
    }

    proposal.strategyId = QStringLiteral("mill_annular");
    proposal.selectionChain.closed = false;
    if (!expect(!OperationFactory::CreateConfirmed(proposal, layers).ok,
                "annular milling must reject an open outer chain")) {
        return 1;
    }
    proposal.selectionChain.closed = true;
    proposal.contourFeature.radius = 20.0;
    proposal.contourFeature.points = {
        QVector3D(20, 0, 0), QVector3D(14, 14, 0), QVector3D(0, 20, 0),
        QVector3D(-14, 14, 0), QVector3D(-20, 0, 0), QVector3D(-14, -14, 0),
        QVector3D(0, -20, 0), QVector3D(14, -14, 0)};
    proposal.contourFeature.islands = {{
        QVector3D(8, 0, 0), QVector3D(6, 6, 0), QVector3D(0, 8, 0),
        QVector3D(-6, 6, 0), QVector3D(-8, 0, 0), QVector3D(-6, -6, 0),
        QVector3D(0, -8, 0), QVector3D(6, -6, 0)}};
    proposal.selectionChain.selectedBranchGeometryId.clear();
    if (!expect(!OperationFactory::CreateConfirmed(proposal, layers).ok,
                "annular milling must require explicit island branch binding")) {
        return 1;
    }
    proposal.selectionChain.selectedBranchGeometryId = QStringLiteral("island:0");
    if (!expect(OperationFactory::CreateConfirmed(proposal, layers).ok,
                "verified outer and island bindings should create an annular operation")) {
        return 1;
    }
    proposal.contourFeature.islands.clear();
    if (!expect(!OperationFactory::CreateConfirmed(proposal, layers).ok,
                "annular milling must reject a missing island boundary")) {
        return 1;
    }

    proposal.strategyId = QStringLiteral("mill_outer_chamfer");
    proposal.contourFeature.radius = 0.0;
    proposal.contourFeature.points = {
        QVector3D(0, 0, 0), QVector3D(20, 0, 0),
        QVector3D(20, 10, 0), QVector3D(0, 10, 0)};
    proposal.selectionChain.closed = true;
    proposal.selectionChain.machiningSide = ChainMachiningSide::Outside;
    if (!expect(OperationFactory::CreateConfirmed(proposal, layers).ok,
                "confirmed outside closed chain should create an outer chamfer operation")) {
        return 1;
    }
    proposal.selectionChain.machiningSide = ChainMachiningSide::Inside;
    if (!expect(!OperationFactory::CreateConfirmed(proposal, layers).ok,
                "outer chamfer must reject a chain that is not explicitly outside")) {
        return 1;
    }

    proposal.strategyId = QStringLiteral("mill_slope_plane_2d");
    proposal.selectionChain.machiningSide = ChainMachiningSide::Mixed;
    proposal.selectionChain.closed = true;
    proposal.selectionChain.orderedGeometryIds = QStringList{
        QStringLiteral("edge-1"), QStringLiteral("edge-2"),
        QStringLiteral("edge-3"), QStringLiteral("edge-4")};
    if (!expect(OperationFactory::CreateConfirmed(proposal, layers).ok,
                "verified closed rectangular chain should create a 2D slope operation")) {
        return 1;
    }
    proposal.selectionChain.closed = false;
    return expect(!OperationFactory::CreateConfirmed(proposal, layers).ok,
                  "2D slope must reject an open projected boundary") ? 0 : 1;
}
