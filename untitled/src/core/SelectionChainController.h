#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector3D>

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

struct SelectionChain {
    QString id;
    ChainGeometrySource geometrySource = ChainGeometrySource::Entity;
    ChainSelectionMode selectionMode = ChainSelectionMode::Chain;
    ChainMachiningSide machiningSide = ChainMachiningSide::Mixed;
    QString coordinateSystemId = QStringLiteral("G54");
    QStringList orderedGeometryIds;
    QVector3D startPoint;
    bool hasStartPoint = false;
    bool reversed = false;
    bool closed = false;
    ChainSortStrategy sortStrategy = ChainSortStrategy::SelectionOrder;
    QString selectedBranchGeometryId;

    bool operator==(const SelectionChain &other) const;
};

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
