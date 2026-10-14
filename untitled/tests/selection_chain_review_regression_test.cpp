#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QKeyEvent>
#include <QListWidget>
#include <QPushButton>
#include <cstdlib>
#include <iostream>

#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"

static MeshPickEdge edge(const QString &id, const QString &first, const QString &last,
                         const QVector3D &a, const QVector3D &b)
{
    MeshPickEdge value;
    value.id = id;
    value.firstVertexId = first;
    value.lastVertexId = last;
    value.points = {a, b};
    value.parameters = {0.0, 1.0};
    value.adjacentFaceIndices = {1};
    return value;
}

static MeshPickVertex vertex(const QString &id, const QVector3D &point,
                             const QStringList &incident)
{
    MeshPickVertex value;
    value.id = id;
    value.point = point;
    value.incidentEdgeIds = incident;
    return value;
}

static SelectionCandidate square(const QString &geometryId, const QString &faceId)
{
    SelectionCandidate candidate;
    candidate.geometryId = geometryId;
    candidate.label = geometryId;
    candidate.faceIds = QStringList{faceId};
    candidate.surfaceNormal = QVector3D(0, 0, 1);
    candidate.closed = true;
    candidate.outerLoopPointCount = 4;
    candidate.topologyEdges = {
        edge("edge:1", "vertex:1", "vertex:2", {0, 0, 0}, {10, 0, 0}),
        edge("edge:2", "vertex:2", "vertex:3", {10, 0, 0}, {10, 10, 0}),
        edge("edge:3", "vertex:3", "vertex:4", {10, 10, 0}, {0, 10, 0}),
        edge("edge:4", "vertex:4", "vertex:1", {0, 10, 0}, {0, 0, 0})
    };
    candidate.topologyVertices = {
        vertex("vertex:1", {0, 0, 0}, {"edge:1", "edge:4"}),
        vertex("vertex:2", {10, 0, 0}, {"edge:1", "edge:2"}),
        vertex("vertex:3", {10, 10, 0}, {"edge:2", "edge:3"}),
        vertex("vertex:4", {0, 10, 0}, {"edge:3", "edge:4"}),
        vertex("vertex:outside", {50, 50, 0}, {})
    };
    MeshBoundaryLoop loop;
    loop.faceId = faceId;
    loop.faceIndex = 1;
    loop.outer = true;
    loop.orderedEdgeIds = QStringList{"edge:1", "edge:2", "edge:3", "edge:4"};
    loop.orderedForward = {true, true, true, true};
    candidate.boundaryLoops = {loop};
    return candidate;
}

static SelectionCandidate singleEdgeCandidate(const QString &geometryId,
                                               const QString &faceId,
                                               const QString &edgeId,
                                               const QString &firstVertexId,
                                               const QString &lastVertexId,
                                               const QVector3D &firstPoint,
                                               const QVector3D &lastPoint)
{
    SelectionCandidate candidate;
    candidate.geometryId = geometryId;
    candidate.label = geometryId;
    candidate.faceIds = QStringList{faceId};
    candidate.surfaceNormal = QVector3D(0, 0, 1);
    candidate.topologyEdges = {
        edge(edgeId, firstVertexId, lastVertexId, firstPoint, lastPoint)
    };
    candidate.topologyVertices = {
        vertex(firstVertexId, firstPoint, {edgeId}),
        vertex(lastVertexId, lastPoint, {edgeId})
    };
    MeshBoundaryLoop loop;
    loop.faceId = faceId;
    loop.faceIndex = 2;
    loop.outer = true;
    loop.orderedEdgeIds = QStringList{edgeId};
    loop.orderedForward = {true};
    candidate.boundaryLoops = {loop};
    return candidate;
}

static SelectionCandidate squareWithInnerLoop()
{
    SelectionCandidate candidate = square("region:with-hole", "face:with-hole");
    candidate.topologyEdges.append(
        edge("edge:5", "vertex:5", "vertex:6", {3, 3, 0}, {7, 3, 0}));
    candidate.topologyEdges.append(
        edge("edge:6", "vertex:6", "vertex:7", {7, 3, 0}, {5, 7, 0}));
    candidate.topologyEdges.append(
        edge("edge:7", "vertex:7", "vertex:5", {5, 7, 0}, {3, 3, 0}));
    candidate.topologyVertices.append(vertex("vertex:5", {3, 3, 0}, {"edge:5", "edge:7"}));
    candidate.topologyVertices.append(vertex("vertex:6", {7, 3, 0}, {"edge:5", "edge:6"}));
    candidate.topologyVertices.append(vertex("vertex:7", {5, 7, 0}, {"edge:6", "edge:7"}));
    MeshBoundaryLoop inner;
    inner.faceId = "face:with-hole";
    inner.faceIndex = 1;
    inner.outer = false;
    inner.orderedEdgeIds = QStringList{"edge:5", "edge:6", "edge:7"};
    inner.orderedForward = {true, true, true};
    candidate.boundaryLoops.append(inner);
    return candidate;
}

