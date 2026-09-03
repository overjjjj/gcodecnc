#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector3D>
#include <cmath>

enum class ChainGeometrySource {
    Entity,
    Wire,
    Stock
};

enum class ChainSelectionMode {
    Edge,
    Chain,
    Face,
    PartialChain
};

enum class ChainMachiningSide {
    Inside,
    Outside,
    Mixed
};

enum class ChainSortStrategy {
    SelectionOrder,
    BidirectionalX,
    BidirectionalY,
    UnidirectionalX,
    UnidirectionalY,
    Diagonal,
    PointToPoint
};

enum class SelectionChainState {
    Idle,
    Picking,
    ChainBuilding,
    Ordered,
    Confirmed
};

enum class SetupAccessStatus {
    Ready,
    SetupMismatch,
    DirectionUnreachable,
    InsufficientTopology,
    StaleSource
};

struct SetupAccessResult {
    SetupAccessStatus status = SetupAccessStatus::InsufficientTopology;
    QString issueCode;

    bool ok() const { return status == SetupAccessStatus::Ready; }
};

struct SelectionChain {
    QString id;
    QString sourceFingerprint;
    QString setupFingerprint;
    ChainGeometrySource geometrySource = ChainGeometrySource::Entity;
    ChainSelectionMode selectionMode = ChainSelectionMode::Chain;
    ChainMachiningSide machiningSide = ChainMachiningSide::Mixed;
    QString coordinateSystemId = QStringLiteral("G54");
    QStringList orderedGeometryIds;
    QVector3D selectedSurfaceNormal;
    QVector3D toolAxis = QVector3D(0.0f, 0.0f, 1.0f);
    QVector3D startPoint;
    bool hasStartPoint = false;
    bool reversed = false;
    bool closed = false;
    int outerLoopPointCount = 0;
    int islandCount = 0;
    int freeEndCount = 0;
    bool explicitUserSelection = false;
    ChainSortStrategy sortStrategy = ChainSortStrategy::SelectionOrder;

    bool operator==(const SelectionChain &other) const;
};

inline SetupAccessResult evaluateSelectionAccess(
    const SelectionChain &selection,
    const QString &currentSourceFingerprint,
    const QString &currentSetupFingerprint,
    const QString &currentCoordinateSystemId,
    bool requireClosedBoundary)
{
    if (!selection.explicitUserSelection || selection.orderedGeometryIds.isEmpty()) {
        return {SetupAccessStatus::InsufficientTopology,
                QStringLiteral("SELECTION_EVIDENCE_REQUIRED")};
    }
    if (selection.sourceFingerprint.isEmpty() ||
        (!currentSourceFingerprint.isEmpty() &&
         selection.sourceFingerprint != currentSourceFingerprint)) {
        return {SetupAccessStatus::StaleSource,
                QStringLiteral("SELECTION_SOURCE_STALE")};
    }
    if (selection.setupFingerprint.isEmpty() ||
        (!currentSetupFingerprint.isEmpty() &&
         selection.setupFingerprint != currentSetupFingerprint) ||
        selection.coordinateSystemId.compare(currentCoordinateSystemId,
                                             Qt::CaseInsensitive) != 0) {
        return {SetupAccessStatus::SetupMismatch,
                QStringLiteral("SELECTION_SETUP_MISMATCH")};
    }
    if (selection.selectedSurfaceNormal.lengthSquared() <= 1.0e-8f ||
        selection.toolAxis.lengthSquared() <= 1.0e-8f ||
        std::abs(QVector3D::dotProduct(selection.selectedSurfaceNormal.normalized(),
                                       selection.toolAxis.normalized())) < 0.65f) {
        return {SetupAccessStatus::DirectionUnreachable,
                QStringLiteral("SELECTION_DIRECTION_UNREACHABLE")};
    }
    if (requireClosedBoundary &&
        (!selection.closed || selection.outerLoopPointCount < 3 ||
         selection.freeEndCount != 0)) {
        return {SetupAccessStatus::InsufficientTopology,
                QStringLiteral("SELECTION_TOPOLOGY_INSUFFICIENT")};
    }
    return {SetupAccessStatus::Ready, QString()};
}

class SelectionChainController
{
public:
    explicit SelectionChainController(const SelectionChain &initial = {});

    void begin();
    SelectionChainState state() const { return m_state; }

    bool addGeometry(const QString &geometryId, const QVector3D &referencePoint);
    bool removeGeometry(const QString &geometryId);
    QStringList geometryIds() const;
    bool finishChain();
    bool reverse();
    bool sort(ChainSortStrategy strategy);

    void setGeometrySource(ChainGeometrySource source);
    void setSelectionMode(ChainSelectionMode mode);
    void setMachiningSide(ChainMachiningSide side);
    void setCoordinateSystemId(const QString &coordinateSystemId);
    void setSourceContext(const QString &sourceFingerprint,
                          const QString &setupFingerprint);
    void setSelectedSurfaceNormal(const QVector3D &normal);
    void setToolAxis(const QVector3D &axis);
    void setTopologyEvidence(int outerLoopPointCount,
                             int islandCount,
                             int freeEndCount);
    void setClosed(bool closed);
    void setStartPoint(const QVector3D &point);

    void setBranchChoices(const QStringList &geometryIds, int currentIndex = 0);
    QString previousBranch();
    QString switchBranch();
    QString nextBranch();
    QString currentBranch() const;

    bool undo();
    bool redo();
    SelectionChain commit();
    void cancel();
    bool isDirty() const;
    bool reversed() const { return m_chain.reversed; }

private:
    struct GeometryRecord {
        QString id;
        QVector3D referencePoint;
        int selectionOrder = 0;

        bool operator==(const GeometryRecord &other) const;
    };

    struct Snapshot {
        SelectionChain chain;
        QList<GeometryRecord> records;
        SelectionChainState state = SelectionChainState::Idle;
    };

    SelectionChain m_chain;
    QList<GeometryRecord> m_records;
    SelectionChainState m_state = SelectionChainState::Idle;

    SelectionChain m_baselineChain;
    QList<GeometryRecord> m_baselineRecords;
    SelectionChainState m_baselineState = SelectionChainState::Idle;

    QList<Snapshot> m_undoStack;
    QList<Snapshot> m_redoStack;
    QStringList m_branchChoices;
    int m_branchIndex = -1;

    Snapshot snapshot() const;
    void restore(const Snapshot &snapshot);
    void rememberForUndo();
    void syncGeometryIds();
    QString navigateBranch(int offset);
    static bool pointLessX(const GeometryRecord &left, const GeometryRecord &right);
    static bool pointLessY(const GeometryRecord &left, const GeometryRecord &right);
};
