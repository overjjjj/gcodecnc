#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <cstdlib>
#include <cmath>
#include <iostream>

#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"

static MeshPickEdge makeEdge(const QString &id, const QString &first, const QString &last,
                             const QVector3D &a, const QVector3D &b)
{
    MeshPickEdge edge;
    edge.id = id;
    edge.firstVertexId = first;
    edge.lastVertexId = last;
    edge.points = {a, b};
    edge.parameters = {0.0, 1.0};
    edge.adjacentFaceIndices = {1};
    return edge;
}

static SelectionCandidate polygonCandidate(int edgeCount, bool outer,
                                           const QString &faceId = QStringLiteral("face:14"))
{
    SelectionCandidate candidate;
    candidate.geometryId = QStringLiteral("region:%1:%2")
        .arg(outer ? QStringLiteral("outer") : QStringLiteral("inner"))
        .arg(edgeCount);
    candidate.faceIds = QStringList{faceId};
    candidate.surfaceNormal = QVector3D(0, 0, 1);
    candidate.closed = true;
    candidate.topologyOnly = true;
    MeshBoundaryLoop loop;
    loop.faceId = faceId;
    loop.faceIndex = faceId.section(QLatin1Char(':'), 1).toInt();
    loop.outer = outer;
    for (int i = 0; i < edgeCount; ++i) {
        const int next = (i + 1) % edgeCount;
        const float a = float(i) * 6.283185307f / float(edgeCount);
        const float b = float(next) * 6.283185307f / float(edgeCount);
        const QString edgeId = QStringLiteral("edge:%1").arg(i + 1);
        const QString first = QStringLiteral("vertex:%1").arg(i + 1);
        const QString last = QStringLiteral("vertex:%1").arg(next + 1);
        candidate.topologyEdges.append(makeEdge(
            edgeId, first, last,
            QVector3D(10.0f * std::cos(a), 10.0f * std::sin(a), 0),
            QVector3D(10.0f * std::cos(b), 10.0f * std::sin(b), 0)));
        loop.orderedEdgeIds.append(edgeId);
        loop.orderedForward.append(true);
    }
    candidate.boundaryLoops = {loop};
    candidate.outerLoopPointCount = outer ? edgeCount : 0;
    candidate.islandCount = outer ? 0 : 1;
    return candidate;
}

static void setChainMode(SelectionChainDialog &dialog)
{
    auto *mode = dialog.findChild<QComboBox *>(QStringLiteral("selectionMode"));
    mode->setCurrentIndex(static_cast<int>(ChainSelectionMode::Chain));
    QMetaObject::invokeMethod(mode, "activated",
                              Q_ARG(int, static_cast<int>(ChainSelectionMode::Chain)));
}