static void setMode(SelectionChainDialog &dialog, ChainSelectionMode mode)
{
    auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("selectionMode"));
    combo->setCurrentIndex(static_cast<int>(mode));
    QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, static_cast<int>(mode)));
}

static bool sameRefs(const QList<SelectionTopologyRef> &refs,
                     const QStringList &ids, const QVector<bool> &forward)
{
    if (refs.size() != ids.size() || refs.size() != forward.size()) return false;
    for (int i = 0; i < refs.size(); ++i)
        if (refs.at(i).id != ids.at(i) || refs.at(i).forward != forward.at(i)) return false;
    return true;
}

static bool sameRows(SelectionChainDialog &dialog, const QStringList &ids,
                     const QVector<bool> &forward)
{
    auto *rows = dialog.findChild<QListWidget *>(QStringLiteral("selectionEdges"));
    if (!rows || rows->count() != ids.size() || ids.size() != forward.size()) return false;
    for (int index = 0; index < ids.size(); ++index) {
        const QString text = rows->item(index)->text();
        if (!text.startsWith(ids.at(index)) ||
            !text.contains(forward.at(index) ? QStringLiteral("正向")
                                             : QStringLiteral("反向"))) return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const SelectionCandidate base = square("region:A", "face:1");

    // U1: a unique real loop is offered as one complete-boundary confirmation.
    {
        SelectionCandidate unique = base;
        unique.topologyOnly = true;
        SelectionChainDialog dialog;
        QVector<QVector3D> preview;
        bool previewClosed = false;
        dialog.topologyPreviewChanged = [&](const QVector<QVector3D> &points, bool closed) {
            preview = points;
            previewClosed = closed;
        };
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({unique});
        dialog.selectGeometry("region:A");
        auto *useWhole = dialog.findChild<QPushButton *>(
            QStringLiteral("selectionUseFullBoundary"));
        auto *adjust = dialog.findChild<QPushButton *>(
            QStringLiteral("selectionAdjustBoundary"));
        auto *rows = dialog.findChild<QListWidget *>(QStringLiteral("selectionEdges"));
        if (!useWhole || useWhole->isHidden() || !adjust || adjust->isHidden() ||
            !previewClosed || preview.size() != 4 || !rows || rows->count() != 0) {
            std::cerr << "U1 unique loop must preview the full real boundary before confirmation\n";
            std::_Exit(1);
        }
        useWhole->click();
        dialog.accept();
        if (!dialog.selection().closed ||
            !sameRefs(dialog.selection().orderedTopologyRefs,
                      {"edge:1", "edge:2", "edge:3", "edge:4"},
                      {true, true, true, true})) {
            std::cerr << "U1 one confirmation must adopt the complete real loop\n";
            std::_Exit(1);
        }
    }

    // U2: multiple loops stay ambiguous instead of silently choosing the first loop.
    {
        SelectionCandidate ambiguous = squareWithInnerLoop();
        ambiguous.topologyOnly = true;
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({ambiguous});
        dialog.selectGeometry(ambiguous.geometryId);
        auto *useWhole = dialog.findChild<QPushButton *>(
            QStringLiteral("selectionUseFullBoundary"));
        auto *rows = dialog.findChild<QListWidget *>(QStringLiteral("selectionEdges"));
        if ((useWhole && !useWhole->isHidden()) || !rows || rows->count() != 0) {
            std::cerr << "U2 inner/outer loops must require an explicit model range\n";
            std::_Exit(1);
        }
    }

    // U3: hovering previews only the nearest edge; clicking still commits the
    // continuous interval as one undoable operation.
    {
        SelectionCandidate unique = base;
        unique.topologyOnly = true;
        SelectionChainDialog dialog;
        QVector<QVector3D> preview;
        dialog.topologyHoverChanged = [&](const QVector<QVector3D> &points) {
            preview = points;
        };
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({unique});
        dialog.selectGeometry(unique.geometryId);
        auto *adjust = dialog.findChild<QPushButton *>(
            QStringLiteral("selectionAdjustBoundary"));
        if (!adjust) std::_Exit(1);
        adjust->click();
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.previewTopology("edge:3", 1, {0.1f, 10, 0}, 0.99);
        if (!sameRows(dialog, {"edge:1"}, {true}) ||
            preview != unique.topologyEdges.at(2).points) {
            std::cerr << "U3 hover must preview one edge without changing formal refs\n";
            std::_Exit(1);
        }
        dialog.selectTopology("edge:3", 1, {0.1f, 10, 0}, 0.99);
        if (!sameRows(dialog, {"edge:1", "edge:2", "edge:3"},
                      {true, true, true})) {
            std::cerr << "U3 clicking the previewed terminator must commit the interval\n";
            std::_Exit(1);
        }
        dialog.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
        if (!sameRows(dialog, {"edge:1"}, {true})) {
            std::cerr << "U3 one undo must remove the whole connected interval\n";
            std::_Exit(1);
        }
    }

    // U4: the status remains explicit and a unique next edge can be accepted directly.
    {
        SelectionCandidate unique = base;
        unique.topologyOnly = true;
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({unique});
        dialog.selectGeometry(unique.geometryId);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionAdjustBoundary"))->click();
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        auto *next = dialog.findChild<QPushButton *>(QStringLiteral("selectionAcceptNextEdge"));
        auto *state = dialog.findChild<QLabel *>(QStringLiteral("selectionChainStatus"));
        if (!next || next->isHidden() || !next->isEnabled() || !state ||
            !state->text().contains(QStringLiteral("未闭合")) ||
            !state->text().contains(QStringLiteral("已选 1 段")) ||
            !state->text().contains(QStringLiteral("下一步"))) {
            std::cerr << "U4 chain state and next action must remain visible\n";
            std::_Exit(1);
        }
        next->click();
        if (!sameRows(dialog, {"edge:1", "edge:2"}, {true, true})) {
            std::cerr << "U4 unique next edge must append without branch cycling\n";
            std::_Exit(1);
        }
    }

    // U5: Esc cancels only the candidate interval, while Ctrl+Z/Ctrl+Y operate on
    // actual selections as one session-scoped action.
    {
        SelectionCandidate unique = base;
        unique.topologyOnly = true;
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({unique});
        dialog.selectGeometry(unique.geometryId);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionAdjustBoundary"))->click();
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.previewTopology("edge:3", 1, {0.1f, 10, 0}, 0.99);
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&dialog, &escape);
        if (!sameRows(dialog, {"edge:1"}, {true})) {
            std::cerr << "U5 Esc must cancel the preview and preserve the formal chain\n";
            std::_Exit(1);
        }
        dialog.selectTopology("edge:2", 1, {10, 9.9f, 0}, 0.99);
        QKeyEvent undo(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
        QApplication::sendEvent(&dialog, &undo);
        if (!sameRows(dialog, {"edge:1"}, {true})) {
            std::cerr << "U5 Ctrl+Z must undo one actual selection\n";
            std::_Exit(1);
        }
        QKeyEvent redo(QEvent::KeyPress, Qt::Key_Y, Qt::ControlModifier);
        QApplication::sendEvent(&dialog, &redo);
        if (!sameRows(dialog, {"edge:1", "edge:2"}, {true, true})) {
            std::cerr << "U5 Ctrl+Y must redo one actual selection\n";
            std::_Exit(1);
        }
    }

    // U6: an incident edge outside the selected loop is not a legal continuation.
    {
        SelectionCandidate branch = base;
        branch.topologyOnly = true;
        branch.topologyEdges.append(
            edge("edge:5", "vertex:2", "vertex:5", {10, 0, 0}, {15, -5, 0}));
        branch.topologyVertices[1].incidentEdgeIds.append("edge:5");
        branch.topologyVertices.append(vertex("vertex:5", {15, -5, 0}, {"edge:5"}));
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({branch});
        dialog.selectGeometry(branch.geometryId);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionAdjustBoundary"))->click();
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        auto *state = dialog.findChild<QLabel *>(QStringLiteral("selectionChainStatus"));
        if (!state || !state->text().contains(QStringLiteral("edge:2")) ||
            state->text().contains(QStringLiteral("edge:5"))) {
            std::cerr << "U6 next-step prompt must name only the in-loop continuation\n";
            std::_Exit(1);
        }
        dialog.previewTopology("edge:5", 1, {10.1f, -0.1f, 0}, 0.01);
        if (!sameRows(dialog, {"edge:1"}, {true})) {
            std::cerr << "U6 branch hover must not change formal refs\n";
            std::_Exit(1);
        }
        dialog.selectTopology("edge:5", 1, {10.1f, -0.1f, 0}, 0.01);
        if (!sameRows(dialog, {"edge:1"}, {true})) {
            std::cerr << "U6 clicking an out-of-loop edge must preserve the chain\n";
            std::_Exit(1);
        }
    }

    // R1: Edge mode appends only a continuous edge and closes the same chain.
    {
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.selectTopology("edge:2", 1, {10, 9.9f, 0}, 0.99);
        dialog.accept();
        if (!sameRefs(dialog.selection().orderedTopologyRefs,
                      {"edge:1", "edge:2"}, {true, true})) {
            std::cerr << "R1 edge mode must append an adjacent edge\n";
            std::_Exit(1);
        }
    }
    {
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.selectTopology("edge:2", 1, {10, 9.9f, 0}, 0.99);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionReverse"))->click();
        dialog.selectTopology("edge:4", 1, {0, 0.1f, 0}, 0.99);
        if (!sameRows(dialog, {"edge:2", "edge:1", "edge:4"},
                      {false, false, false})) {
            std::cerr << "B1 reverse must remain an active chain for the next edge\n";
            std::_Exit(1);
        }
        dialog.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
        if (!sameRows(dialog, {"edge:2", "edge:1"}, {false, false})) {
            std::cerr << "B1 undo must restore the reversed active-chain intermediate state\n";
            std::_Exit(1);
        }
        dialog.selectTopology("edge:4", 1, {0, 0.1f, 0}, 0.99);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
        dialog.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
        if (!sameRows(dialog, {"edge:1", "edge:2"}, {true, true})) {
            std::cerr << "B1 second undo must restore the pre-reverse chain\n";
            std::_Exit(1);
        }
        dialog.findChild<QPushButton *>(QStringLiteral("selectionRedo"))->click();
        if (!sameRows(dialog, {"edge:2", "edge:1"}, {false, false})) {
            std::cerr << "B1 redo must restore the reversed active-chain state\n";
            std::_Exit(1);
        }
        dialog.selectTopology("edge:4", 1, {0, 0.1f, 0}, 0.99);
        dialog.accept();
        if (!sameRefs(dialog.selection().orderedTopologyRefs,
                      {"edge:2", "edge:1", "edge:4"},
                      {false, false, false})) {
            std::cerr << "B1 restored reversed chain must still accept a continuous edge\n";
            std::_Exit(1);
        }
    }
    {
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::Chain);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionPickStart"))->click();
        dialog.selectTopology("vertex:outside", 2, {50, 50, 0}, 0.0);
        dialog.accept();
        if (dialog.selection().orderedTopologyRefs.first().id != "edge:1" ||
            dialog.selection().startAnchor.topologyId != "vertex:1") {
            std::cerr << "R3 a vertex outside the current chain must not change its start\n";
            std::_Exit(1);
        }
    }

    // B2: a model mis-pick in another candidate must not replace an active draft.
    {
        const SelectionCandidate other = singleEdgeCandidate(
            "region:B", "face:B", "edge:B", "vertex:B1", "vertex:B2",
            {20, 20, 0}, {30, 20, 0});
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base, other});
        dialog.selectGeometry("region:A");
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.selectTopology("edge:2", 1, {10, 9.9f, 0}, 0.99);
        dialog.selectTopology("edge:B", 1, {20.1f, 20, 0}, 0.01);
        if (dialog.selectedCandidateGeometryId() != "region:A" ||
            !sameRows(dialog, {"edge:1", "edge:2"}, {true, true})) {
            std::cerr << "B2 cross-candidate edge mis-pick must preserve candidate and draft\n";
            std::_Exit(1);
        }
        dialog.accept();
        if (!sameRefs(dialog.selection().orderedTopologyRefs,
                      {"edge:1", "edge:2"}, {true, true})) {
            std::cerr << "B2 preserved edge draft must remain confirmable\n";
            std::_Exit(1);
        }
    }
    {
        const SelectionCandidate other = singleEdgeCandidate(
            "region:B", "face:B", "edge:B", "vertex:B1", "vertex:B2",
            {20, 20, 0}, {30, 20, 0});
        SelectionChainDialog dialog;
        int pickMode = -1;
        dialog.pickModeChanged = [&](int mode) { pickMode = mode; };
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base, other});
        dialog.selectGeometry("region:A");
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.selectTopology("edge:2", 1, {10, 9.9f, 0}, 0.99);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionPickStart"))->click();
        dialog.selectTopology("vertex:B1", 2, {20, 20, 0}, 0.0);
        if (dialog.selectedCandidateGeometryId() != "region:A" || pickMode != 2 ||
            !sameRows(dialog, {"edge:1", "edge:2"}, {true, true})) {
            std::cerr << "B2 cross-candidate vertex mis-pick must preserve start-edit session\n";
            std::_Exit(1);
        }
        dialog.selectTopology("vertex:3", 2, {10, 10, 0}, 0.0);
        dialog.selectTopology("edge:4", 1, {0, 0.1f, 0}, 0.99);
        dialog.accept();
        if (!sameRefs(dialog.selection().orderedTopologyRefs,
                      {"edge:2", "edge:1", "edge:4"},
                      {false, false, false})) {
            std::cerr << "B2 preserved start-edit session must continue after a valid endpoint\n";
            std::_Exit(1);
        }
    }
    {
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.selectTopology("edge:2", 1, {10, 9.9f, 0}, 0.99);
        dialog.selectTopology("edge:3", 1, {0.1f, 10, 0}, 0.99);
        dialog.selectTopology("edge:4", 1, {0, 0.1f, 0}, 0.99);
        dialog.accept();
        if (!dialog.selection().closed ||
            !sameRefs(dialog.selection().orderedTopologyRefs,
                      {"edge:1", "edge:2", "edge:3", "edge:4"},
                      {true, true, true, true})) {
            std::cerr << "R1 adjacent edge clicks must close one continuous chain\n";
            std::_Exit(1);
        }
    }
    {
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.selectTopology("edge:2", 1, {10, 9.9f, 0}, 0.99);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionPickStart"))->click();
        dialog.selectTopology("vertex:2", 2, {10, 0, 0}, 0.0);
        dialog.accept();
        if (!sameRefs(dialog.selection().orderedTopologyRefs,
                      {"edge:1", "edge:2"}, {true, true}) ||
            dialog.selection().startAnchor.topologyId != "vertex:1") {
            std::cerr << "R3 an open-chain interior vertex must not become the start\n";
            std::_Exit(1);
        }
    }
    {
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.selectTopology("edge:2", 1, {10, 9.9f, 0}, 0.99);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionFinishChain"))->click();
        dialog.findChild<QPushButton *>(QStringLiteral("selectionReverse"))->click();
        dialog.selectTopology("edge:3", 1, {10, 10, 0}, 0.0);
        dialog.accept();
        if (!sameRefs(dialog.selection().orderedTopologyRefs, {"edge:3"}, {true})) {
            std::cerr << "R1 clicking after Finish must start a new edge chain\n";
            std::_Exit(1);
        }
    }
    {
        SelectionCandidate branch = base;
        branch.topologyEdges.append(
            edge("edge:5", "vertex:2", "vertex:5", {10, 0, 0}, {15, -5, 0}));
        branch.topologyVertices[1].incidentEdgeIds.append("edge:5");
        branch.topologyVertices.append(vertex("vertex:5", {15, -5, 0}, {"edge:5"}));
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({branch});
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        auto *edges = dialog.findChild<QListWidget *>(QStringLiteral("selectionEdges"));
        dialog.findChild<QPushButton *>(QStringLiteral("selectionNextBranch"))->click();
        if (edges->count() != 1) {
            std::cerr << "R1 branch navigation must preview without changing formal refs\n";
            std::_Exit(1);
        }
        dialog.findChild<QPushButton *>(QStringLiteral("selectionSwitchBranch"))->click();
        dialog.accept();
        if (dialog.selection().orderedTopologyRefs.size() != 2 ||
            dialog.selection().orderedTopologyRefs.last().id != "edge:2") {
            std::cerr << "R1 branch confirmation must stay in the selected loop\n";
            std::_Exit(1);
        }
    }
    {
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:2", 1, {10, 9.9f, 0}, 0.99);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.accept();
        if (!sameRefs(dialog.selection().orderedTopologyRefs,
                      {"edge:2", "edge:1"}, {false, false})) {
            std::cerr << "R1 reverse edge traversal must follow the current endpoint\n";
            std::_Exit(1);
        }
    }
    {
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.selectTopology("edge:3", 1, {10, 10, 0}, 0.0);
        dialog.accept();
        if (!sameRefs(dialog.selection().orderedTopologyRefs, {"edge:1"}, {true})) {
            std::cerr << "R1 disconnected edge must preserve the existing draft\n";
            std::_Exit(1);
        }
    }

    // R2: partial-chain undo restores its private start-session state.
    {
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::PartialChain);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
        dialog.selectTopology("edge:3", 1, {10, 10, 0}, 0.0);
        dialog.selectTopology("edge:4", 1, {0, 0.1f, 0}, 0.99);
        dialog.accept();
        if (!sameRefs(dialog.selection().orderedTopologyRefs,
                      {"edge:3", "edge:4"}, {true, true})) {
            std::cerr << "R2 undo must discard the old partial-chain start\n";
            std::_Exit(1);
        }
    }
    {
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::PartialChain);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
        dialog.selectTopology("edge:3", 1, {10, 10, 0}, 0.0);
        dialog.accept();
        if (!sameRefs(dialog.selection().orderedTopologyRefs,
                      {"edge:1", "edge:2", "edge:3"}, {true, true, true})) {
            std::cerr << "R2 undoing a mode switch must restore the partial-chain session\n";
            std::_Exit(1);
        }
    }
    {
        SelectionChainDialog dialog;
        int pickMode = -1;
        dialog.pickModeChanged = [&](int mode) { pickMode = mode; };
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::Chain);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionPickStart"))->click();
        if (pickMode != 2) {
            std::cerr << "R2 start-point editing must switch the viewport to vertex mode\n";
            std::_Exit(1);
        }
        setMode(dialog, ChainSelectionMode::Face);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
        if (pickMode != 2) {
            std::cerr << "R2 undo must restore the vertex picking mode\n";
            std::_Exit(1);
        }
        dialog.findChild<QPushButton *>(QStringLiteral("selectionRedo"))->click();
        if (pickMode != 0) {
            std::cerr << "R2 redo must restore the face picking mode\n";
            std::_Exit(1);
        }
    }

    // R3: changing a closed-loop start rotates real refs and preview together.
    {
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::Chain);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionPickStart"))->click();
        dialog.selectTopology("vertex:3", 2, {10, 10, 0}, 0.0);
        dialog.accept();
        const SelectionChain selection = dialog.selection();
        if (!sameRefs(selection.orderedTopologyRefs,
                      {"edge:3", "edge:4", "edge:1", "edge:2"},
                      {true, true, true, true}) ||
            selection.startAnchor.topologyId != "vertex:3" ||
            selection.startAnchor.hasCurveParameter ||
            dialog.selectedPoints().isEmpty() || dialog.selectedPoints().first() != QVector3D(10, 10, 0)) {
            std::cerr << "R3 closed-loop start must rotate refs, anchor and preview\n";
            std::_Exit(1);
        }
    }
    {
        SelectionChainDialog dialog;
        int pickMode = -1;
        dialog.pickModeChanged = [&](int mode) { pickMode = mode; };
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::Chain);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionPickStart"))->click();
        dialog.selectTopology("vertex:3", 2, {10, 10, 0}, 0.0);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
        if (pickMode != 2 ||
            !sameRows(dialog, {"edge:1", "edge:2", "edge:3", "edge:4"},
                      {true, true, true, true})) {
            std::cerr << "R3 closed-start undo must restore refs and vertex-pick state\n";
            std::_Exit(1);
        }
        dialog.findChild<QPushButton *>(QStringLiteral("selectionRedo"))->click();
        if (pickMode != 1 ||
            !sameRows(dialog, {"edge:3", "edge:4", "edge:1", "edge:2"},
                      {true, true, true, true})) {
            std::cerr << "R3 closed-start redo must restore the rotated intermediate state\n";
            std::_Exit(1);
        }
        dialog.findChild<QPushButton *>(QStringLiteral("selectionReverse"))->click();
        dialog.accept();
        const SelectionChain selection = dialog.selection();
        if (!sameRefs(selection.orderedTopologyRefs,
                      {"edge:2", "edge:1", "edge:4", "edge:3"},
                      {false, false, false, false}) ||
            selection.startAnchor.topologyId != "vertex:3" ||
            dialog.selectedPoints().isEmpty() || dialog.selectedPoints().first() != QVector3D(10, 10, 0)) {
            std::cerr << "R3 reverse and undo/redo must preserve the selected loop start\n";
            std::_Exit(1);
        }
    }
    {
        SelectionChainDialog dialog;
        int pickMode = -1;
        dialog.pickModeChanged = [&](int mode) { pickMode = mode; };
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.selectTopology("edge:2", 1, {10, 9.9f, 0}, 0.99);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionPickStart"))->click();
        dialog.selectTopology("vertex:3", 2, {10, 10, 0}, 0.0);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
        if (pickMode != 2 || !sameRows(dialog, {"edge:1", "edge:2"}, {true, true})) {
            std::cerr << "B1 endpoint-edit undo must restore the pending vertex-pick state\n";
            std::_Exit(1);
        }
        dialog.findChild<QPushButton *>(QStringLiteral("selectionRedo"))->click();
        if (pickMode != 1 || !sameRows(dialog, {"edge:2", "edge:1"}, {false, false})) {
            std::cerr << "B1 endpoint-edit redo must restore the reversed chain state\n";
            std::_Exit(1);
        }
        dialog.selectTopology("edge:4", 1, {0, 0.1f, 0}, 0.99);
        dialog.accept();
        const SelectionChain selection = dialog.selection();
        if (!selection.reversed ||
            !sameRefs(selection.orderedTopologyRefs,
                      {"edge:2", "edge:1", "edge:4"}, {false, false, false}) ||
            selection.startAnchor.topologyId != "vertex:3" ||
            selection.endAnchor.topologyId != "vertex:4") {
            std::cerr << "B1 open-chain endpoint change must remain appendable\n";
            std::_Exit(1);
        }
    }

    // R4: an explicit candidate/loop context wins over candidate list order.
    {
        SelectionCandidate a = square("region:A", "face:1");
        SelectionCandidate b = square("region:B", "face:2");
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({a, b});
        dialog.selectGeometry("region:B");
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.accept();
        if (dialog.selectedCandidateGeometryId() != "region:B") {
            std::cerr << "R4 explicit candidate context must survive a shared-edge click\n";
            std::_Exit(1);
        }
    }
    {
        SelectionCandidate a = square("region:A", "face:1");
        SelectionCandidate b = square("region:B", "face:2");
        QString ids[2];
        const QList<QList<SelectionCandidate>> orders{{a, b}, {b, a}};
        for (int i = 0; i < 2; ++i) {
            SelectionChainDialog dialog;
            dialog.setContext("source", "setup", "G54");
            dialog.setCandidates(orders.at(i));
            dialog.selectGeometry("region:B");
            setMode(dialog, ChainSelectionMode::Edge);
            dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
            dialog.accept();
            ids[i] = dialog.selection().id;
            if (dialog.selection().boundaryLoops.first().faceId != "face:2") {
                std::cerr << "R4 shared-edge result changed with candidate order\n";
                std::_Exit(1);
            }
        }
        if (ids[0] != ids[1]) {
            std::cerr << "R4 stable selection identity must ignore candidate list order\n";
            std::_Exit(1);
        }
    }

    // R4: without context a shared edge stays pending until a stable face/loop is confirmed.
    {
        SelectionCandidate a = square("region:A", "face:1");
        SelectionCandidate b = square("region:B", "face:2");
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({a, b});
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        auto *contexts = dialog.findChild<QComboBox *>(QStringLiteral("selectionTopologyContext"));
        auto *confirm = dialog.findChild<QPushButton *>(QStringLiteral("selectionConfirmTopologyContext"));
        if (!contexts || !confirm || contexts->count() != 2 ||
            !dialog.selection().orderedTopologyRefs.isEmpty()) {
            std::cerr << "R4 ambiguous shared edge must expose two pending contexts\n";
            std::_Exit(1);
        }
        contexts->setCurrentIndex(1);
        QMetaObject::invokeMethod(contexts, "activated", Q_ARG(int, 1));
        confirm->click();
        dialog.accept();
        if (dialog.selection().boundaryLoops.isEmpty() ||
            dialog.selection().boundaryLoops.first().faceId != "face:2") {
            std::cerr << "R4 confirmed context must retain the chosen face/loop identity\n";
            std::_Exit(1);
        }
    }
    {
        SelectionCandidate ambiguous = square("region:A", "face:1");
        MeshBoundaryLoop second = ambiguous.boundaryLoops.first();
        second.faceId = "face:2";
        second.faceIndex = 2;
        ambiguous.faceIds.append("face:2");
        ambiguous.boundaryLoops.append(second);
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({ambiguous});
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        auto *contexts = dialog.findChild<QComboBox *>(QStringLiteral("selectionTopologyContext"));
        if (!contexts || contexts->count() != 2) {
            std::cerr << "R4 shared edge in two loops of one candidate must remain ambiguous\n";
            std::_Exit(1);
        }
    }
    {
        const SelectionCandidate b = singleEdgeCandidate(
            "region:B", "face:B", "edge:shared", "vertex:S1", "vertex:S2",
            {20, 20, 0}, {30, 20, 0});
        const SelectionCandidate c = singleEdgeCandidate(
            "region:C", "face:C", "edge:shared", "vertex:S1", "vertex:S2",
            {20, 20, 0}, {30, 20, 0});
        SelectionChainDialog dialog;
        QVector<QVector3D> previewPoints;
        dialog.topologyPreviewChanged = [&](const QVector<QVector3D> &points, bool) {
            previewPoints = points;
        };
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base, b, c});
        dialog.selectGeometry("region:A");
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.selectTopology("edge:shared", 1, {20.1f, 20, 0}, 0.01);
        auto *contexts = dialog.findChild<QComboBox *>(QStringLiteral("selectionTopologyContext"));
        auto *confirm = dialog.findChild<QPushButton *>(
            QStringLiteral("selectionConfirmTopologyContext"));
        dialog.accept();
        if (dialog.result() == QDialog::Accepted || !contexts || contexts->isHidden() ||
            !confirm || confirm->isHidden() || previewPoints.size() != 2 ||
            previewPoints.first() != QVector3D(20, 20, 0) ||
            previewPoints.last() != QVector3D(30, 20, 0) ||
            dialog.selectedCandidateGeometryId() != "region:A" ||
            !sameRows(dialog, {"edge:1"}, {true})) {
            std::cerr << "R4 an unconfirmed ambiguity preview must block Accept and preserve the draft\n";
            std::_Exit(1);
        }
    }

    // B3: a partial-chain start belongs to one exact candidate loop, not only its face.
    {
        const SelectionCandidate withHole = squareWithInnerLoop();
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({withHole});
        setMode(dialog, ChainSelectionMode::PartialChain);
        dialog.selectTopology("edge:4", 1, {0, 9.9f, 0}, 0.01);
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
        if (!sameRows(dialog, {"edge:4"}, {true}) ||
            dialog.findChild<QComboBox *>(QStringLiteral("selectionMode"))->currentIndex() !=
                static_cast<int>(ChainSelectionMode::PartialChain)) {
            std::cerr << "B3 undo must restore the exact partial-loop start session\n";
            std::_Exit(1);
        }
        dialog.findChild<QPushButton *>(QStringLiteral("selectionRedo"))->click();
        if (dialog.findChild<QComboBox *>(QStringLiteral("selectionMode"))->currentIndex() !=
            static_cast<int>(ChainSelectionMode::Edge)) {
            std::cerr << "B3 redo must restore the post-switch state\n";
            std::_Exit(1);
        }
        dialog.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
        dialog.selectTopology("edge:5", 1, {3.1f, 3, 0}, 0.01);
        if (!sameRows(dialog, {"edge:4"}, {true})) {
            std::cerr << "B3 same-face inner-loop endpoint must preserve the outer-loop draft\n";
            std::_Exit(1);
        }
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.accept();
        if (!sameRefs(dialog.selection().orderedTopologyRefs,
                      {"edge:4", "edge:1"}, {true, true})) {
            std::cerr << "B3 restored outer-loop partial chain must finish on its own loop\n";
            std::_Exit(1);
        }
    }

    // Continuity: a malformed loop must never be previewed or accepted as a fake segment.
    {
        SelectionCandidate broken = base;
        broken.geometryId = "region:broken";
        broken.boundaryLoops[0].orderedEdgeIds =
            QStringList{"edge:1", "edge:3", "edge:2", "edge:4"};
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({broken});
        setMode(dialog, ChainSelectionMode::Chain);
        dialog.selectTopology("edge:1", 1, {0.1f, 0, 0}, 0.01);
        dialog.accept();
        if (dialog.result() == QDialog::Accepted) {
            std::cerr << "disconnected topology refs must not be accepted as a straight join\n";
            std::_Exit(1);
        }
    }

    // Re-clicking a chain endpoint removes that edge; undo restores its direction.
    {
        SelectionChainDialog dialog;
        dialog.setContext("source", "setup", "G54");
        dialog.setCandidates({base});
        setMode(dialog, ChainSelectionMode::Edge);
        dialog.selectTopology("edge:1", 1, {0.1f,0,0}, 0.01);
        dialog.selectTopology("edge:2", 1, {10,0.1f,0}, 0.01);
        dialog.selectTopology("edge:1", 1, {0.1f,0,0}, 0.01);
        if (!sameRows(dialog, {"edge:2"}, {true})) {
            std::cerr << "FAIL re-click A must remove A and retain adjacent B\n";
            std::_Exit(1);
        }
        dialog.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
        if (!sameRows(dialog, {"edge:1","edge:2"}, {true,true})) {
            std::cerr << "FAIL undo must restore A/B and their direction\n";
            std::_Exit(1);
        }
        dialog.selectTopology("edge:3", 1, {9.9f,10,0}, 0.01);
        dialog.selectTopology("edge:2", 1, {10,0.1f,0}, 0.01);
        if (!sameRows(dialog, {"edge:1","edge:2","edge:3"}, {true,true,true})) {
            std::cerr << "FAIL removing an interior edge must not create a disconnected chain\n";
            std::_Exit(1);
        }
    }
    std::cout << "PASS R1-R4 and B1-B3 selection review regressions\n";
    std::cout.flush();
    std::_Exit(0);
}
