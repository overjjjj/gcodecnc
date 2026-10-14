#include <QApplication>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdlib>
#include <limits>
#include <iostream>
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include "../src/core/ProjectManager.h"
#include "../src/app/MainWindow.h"
#include "../src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"

static void require(bool value, const char *message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

static SelectionCandidate square()
{
    SelectionCandidate candidate;
    candidate.geometryId = "region:1";
    candidate.faceIds = QStringList{"face:1"};
    candidate.closed = true;
    candidate.topologyOnly = true;
    candidate.outerLoopPointCount = 4;
    const QVector<QVector3D> points{{0,0,0},{10,0,0},{10,10,0},{0,10,0}};
    MeshBoundaryLoop loop;
    loop.faceId = "face:1";
    loop.faceIndex = 1;
    loop.outer = true;
    for (int i = 0; i < 4; ++i) {
        MeshPickEdge edge;
        edge.id = QString("edge:%1").arg(i+1);
        edge.firstVertexId = QString("vertex:%1").arg(i+1);
        edge.lastVertexId = QString("vertex:%1").arg((i+1)%4+1);
        edge.points = {points[i], points[(i+1)%4]};
        edge.parameters = {0,1};
        edge.adjacentFaceIndices = {1};
        candidate.topologyEdges.append(edge);
        loop.orderedEdgeIds.append(edge.id);
        loop.orderedForward.append(true);
    }
    candidate.boundaryLoops = {loop};
    return candidate;
}

static void configure(SelectionChainDialog &dialog, const SelectionCandidate &candidate)
{
    dialog.setContext("source", "setup", "G54");
    dialog.setCandidates({candidate});
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    SelectionChainDialog dialog;
    auto *last = dialog.findChild<QPushButton *>("selectionLast");
    if (!last || last->isEnabled()) {
        std::cerr << "Previous selection must have a disabled entry before any confirmation\n";
        return 1;
    }
    const auto candidate = square();
    configure(dialog, candidate);
    dialog.selectGeometry(candidate.geometryId);
    auto *full = dialog.findChild<QPushButton *>("selectionUseFullBoundary");
    require(full && full->isEnabled(), "Unique candidate must offer its complete boundary");
    full->click();
    dialog.findChild<QPushButton *>("selectionReverse")->click();
    dialog.accept();
    const SelectionChain previous = dialog.selection();
    const auto points = dialog.selectedPoints();
    require(previous.explicitUserSelection && previous.candidateGeometryId == candidate.geometryId,
            "Confirmation must retain its candidate provenance");

    SelectionChainDialog restored;
    QVector<QVector3D> preview;
    restored.previewChanged = [&](const QVector<QVector3D> &value) { preview = value; };
    configure(restored, candidate);
    auto other = candidate;
    other.geometryId = "region:2";
    restored.setCandidates({other, candidate});
    restored.setPreviousSelection(previous);
    restored.findChild<QPushButton *>("selectionLast")->click();
    require(restored.selection().orderedGeometryIds.isEmpty(), "Restore must wait for confirmation");
    require(restored.selectedCandidateGeometryId() == candidate.geometryId && preview == points,
            "Restore must resolve by identity and retain reversed ordered points");
    restored.findChild<QPushButton *>("selectionUndo")->click();
    require(restored.selectedCandidateGeometryId().isEmpty(), "Restoration must be undoable");
    restored.findChild<QPushButton *>("selectionLast")->click();
    restored.accept();
    require(restored.selection() == previous, "Confirmed restoration must retain all evidence");
    require(restored.selectedPoints() == points, "Confirmed restoration must retain ordered points");

    SelectionChainDialog adjusting;
    configure(adjusting, candidate);
    adjusting.setPreviousSelection(previous);
    adjusting.selectTopology("edge:1", 1, {1,0,0}, 0.1);
    adjusting.findChild<QPushButton *>("selectionAdjustBoundary")->click();
    adjusting.findChild<QPushButton *>("selectionLast")->click();
    require(adjusting.findChild<QLabel *>("selectionStatus")->text().contains(QString::fromUtf8("请先完成")),
            "Restoring during boundary adjustment must be blocked without discarding the session");
    adjusting.selectTopology("edge:2", 1, {10,1,0}, 0.1);
    adjusting.findChild<QPushButton *>("selectionFinishChain")->click();
    adjusting.findChild<QPushButton *>("selectionLast")->click();
    std::cout << "After finishing adjustment: " << adjusting.findChild<QLabel *>("selectionStatus")->text().toStdString() << '\n';
    adjusting.accept();
    require(adjusting.selection() == previous, "Completing boundary adjustment must allow restoration again");

    for (const auto mode : {ChainSelectionMode::Face, ChainSelectionMode::PartialChain}) {
        SelectionChainDialog source;
        configure(source, candidate);
        auto *combo = source.findChild<QComboBox *>("selectionMode");
        combo->setCurrentIndex(int(mode));
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, int(mode)));
        if (mode == ChainSelectionMode::Face) source.selectTopology("face:1", 0, {5,5,0}, 0);
        else {
            source.selectTopology("edge:1", 1, {1,0,0}, 0.1);
            source.selectTopology("edge:2", 1, {10,1,0}, 0.1);
        }
        source.accept();
        require(source.selection().explicitUserSelection, "Mode fixture must be explicitly confirmed");
        SelectionChainDialog target;
        configure(target, candidate);
        target.setPreviousSelection(source.selection());
        target.findChild<QPushButton *>("selectionLast")->click();
        target.accept();
        require(target.selection() == source.selection() && target.selectedPoints() == source.selectedPoints(),
                "Face and partial-chain restoration must preserve complete evidence and points");
    }

    auto rejected = [&](SelectionChain saved, SelectionCandidate current, const QString &source,
                        const QString &setup, const QString &wcs, QVector3D axis) {
        SelectionChainDialog check;
        check.setContext(source, setup, wcs, axis);
        check.setCandidates({current});
        check.setPreviousSelection(saved);
        check.findChild<QPushButton *>("selectionLast")->click();
        require(check.selectedCandidateGeometryId().isEmpty(), "Invalid history must not alter draft");
    };
    rejected(previous, candidate, "changed", "setup", "G54", {0,0,1});
    rejected(previous, candidate, "source", "changed", "G54", {0,0,1});
    rejected(previous, candidate, "source", "setup", "G55", {0,0,1});
    rejected(previous, candidate, "", "setup", "G54", {0,0,1});
    rejected(previous, candidate, "source", "setup", "G54", {0,0,-1});
    auto broken = candidate;
    broken.topologyEdges[0].firstVertexId = "vertex:gone";
    rejected(previous, broken, "source", "setup", "G54", {0,0,1});
    broken = candidate;
    broken.boundaryLoops[0].outer = false;
    rejected(previous, broken, "source", "setup", "G54", {0,0,1});
    auto stale = previous;
    stale.startAnchor.point += QVector3D(1,0,0);
    rejected(stale, candidate, "source", "setup", "G54", {0,0,1});
    stale = previous;
    stale.startAnchor.point.setX(std::numeric_limits<float>::quiet_NaN());
    rejected(stale, candidate, "source", "setup", "G54", {0,0,1});
    stale = previous;
    stale.explicitUserSelection = false;
    rejected(stale, candidate, "source", "setup", "G54", {0,0,1});
    SelectionChainDialog strategy;
    configure(strategy, candidate);
    strategy.setPreviousSelection(previous);
    strategy.acceptanceError = [](const SelectionChain &, const QString &) { return QString("unsupported region"); };
    strategy.findChild<QPushButton *>("selectionLast")->click();
    require(strategy.selectedCandidateGeometryId().isEmpty(), "Restore must enforce current strategy acceptance");

    ProjectManager project;
    MachiningOperation first, second;
    first.id = "first";
    first.selectionEvidence = previous;
    second = first;
    second.id = "second";
    SelectionChainController newer(previous);
    newer.setMachiningSide(ChainMachiningSide::Inside);
    second.selectionEvidence = newer.commit();
    project.setOperations({first});
    project.setOperations({first, second});
    project.setOperations({second, first});
    require(project.lastEstablishedSelection() == second.selectionEvidence,
            "Reordering must not rewrite creation history");
    const auto fingerprint = ProgramSnapshotFingerprint::calculate({first}, {first.id});
    auto changed = first;
    changed.selectionEvidence.candidateGeometryId = "different-candidate";
    require(ProgramSnapshotFingerprint::calculate({changed}, {first.id}) != fingerprint,
            "Candidate provenance changes must invalidate the program fingerprint");
    SelectionChainDialog cancelled;
    configure(cancelled, candidate);
    cancelled.selectTopology("edge:1", 1, {1,0,0}, 0.1);
    cancelled.accept();
    require(project.lastEstablishedSelection() == second.selectionEvidence,
            "Geometry confirmation without establishing an operation must not change history");
    QTemporaryDir temporary;
    const QString path = temporary.filePath("project.cnext");
    require(project.saveToFile(path), "Save history");
    ProjectManager loaded;
    require(loaded.loadFromFile(path), "Reload history");
    require(loaded.lastEstablishedSelection() == second.selectionEvidence, "History and provenance must survive reload");
    require(ProgramSnapshotFingerprint::calculate(loaded.operations(), {first.id}) == fingerprint,
            "Reload must preserve the selection program fingerprint");
    QFile legacyFile(path);
    require(legacyFile.open(QIODevice::ReadOnly), "Read saved project");
    auto legacy = QJsonDocument::fromJson(legacyFile.readAll()).object();
    legacyFile.close();
    legacy.remove("operationCreationOrder");
    require(legacyFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "Write legacy project fixture");
    legacyFile.write(QJsonDocument(legacy).toJson());
    legacyFile.close();
    ProjectManager oldProject;
    require(oldProject.loadFromFile(path), "Load old project without history");
    oldProject.setOperations(oldProject.operations());
    require(!oldProject.lastEstablishedSelection().explicitUserSelection,
            "Repeated updates of legacy operations must not invent creation history");
    auto copy = first;
    copy.id = "copy";
    auto copied = loaded.operations();
    copied.append(copy);
    loaded.setOperations(copied);
    require(loaded.lastEstablishedSelection() == copy.selectionEvidence,
            "A newly established copy is the latest operation even though it retains source evidence");
    loaded.setOperations({second, first});
    SelectionChainDialog reopened;
    configure(reopened, candidate);
    reopened.setPreviousSelection(loaded.lastEstablishedSelection());
    reopened.findChild<QPushButton *>("selectionLast")->click();
    reopened.accept();
    require(reopened.selection() == second.selectionEvidence, "Saved operation must restore after project reload");
    loaded.setOperations({first});
    require(loaded.lastEstablishedSelection() == first.selectionEvidence, "Deletion must fall back to preceding established operation");
    loaded.setOperations({});
    require(!loaded.lastEstablishedSelection().explicitUserSelection, "Empty project must have no previous selection");
    if (argc > 1) {
        MainWindow styleParent;
        styleParent.setAttribute(Qt::WA_DontShowOnScreen);
        SelectionChainDialog screenshot(&styleParent);
        screenshot.setAttribute(Qt::WA_DontShowOnScreen);
        configure(screenshot, candidate);
        screenshot.setPreviousSelection(previous);
        screenshot.findChild<QPushButton *>("selectionLast")->click();
        auto *confirm = screenshot.findChild<QPushButton *>("selectionNext");
        std::cout << "Before layout: text=" << confirm->text().toStdString()
                  << " width=" << confirm->width() << " hint=" << confirm->sizeHint().width()
                  << " minimumHint=" << confirm->minimumSizeHint().width() << '\n';
        screenshot.resize(380, 720);
        screenshot.show();
        QApplication::processEvents();
        std::cout << "After layout: width=" << confirm->width() << " hint=" << confirm->sizeHint().width()
                  << " minimumHint=" << confirm->minimumSizeHint().width() << '\n';
        require(confirm->width() >= confirm->sizeHint().width(), "Native confirmation button must fit its text");
        require(screenshot.grab().save(QString::fromLocal8Bit(argv[1])), "Save native hidden selection screenshot");
    }
    std::cout << "Previous operation selection restoration and persistence passed\n";
    return 0;
}