static void setMode(SelectionChainDialog &dialog, ChainSelectionMode value)
{
    auto *mode = dialog.findChild<QComboBox *>(QStringLiteral("selectionMode"));
    mode->setCurrentIndex(static_cast<int>(value));
    QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, static_cast<int>(value)));
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    SelectionCandidate candidate;
    candidate.geometryId = QStringLiteral("region:slot");
    candidate.faceIds = QStringList{QStringLiteral("face:1")};
    candidate.surfaceNormal = QVector3D(0, 0, 1);
    candidate.closed = true;
    candidate.outerLoopPointCount = 4;
    candidate.topologyEdges = {
        makeEdge("edge:1", "vertex:1", "vertex:2", {0, 0, 0}, {10, 0, 0}),
        makeEdge("edge:2", "vertex:2", "vertex:3", {10, 0, 0}, {10, 10, 0}),
        makeEdge("edge:3", "vertex:3", "vertex:4", {10, 10, 0}, {0, 10, 0}),
        makeEdge("edge:4", "vertex:4", "vertex:1", {0, 10, 0}, {0, 0, 0})
    };
    MeshBoundaryLoop outer;
    outer.faceId = QStringLiteral("face:1");
    outer.faceIndex = 1;
    outer.outer = true;
    outer.orderedEdgeIds = QStringList{"edge:1", "edge:2", "edge:3", "edge:4"};
    outer.orderedForward = {true, true, true, true};
    candidate.boundaryLoops = {outer};

    SelectionChainDialog dialog;
    dialog.setContext(QStringLiteral("source"), QStringLiteral("setup"), QStringLiteral("G54"));
    dialog.setCandidates({candidate});
    auto *mode = dialog.findChild<QComboBox *>(QStringLiteral("selectionMode"));

    {
        SelectionChainDialog slotDialog;
        int pickMode = -1;
        slotDialog.pickModeChanged = [&](int value) { pickMode = value; };
        slotDialog.setEdgePickingOnly(true);
        setMode(slotDialog, ChainSelectionMode::Face);
        if (pickMode != 1 ||
            slotDialog.findChild<QComboBox *>(QStringLiteral("selectionMode"))->currentIndex() ==
                static_cast<int>(ChainSelectionMode::Face)) {
            std::cerr << "slot boundary selection must not fall back to face picking\n";
            std::_Exit(1);
        }
    }

    mode->setCurrentIndex(static_cast<int>(ChainSelectionMode::Chain));
    QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, static_cast<int>(ChainSelectionMode::Chain)));
    dialog.selectTopology(QStringLiteral("edge:1"), 1, QVector3D(0.1f, 0, 0), 0.01);
    dialog.accept();
    const SelectionChain chain = dialog.selection();
    if (chain.evidenceVersion != 2 ||
        chain.orderedTopologyRefs.size() != 4 ||
        chain.orderedTopologyRefs.first().id != QStringLiteral("edge:1") ||
        !chain.hasStartAnchor || chain.startAnchor.topologyId != QStringLiteral("vertex:1") ||
        chain.boundaryLoops.size() != 1 || !chain.closed) {
        std::cerr << "near-end chain click must retain ordered real topology evidence\n";
        std::_Exit(1);
    }
    dialog.reject();

    dialog.setCandidates({candidate});
    mode->setCurrentIndex(static_cast<int>(ChainSelectionMode::PartialChain));
    QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, static_cast<int>(ChainSelectionMode::PartialChain)));
    dialog.selectTopology(QStringLiteral("edge:1"), 1, QVector3D(0.1f, 0, 0), 0.01);
    dialog.selectTopology(QStringLiteral("edge:3"), 1, QVector3D(9.9f, 10, 0), 0.01);
    dialog.accept();
    const SelectionChain partial = dialog.selection();
    if (partial.orderedTopologyRefs.size() != 3 || partial.closed ||
        !partial.hasEndAnchor || partial.endAnchor.topologyId != QStringLiteral("vertex:4")) {
        std::cerr << "partial chain must stop at the clicked edge near endpoint\n";
        std::_Exit(1);
    }
    dialog.reject();

    dialog.setCandidates({candidate});
    mode->setCurrentIndex(static_cast<int>(ChainSelectionMode::Face));
    QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, static_cast<int>(ChainSelectionMode::Face)));
    dialog.selectTopology(QStringLiteral("face:1"), 0, QVector3D(5, 5, 0), 0.0);
    dialog.accept();
    const SelectionChain face = dialog.selection();
    if (face.orderedTopologyRefs.size() != 1 ||
        face.orderedTopologyRefs.first().id != QStringLiteral("face:1") ||
        face.boundaryLoops.size() != 1 || !face.boundaryLoops.first().outer) {
        std::cerr << "face pick must retain its real boundary loops\n";
        std::_Exit(1);
    }

    // A real 14-edge outer wire must be ready in the UI and accepted through
    // the same access decision used by accept().
    {
        SelectionChainDialog outerDialog;
        outerDialog.setRequireClosedBoundary(true);
        outerDialog.setContext("source", "setup", "G54");
        const SelectionCandidate outer14 = polygonCandidate(14, true);
        outerDialog.setCandidates({outer14});
        setChainMode(outerDialog);
        outerDialog.selectGeometry(outer14.geometryId);
        outerDialog.findChild<QPushButton *>(QStringLiteral("selectionUseFullBoundary"))->click();
        auto *status = outerDialog.findChild<QLabel *>(QStringLiteral("selectionChainStatus"));
        if (!status || !status->text().contains(QStringLiteral("加工参数"))) {
            std::cerr << "RED: a valid 14-edge outer loop must be reported ready\n";
            std::_Exit(1);
        }
        outerDialog.accept();
        if (outerDialog.result() != QDialog::Accepted ||
            outerDialog.selection().outerLoopPointCount != 14) {
            std::cerr << "RED: a valid 14-edge outer loop must be accepted\n";
            std::_Exit(1);
        }
    }

    {
        SelectionChainDialog incrementalDialog;
        incrementalDialog.setRequireClosedBoundary(true);
        incrementalDialog.setContext("source", "setup", "G54");
        const SelectionCandidate outer14 = polygonCandidate(14, true);
        incrementalDialog.setCandidates({outer14});
        setMode(incrementalDialog, ChainSelectionMode::Edge);
        for (const MeshPickEdge &edge : outer14.topologyEdges)
            incrementalDialog.selectTopology(edge.id, 1, edge.points.first(), 0.0);
        incrementalDialog.accept();
        if (incrementalDialog.result() != QDialog::Accepted ||
            incrementalDialog.selection().orderedTopologyRefs.size() != 14 ||
            incrementalDialog.selection().outerLoopPointCount != 14) {
            std::cerr << "RED: incrementally selected 14-edge outer loop must be accepted\n";
            std::_Exit(1);
        }
    }

    // Hover is a single-edge affordance.  It must not replace the persistent
    // confirmed-chain highlight with the whole tentative interval.
    {
        SelectionChainDialog hoverDialog;
        const SelectionCandidate outer14 = polygonCandidate(14, true);
        QVector<QVector3D> confirmedPoints;
        QVector<QVector3D> hoverPoints;
        hoverDialog.confirmedTopologyPreviewChanged =
            [&](const QVector<QVector3D> &points, bool) { confirmedPoints = points; };
        hoverDialog.topologyHoverChanged =
            [&](const QVector<QVector3D> &points) { hoverPoints = points; };
        hoverDialog.setContext("source", "setup", "G54");
        hoverDialog.setCandidates({outer14});
        setChainMode(hoverDialog);
        hoverDialog.selectGeometry(outer14.geometryId);
        hoverDialog.findChild<QPushButton *>(QStringLiteral("selectionAdjustBoundary"))->click();
        hoverDialog.selectTopology("edge:1", 1,
                                   outer14.topologyEdges.at(0).points.first(), 0.0);
        const QVector<QVector3D> selectedBeforeHover = confirmedPoints;
        hoverDialog.previewTopology("edge:4", 1,
                                    outer14.topologyEdges.at(3).points.first(), 0.0);
        if (hoverPoints != outer14.topologyEdges.at(3).points ||
            confirmedPoints != selectedBeforeHover || confirmedPoints.isEmpty()) {
            std::cerr << "RED: hover must preview one edge without replacing the selected chain\n";
            std::_Exit(1);
        }
        hoverDialog.previewTopology(QString(), 1, {}, 0.0);
        if (hoverDialog.hasCandidatePreview() || confirmedPoints != selectedBeforeHover) {
            std::cerr << "RED: leaving an edge must clear hover and preserve the selected chain\n";
            std::_Exit(1);
        }
    }

    {
        SelectionCandidate first = polygonCandidate(4, true, QStringLiteral("face:1"));
        SelectionCandidate second = polygonCandidate(4, true, QStringLiteral("face:2"));
        second.geometryId = QStringLiteral("region:second");
        for (MeshPickEdge &edge : second.topologyEdges) edge.id.prepend(QStringLiteral("second-"));
        for (QString &edgeId : second.boundaryLoops[0].orderedEdgeIds)
            edgeId.prepend(QStringLiteral("second-"));
        SelectionChainDialog scopeDialog;
        QStringList scopedEdges;
        QVector<int> scopedFaces;
        scopeDialog.topologyScopeChanged = [&](const QStringList &edges, const QStringList &,
                                                const QVector<int> &faces) {
            scopedEdges = edges;
            scopedFaces = faces;
        };
        scopeDialog.setContext("source", "setup", "G54");
        scopeDialog.setCandidates({first, second});
        if (scopedEdges.size() != 8 || scopedFaces.size() != 2) {
            std::cerr << "RED: initial scope must contain all front candidates\n";
            std::_Exit(1);
        }
        scopeDialog.selectGeometry(second.geometryId);
        if (scopedEdges.size() != 4 || scopedFaces != QVector<int>{2} ||
            !scopedEdges.first().startsWith(QStringLiteral("second-"))) {
            std::cerr << "RED: selecting a machining plane must narrow the pick scope\n";
            std::_Exit(1);
        }
    }

    // An inner BRep wire is not silently promoted to an outer machining
    // boundary.  The UI and accept() must agree and explain the source role.
    {
        SelectionChainDialog innerDialog;
        innerDialog.setRequireClosedBoundary(true);
        innerDialog.setContext("source", "setup", "G54");
        const SelectionCandidate inner14 = polygonCandidate(14, false);
        innerDialog.setCandidates({inner14});
        setChainMode(innerDialog);
        innerDialog.selectGeometry(inner14.geometryId);
        innerDialog.findChild<QPushButton *>(QStringLiteral("selectionUseFullBoundary"))->click();
        auto *chainStatus = innerDialog.findChild<QLabel *>(QStringLiteral("selectionChainStatus"));
        if (!chainStatus || chainStatus->text().contains(QStringLiteral("下一步：加工参数"))) {
            std::cerr << "RED: an inner loop must not be reported ready\n";
            std::_Exit(1);
        }
        innerDialog.accept();
        const auto labels = innerDialog.findChildren<QLabel *>();
        bool explained = false;
        for (QLabel *label : labels)
            explained = explained || label->text().contains(QStringLiteral("内环"));
        if (innerDialog.result() == QDialog::Accepted || !explained) {
            std::cerr << "RED: inner-loop rejection must name the BRep loop role\n";
            std::_Exit(1);
        }
    }

    // One closed curve is a legal outer wire when the source topology says so;
    // counting polygon vertices must not reject circles/ellipses.
    {
        for (int edgeCount : {1, 2}) {
            SelectionCandidate curved = polygonCandidate(edgeCount, true, QStringLiteral("face:1"));
            if (edgeCount == 1) {
                curved.topologyEdges[0].firstVertexId = QStringLiteral("vertex:1");
                curved.topologyEdges[0].lastVertexId = QStringLiteral("vertex:1");
                curved.topologyEdges[0].points = {
                    QVector3D(10, 0, 0), QVector3D(0, 10, 0), QVector3D(-10, 0, 0),
                    QVector3D(0, -10, 0), QVector3D(10, 0, 0)};
            }
            SelectionChainDialog curvedDialog;
            curvedDialog.setRequireClosedBoundary(true);
            curvedDialog.setContext("source", "setup", "G54");
            curvedDialog.setCandidates({curved});
            setChainMode(curvedDialog);
            curvedDialog.selectGeometry(curved.geometryId);
            curvedDialog.findChild<QPushButton *>(QStringLiteral("selectionUseFullBoundary"))->click();
            curvedDialog.accept();
            if (curvedDialog.result() != QDialog::Accepted) {
                std::cerr << "RED: a one/two-edge closed outer wire must be accepted\n";
                std::_Exit(1);
            }
        }
    }

    // A geometrically closed chain without a confirmed source loop must not
    // fabricate outer-boundary evidence.
    {
        SelectionChain evidence;
        evidence.explicitUserSelection = true;
        evidence.evidenceVersion = 2;
        evidence.orderedGeometryIds = QStringList{QStringLiteral("edge:1")};
        evidence.orderedTopologyRefs = {{QStringLiteral("edge:1"), SelectionTopologyKind::Edge, true}};
        evidence.sourceFingerprint = QStringLiteral("source");
        evidence.setupFingerprint = QStringLiteral("setup");
        evidence.coordinateSystemId = QStringLiteral("G54");
        evidence.selectedSurfaceNormal = QVector3D(0, 0, 1);
        evidence.toolAxis = QVector3D(0, 0, 1);
        evidence.closed = true;
        evidence.outerLoopPointCount = 1;
        evidence.freeEndCount = 0;
        if (evaluateSelectionAccess(evidence, "source", "setup", "G54", true).ok()) {
            std::cerr << "RED: a v2 closed chain without a source loop must be rejected\n";
            std::_Exit(1);
        }
    }

    std::cout << "PASS topology chain, partial-chain and face evidence\n";
    std::cout.flush();
    std::_Exit(0);
}
