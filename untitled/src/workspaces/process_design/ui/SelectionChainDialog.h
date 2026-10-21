#pragma once
#include <QDialog>
#include <functional>
#include "../selection/SelectionCandidate.h"

class QListWidget;
class QComboBox;
class QLabel;
class QPushButton;
class QKeyEvent;
class QEvent;
class QShowEvent;

class SelectionChainDialog : public QDialog
{
public:
    explicit SelectionChainDialog(QWidget *parent = nullptr);
    void selectGeometry(const QString &id);
    void selectTopology(const QString &id, int kind, const QVector3D &point,
                        double curveParameter);
    void previewTopology(const QString &id, int kind, const QVector3D &point,
                         double curveParameter);
    QString selectedCandidateGeometryId() const;
    bool hasCandidatePreview() const;
    std::function<void(const QVector<QVector3D> &)> previewChanged;
    std::function<void(const QVector<QVector3D> &, bool)> confirmedTopologyPreviewChanged;
    std::function<void(const QVector<QVector3D> &, bool)> topologyPreviewChanged;
    std::function<void(const QVector<QVector3D> &)> topologyHoverChanged;
    std::function<void(int)> pickModeChanged;
    std::function<void(const QStringList &)> facePreviewChanged;
    std::function<void(const QStringList &, const QStringList &,
                       const QVector<int> &)> topologyScopeChanged;
    std::function<QString(const SelectionChain &, const QString &)> acceptanceError;
    std::function<void(SelectionCandidate &, const MeshBoundaryLoop &)> resolveBoundary;
    void setCandidates(const QList<SelectionCandidate> &candidates);
    void setPreviousSelection(const SelectionChain &selection);
    void setPreviousSelections(const QList<SelectionChain> &selections);
    void setContext(const QString &sourceFingerprint, const QString &setupFingerprint,
                    const QString &coordinateSystemId,
                    const QVector3D &toolAxis = QVector3D(0, 0, 1));
    const SelectionChain &selection() const { return m_selection; }
    QList<SelectionChain> selections() const;
    QVector<QVector3D> selectedPoints() const;
    QVector<QVector3D> selectedPoints(const SelectionChain &selection);
    void setRequireClosedBoundary(bool required) { m_requireClosed = required; }
    void setEdgePickingOnly(bool required);
    void accept() override;
    void reject() override;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
private:
    bool handleSessionKey(QKeyEvent *event);
    void refresh();
    void updateValidationStatus(const QString &currentState);
    void pickCandidate();
    void navigateBranch(int offset);
    void updateGeometry();
    void remember();
    void restoreHistory(bool redo);
    void restorePreviousSelection();
    int validatePreviousSelection(const SelectionChain &selection, bool checkStrategy = true);
    void selectCoplanarRegions();
    void resetTransientSession();
    void syncPickMode();
    void selectStartVertex(const QString &id);
    void syncTopologyScope();
    QVector<QVector3D> orderedPoints(const SelectionChain &draft) const;
    struct TopologyMatch {
        int candidateIndex = -1;
        int loopIndex = -1;
    };
    struct PendingTopologyPick {
        QString id;
        int kind = -1;
        QVector3D point;
        double curveParameter = 0.0;
        QList<TopologyMatch> matches;
        int matchIndex = -1;
        bool isEmpty() const { return id.isEmpty(); }
    };
    QList<TopologyMatch> topologyMatches(const QString &id, int kind) const;
    void setPendingTopologyPick(const QString &id, int kind, const QVector3D &point,
                                double curveParameter,
                                const QList<TopologyMatch> &matches);
    void previewPendingTopologyMatch();
    void confirmPendingTopologyMatch();
    bool candidateChangeWouldDiscardDraft(int candidateIndex, int kind) const;
    void selectTopologyMatch(const QString &id, int kind, const QVector3D &point,
                             double curveParameter, const TopologyMatch &match);
    const MeshPickEdge *edgeForId(const SelectionCandidate &candidate,
                                  const QString &id) const;
    const MeshPickVertex *vertexForId(const SelectionCandidate &candidate,
                                      const QString &id) const;
    bool topologyRefsContinuous(const SelectionCandidate &candidate,
                                const SelectionChain &draft,
                                QString *reason = nullptr) const;
    QString accessFailureText(const SelectionChain &draft,
                              const SetupAccessResult &access) const;
    void applyTopologySelection(const QList<SelectionTopologyRef> &refs,
                                const SelectionAnchor &startAnchor,
                                bool hasStartAnchor,
                                const SelectionAnchor &endAnchor,
                                bool hasEndAnchor,
                                const QList<SelectionBoundaryLoop> &loops,
                                bool closed);
    void resolveSelectedBoundary(int candidateIndex, const SelectionChain &selection);
    void updateTopologyBranches(const SelectionChain &draft);
    void prepareFullBoundaryOffer();
    void useFullBoundary();
    void beginBoundaryAdjustment(ChainSelectionMode mode);
    void acceptUniqueNextEdge();
    void clearTopologyPreview();
    bool buildLoopDraft(int loopIndex, int firstIndex, int step, int count,
                        SelectionChain *draft) const;
    bool buildIntervalPreview(const QString &terminalEdgeId,
                              SelectionChain *draft) const;
    void applyPreviewDraft();
    QList<SelectionCandidate> m_candidates;
    SelectionChainController m_controller;
    SelectionChain m_selection;
    QList<SelectionChain> m_previousSelections;
    QList<SelectionChain> m_regionSelections;
    QPushButton *m_coplanar = nullptr;
    QListWidget *m_regions = nullptr;
    QPushButton *m_removeRegion = nullptr;
    QPushButton *m_lastSelection = nullptr;
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
    QComboBox *m_topologyContext = nullptr;
    QLabel *m_topologyContextLabel = nullptr;
    QPushButton *m_confirmTopologyContext = nullptr;
    QPushButton *m_useFullBoundary = nullptr;
    QPushButton *m_adjustBoundary = nullptr;
    QPushButton *m_acceptNextEdge = nullptr;
    QPushButton *m_connectEdges = nullptr;
    QPushButton *m_extractSegment = nullptr;
    QPushButton *m_moreDetails = nullptr;
    QPushButton *m_finishBoundary = nullptr;
    QPushButton *m_nextButton = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_chainStatus = nullptr;
    bool m_requireClosed = false;
    bool m_edgePickingOnly = false;
    bool m_pickingStart = false;
    QString m_partialStartEdgeId;
    QString m_partialLoopFaceId;
    int m_partialLoopCandidateIndex = -1;
    int m_partialLoopIndex = -1;
    int m_partialStartIndex = -1;
    int m_partialStep = 1;
    SelectionAnchor m_partialStartAnchor;
    QStringList m_branchEdgeIds;
    int m_branchEdgeIndex = -1;
    int m_activeLoopCandidateIndex = -1;
    int m_activeLoopIndex = -1;
    PendingTopologyPick m_pendingTopologyPick;
    enum class TopologyPreviewKind { None, FullBoundary, Interval, Branch };
    TopologyPreviewKind m_topologyPreviewKind = TopologyPreviewKind::None;
    SelectionChain m_topologyPreviewDraft;
    QString m_topologyPreviewTerminalEdgeId;
    bool m_adjustingBoundary = false;
    struct Snapshot {
        SelectionChainController controller;
        QList<SelectionChain> regionSelections;
        int candidateIndex = -1;
        bool pickingStart = false;
        QString partialStartEdgeId;
        QString partialLoopFaceId;
        int partialLoopCandidateIndex = -1;
        int partialLoopIndex = -1;
        int partialStartIndex = -1;
        int partialStep = 1;
        SelectionAnchor partialStartAnchor;
        QStringList branchEdgeIds;
        int branchEdgeIndex = -1;
        int activeLoopCandidateIndex = -1;
        int activeLoopIndex = -1;
        PendingTopologyPick pendingTopologyPick;
    };
    Snapshot snapshot() const;
    void restoreSnapshot(const Snapshot &snapshot);
    QList<Snapshot> m_undo;
    QList<Snapshot> m_redo;
    Snapshot m_adjustBaseline;
    bool m_hasAdjustBaseline = false;
};
