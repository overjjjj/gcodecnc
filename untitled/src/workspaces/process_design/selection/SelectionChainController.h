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

enum class SelectionTopologyKind {
    Face,
    Edge,
    Vertex
};

struct SelectionTopologyRef {
    QString id;
    SelectionTopologyKind kind = SelectionTopologyKind::Edge;
    bool forward = true;
    bool operator==(const SelectionTopologyRef &other) const {
        return id == other.id && kind == other.kind && forward == other.forward;
    }
};

struct SelectionAnchor {
    QString topologyId;
    QVector3D point;
    bool hasCurveParameter = false;
    double curveParameter = 0.0;
    bool operator==(const SelectionAnchor &other) const {
        return topologyId == other.topologyId && point == other.point &&
               hasCurveParameter == other.hasCurveParameter &&
               curveParameter == other.curveParameter;
    }
};

struct MachiningBoundaryCurve {
    bool circular = false;
    QVector3D start, end, center;
    double sweep = 0;
    QString type;
    double firstParameter = 0, lastParameter = 0;
    double approximationBound = 0;
    bool operator==(const MachiningBoundaryCurve &other) const {
        return circular==other.circular && start==other.start && end==other.end &&
               center==other.center && sweep==other.sweep && type==other.type &&
               firstParameter==other.firstParameter && lastParameter==other.lastParameter &&
               approximationBound==other.approximationBound;
    }
};

enum class MachiningBoundaryRole { Undetermined, Outer, Island };

struct SelectionBoundaryLoop {
    QString faceId;
    bool outer = false;
    QStringList orderedEdgeIds;
    QVector<bool> orderedForward;
    // Source BRep outer/inner identity above is never rewritten. These fields
    // certify a separately verified, bounded machining opening below that wire.
    MachiningBoundaryRole machiningRole = MachiningBoundaryRole::Undetermined;
    QVector<QVector3D> machiningBoundary = {};
    double maximumDepth = 0.0;
    QVector<MachiningBoundaryCurve> curves = {};
    double boundaryTolerance = 0;
    QStringList supportFaceIds = {}; // adjacent walls and common floor
    QString discretizationRule, candidateRule, volumeProofRule;
    double sourceBoundaryBound = 0, consumedBoundaryBound = 0;
    double volumeProofBudget = 0, maximumEscapeBudget = 0;
    bool operator==(const SelectionBoundaryLoop &other) const {
        return faceId == other.faceId && outer == other.outer &&
               orderedEdgeIds == other.orderedEdgeIds &&
               orderedForward == other.orderedForward &&
               machiningRole == other.machiningRole && machiningBoundary == other.machiningBoundary &&
               maximumDepth == other.maximumDepth && supportFaceIds == other.supportFaceIds &&
               curves == other.curves && boundaryTolerance == other.boundaryTolerance &&
               discretizationRule==other.discretizationRule && candidateRule==other.candidateRule &&
               volumeProofRule==other.volumeProofRule && sourceBoundaryBound==other.sourceBoundaryBound &&
               consumedBoundaryBound==other.consumedBoundaryBound && volumeProofBudget==other.volumeProofBudget &&
               maximumEscapeBudget==other.maximumEscapeBudget;
    }
};

// Metadata support is not source verification; generation still recomputes the proof.
inline bool openingEvidenceSupported(const SelectionBoundaryLoop &loop)
{
    if(loop.discretizationRule!="occ-bezier-finite-chord-v1" ||
       (loop.candidateRule!="occ-wire-inward-offset-v1" && loop.candidateRule!="source-polygon-inset-candidate-v1") ||
       loop.volumeProofRule!="occ-boundary-component-escape-v1" || loop.curves.isEmpty() ||
       !std::isfinite(loop.boundaryTolerance) || loop.boundaryTolerance<=0 ||
       !std::isfinite(loop.volumeProofBudget) || loop.volumeProofBudget<=0 ||
       !std::isfinite(loop.maximumEscapeBudget) || loop.maximumEscapeBudget<loop.volumeProofBudget)return false;
    const double budget=loop.boundaryTolerance*.25;
    for(double bound:{loop.sourceBoundaryBound,loop.consumedBoundaryBound})
        if(!std::isfinite(bound) || bound<0 || bound>budget)return false;
    for(const auto &curve:loop.curves) {
        if(curve.type!="line" && curve.type!="circle" && curve.type!="ellipse" && curve.type!="bezier" && curve.type!="bspline")return false;
        if(curve.circular!=(curve.type=="circle") || !std::isfinite(curve.firstParameter) ||
           !std::isfinite(curve.lastParameter) || curve.firstParameter==curve.lastParameter ||
           !std::isfinite(curve.approximationBound) || curve.approximationBound<0 || curve.approximationBound>budget)return false;
    }
    return true;
}

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
    int evidenceVersion = 1;
    QString id;
    QString sourceFingerprint;
    QString candidateGeometryId;
    QString setupFingerprint;
    ChainGeometrySource geometrySource = ChainGeometrySource::Entity;
    ChainSelectionMode selectionMode = ChainSelectionMode::Chain;
    ChainMachiningSide machiningSide = ChainMachiningSide::Mixed;
    QString coordinateSystemId = QStringLiteral("G54");
    QStringList orderedGeometryIds;
    QList<SelectionTopologyRef> orderedTopologyRefs;
    SelectionAnchor startAnchor;
    bool hasStartAnchor = false;
    SelectionAnchor endAnchor;
    bool hasEndAnchor = false;
    QList<SelectionBoundaryLoop> boundaryLoops;
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

