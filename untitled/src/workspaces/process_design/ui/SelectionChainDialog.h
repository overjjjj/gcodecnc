#pragma once
#include <QDialog>
#include "../selection/SelectionCandidate.h"

class QListWidget;
class QComboBox;
class QLabel;

class SelectionChainDialog : public QDialog
{
public:
    explicit SelectionChainDialog(QWidget *parent = nullptr, bool slotSelection = false);
    void setCandidates(const QList<SelectionCandidate> &candidates);
    void setContext(const QString &sourceFingerprint, const QString &setupFingerprint,
                    const QString &coordinateSystemId,
                    const QVector3D &toolAxis = QVector3D(0, 0, 1));
    const SelectionChain &selection() const { return m_selection; }
    QVector<QVector3D> selectedPoints() const;
    void setRequireClosedBoundary(bool required) { m_requireClosed = required; }
    void accept() override;
    void reject() override;
private:
    void refresh();
    void pickCandidate();
    void navigateBranch(int offset);
    void updateGeometry();
    void remember();
    void restoreHistory(bool redo);
    QVector<QVector3D> orderedPoints(const SelectionChain &draft) const;
    QList<SelectionCandidate> m_candidates;
    SelectionChainController m_controller;
    SelectionChain m_selection;
    SelectionChain m_context;
    SelectionChain m_candidateContext;
    int m_candidateIndex = -1;
    QListWidget *m_list = nullptr;
    QListWidget *m_edges = nullptr;
    QComboBox *m_source = nullptr;
    QComboBox *m_mode = nullptr;
    QComboBox *m_side = nullptr;
    QComboBox *m_start = nullptr;
    QComboBox *m_sort = nullptr;
    QLabel *m_status = nullptr;
    bool m_requireClosed = false;
    bool m_slotSelection = false;
    struct Snapshot {
        SelectionChainController controller;
        int candidateIndex = -1;
    };
    QList<Snapshot> m_undo;
    QList<Snapshot> m_redo;
};
