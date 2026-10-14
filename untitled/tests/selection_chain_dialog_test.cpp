#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include <QApplication>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <iostream>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--snapshot"))) {
        app.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
    }
    SelectionChainDialog dialog;
    if (dialog.isModal() || dialog.width() > 480) { std::cerr << "selection window must remain narrow and modeless\n"; return 1; }
    for (const char *name : {"selectionSource", "selectionMode", "selectionSide", "selectionStart", "selectionSort"}) {
        if (!dialog.findChild<QComboBox *>(QString::fromLatin1(name))) {
            std::cerr << "missing common selection control: " << name << '\n';
            return 1;
        }
    }
    for (const char *name : {"selectionReverse", "selectionUndo", "selectionRedo", "selectionPreviousBranch", "selectionNextBranch", "selectionSwitchBranch"}) {
        if (!dialog.findChild<QPushButton *>(QString::fromLatin1(name))) {
            std::cerr << "missing chain action: " << name << '\n';
            return 1;
        }
    }
    dialog.accept();
    if (dialog.result() == QDialog::Accepted || dialog.selection().explicitUserSelection) {
        std::cerr << "empty selection must not be confirmable\n";
        return 1;
    }
    SelectionCandidate candidate;
    candidate.geometryId = QStringLiteral("face:1");
    candidate.closed = true;
    candidate.outerLoopPointCount = 4;
    candidate.points = {QVector3D(0, 0, 0), QVector3D(10, 0, 0),
                        QVector3D(10, 10, 0), QVector3D(0, 10, 0)};
    dialog.setContext(QStringLiteral("source-v1"), QStringLiteral("setup-v1"), QStringLiteral("G55"));
    dialog.setCandidates({candidate});
    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("selectionCandidates"));
    list->setCurrentRow(0);
    if (dialog.selection().explicitUserSelection) return 1;
    dialog.findChild<QPushButton *>(QStringLiteral("selectionReverse"))->click();
    dialog.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
    dialog.findChild<QPushButton *>(QStringLiteral("selectionRedo"))->click();
    auto *start = dialog.findChild<QComboBox *>(QStringLiteral("selectionStart"));
    start->setCurrentIndex(2);
    QMetaObject::invokeMethod(start, "activated", Q_ARG(int, 2));
    if (app.arguments().contains(QStringLiteral("--snapshot"))) {
        dialog.show();
        app.processEvents();
        if (!dialog.grab().save(QStringLiteral("debug/selection_chain_dialog.png"))) return 1;
    }
    dialog.accept();
    const auto confirmed = dialog.selection();
    if (!confirmed.explicitUserSelection || !confirmed.reversed ||
        confirmed.startPoint != candidate.points.at(2) ||
        confirmed.coordinateSystemId != QStringLiteral("G55") ||
        confirmed.setupFingerprint != QStringLiteral("setup-v1")) return 1;
    if (dialog.selectedPoints().first() != candidate.points.at(2) ||
        dialog.selectedPoints().at(1) != candidate.points.at(1)) return 1;
    dialog.reject();
    if (dialog.selection().explicitUserSelection) return 1;

    dialog.setCandidates({candidate});
    list->setCurrentRow(0);
    auto *mode = dialog.findChild<QComboBox *>(QStringLiteral("selectionMode"));
    mode->setCurrentIndex(static_cast<int>(ChainSelectionMode::Face));
    QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, static_cast<int>(ChainSelectionMode::Face)));
    dialog.accept();
    if (dialog.selection().orderedGeometryIds != QStringList{candidate.geometryId}) {
        std::cerr << "face mode must select the face identity, not all boundary edges\n";
        return 1;
    }
    dialog.reject();

    dialog.setCandidates({candidate});
    list->setCurrentRow(0);
    mode->setCurrentIndex(static_cast<int>(ChainSelectionMode::Chain));
    QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, static_cast<int>(ChainSelectionMode::Chain)));
    auto *sort = dialog.findChild<QComboBox *>(QStringLiteral("selectionSort"));
    sort->setCurrentIndex(static_cast<int>(ChainSortStrategy::UnidirectionalX));
    QMetaObject::invokeMethod(sort, "activated", Q_ARG(int, static_cast<int>(ChainSortStrategy::UnidirectionalX)));
    dialog.accept();
    if (dialog.result() == QDialog::Accepted) {
        std::cerr << "sorting must not confirm disconnected boundary edges\n";
        return 1;
    }
    dialog.reject();

    SelectionCandidate branch = candidate;
    branch.geometryId = QStringLiteral("face:2");
    candidate.branchIds = QStringList{candidate.geometryId, branch.geometryId};
    branch.branchIds = candidate.branchIds;
    dialog.setCandidates({candidate, branch});
    list->setCurrentRow(0);
    dialog.findChild<QPushButton *>(QStringLiteral("selectionNextBranch"))->click();
    dialog.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
    dialog.accept();
    if (dialog.selection().orderedGeometryIds.isEmpty() ||
        !dialog.selection().orderedGeometryIds.first().startsWith(candidate.geometryId)) {
        std::cerr << "undo must restore the candidate before branch switching\n";
        return 1;
    }
    dialog.reject();

    dialog.setCandidates({candidate});
    list->setCurrentRow(0);
    dialog.setContext(QStringLiteral("source-v2"), QStringLiteral("setup-v2"), QStringLiteral("G56"));
    dialog.accept();
    if (dialog.result() == QDialog::Accepted) {
        std::cerr << "changing context must not restamp an existing selection\n";
        return 1;
    }
    list->setCurrentRow(-1);
    list->setCurrentRow(0);
    dialog.accept();
    if (dialog.result() == QDialog::Accepted) {
        std::cerr << "reselecting an old candidate must not refresh its provenance\n";
        return 1;
    }
    dialog.reject();

    candidate.surfaceNormal = QVector3D(0, 0, 1);
    for (int sourceIndex = 0; sourceIndex < 3; ++sourceIndex) {
        SelectionCandidate sourceCandidate = candidate;
        sourceCandidate.source = static_cast<ChainGeometrySource>(sourceIndex);
        dialog.setCandidates({sourceCandidate});
        auto *source = dialog.findChild<QComboBox *>(QStringLiteral("selectionSource"));
        source->setCurrentIndex(sourceIndex);
        QMetaObject::invokeMethod(source, "activated", Q_ARG(int, sourceIndex));
        if (list->count() != 1) return 1;
        list->setCurrentRow(0);
        dialog.accept();
        if (!dialog.selection().explicitUserSelection || dialog.selection().geometrySource != sourceCandidate.source) return 1;
        dialog.reject();
    }
    auto *source = dialog.findChild<QComboBox *>(QStringLiteral("selectionSource"));
    source->setCurrentIndex(0);
    QMetaObject::invokeMethod(source, "activated", Q_ARG(int, 0));

    int editingFailures = 0;
    {
        SelectionChainDialog cleared;
        cleared.setContext(QStringLiteral("source-v1"), QStringLiteral("setup-v1"), QStringLiteral("G54"));
        cleared.setCandidates({candidate});
        auto *choices = cleared.findChild<QListWidget *>(QStringLiteral("selectionCandidates"));
        choices->setCurrentRow(0);
        choices->clearSelection();
        cleared.accept();
        if (cleared.result() == QDialog::Accepted || cleared.selection().explicitUserSelection) {
            std::cerr << "clearing candidate selection must discard pending geometry\n";
            ++editingFailures;
        } else {
            cleared.findChild<QPushButton *>(QStringLiteral("selectionUndo"))->click();
            cleared.accept();
            if (!cleared.selection().explicitUserSelection || choices->selectedItems().size() != 1) {
                std::cerr << "undo must restore the cleared candidate and its evidence\n";
                ++editingFailures;
            }
        }
    }
    {
        SelectionChainDialog edited;
        edited.setContext(QStringLiteral("source-v1"), QStringLiteral("setup-v1"), QStringLiteral("G54"));
        edited.setCandidates({candidate});
        edited.findChild<QListWidget *>(QStringLiteral("selectionCandidates"))->setCurrentRow(0);
        edited.findChild<QPushButton *>(QStringLiteral("selectionReverse"))->click();
        auto *partialMode = edited.findChild<QComboBox *>(QStringLiteral("selectionMode"));
        partialMode->setCurrentIndex(static_cast<int>(ChainSelectionMode::PartialChain));
        QMetaObject::invokeMethod(partialMode, "activated", Q_ARG(int, static_cast<int>(ChainSelectionMode::PartialChain)));
        edited.findChild<QListWidget *>(QStringLiteral("selectionEdges"))->item(3)->setSelected(false);
        edited.accept();
        const QVector<QVector3D> expected = {candidate.points.at(3), candidate.points.at(2),
                                           candidate.points.at(1), candidate.points.at(0)};
        if (edited.result() != QDialog::Accepted || !edited.selection().reversed ||
            edited.selection().closed || edited.selectedPoints() != expected) {
            std::cerr << "editing a reversed chain must preserve its actual traversal direction\n";
            ++editingFailures;
        }
    }
    if (editingFailures) return 1;

    // Side-facing evidence must never become accepted even via direct accept().
    candidate.surfaceNormal = QVector3D(1, 0, 0);
    dialog.setCandidates({candidate});
    list->setCurrentRow(0);
    dialog.accept();
    if (dialog.result() == QDialog::Accepted || dialog.selection().explicitUserSelection) {
        std::cerr << "unreachable selection must remain pending\n";
        return 1;
    }
    std::cout << "PASS selection chain dialog behavior\n";
    return 0;
}