inline bool selectionSurfaceMatchesToolAxis(const QVector3D &surfaceNormal,
                                            const QVector3D &toolAxis)
{
    return surfaceNormal.lengthSquared() > 1.0e-8f &&
           toolAxis.lengthSquared() > 1.0e-8f &&
           std::abs(QVector3D::dotProduct(surfaceNormal.normalized(),
                                          toolAxis.normalized())) >= 0.65f;
}

inline bool selectionContainsCompleteOpening(const SelectionChain &selection,
                                              const SelectionBoundaryLoop &loop)
{
    const int count = loop.orderedEdgeIds.size();
    if (!(loop.machiningRole == MachiningBoundaryRole::Outer) || loop.outer || count == 0 ||
        loop.orderedForward.size() != count || loop.machiningBoundary.size() < 3 ||
        !std::isfinite(loop.maximumDepth) || loop.maximumDepth <= 0.0 ||
        loop.supportFaceIds.isEmpty() || selection.orderedTopologyRefs.size() != count)
        return false;
    const int start = loop.orderedEdgeIds.indexOf(selection.orderedTopologyRefs.first().id);
    if (start < 0) return false;
    for (int step : {1, -1}) {
        bool matches = true;
        for (int i = 0; i < count; ++i) {
            const int index = (start + step * i + count) % count;
            const auto &ref = selection.orderedTopologyRefs[i];
            if (ref.kind != SelectionTopologyKind::Edge || ref.id != loop.orderedEdgeIds[index] ||
                ref.forward != (step > 0 ? loop.orderedForward[index] : !loop.orderedForward[index])) {
                matches = false; break;
            }
        }
        if (matches) return true;
    }
    return false;
}

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
    if (!selectionSurfaceMatchesToolAxis(selection.selectedSurfaceNormal,
                                         selection.toolAxis)) {
        return {SetupAccessStatus::DirectionUnreachable,
                QStringLiteral("SELECTION_DIRECTION_UNREACHABLE")};
    }
    bool hasSourceOuterLoop = selection.outerLoopPointCount > 0;
    if (selection.evidenceVersion >= 2) {
        hasSourceOuterLoop = false;
        for (const SelectionBoundaryLoop &loop : selection.boundaryLoops) {
            if (selectionContainsCompleteOpening(selection, loop)) {
                hasSourceOuterLoop = true;
                break;
            }
            if (!loop.outer || loop.orderedEdgeIds.isEmpty()) continue;
            bool containsSelectedEdges = true;
            for (const SelectionTopologyRef &ref : selection.orderedTopologyRefs) {
                if (ref.kind == SelectionTopologyKind::Edge &&
                    !loop.orderedEdgeIds.contains(ref.id)) {
                    containsSelectedEdges = false;
                    break;
                }
            }
            if (containsSelectedEdges) {
                hasSourceOuterLoop = true;
                break;
            }
        }
    }
    if (requireClosedBoundary &&
        (!selection.closed || !hasSourceOuterLoop ||
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
    void setTopologySelection(const QList<SelectionTopologyRef> &refs,
                              const SelectionAnchor &startAnchor,
                              bool hasStartAnchor,
                              const SelectionAnchor &endAnchor,
                              bool hasEndAnchor,
                              const QList<SelectionBoundaryLoop> &boundaryLoops);

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
