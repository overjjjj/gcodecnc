#include "SelectionChainController.h"

#include <QCryptographicHash>

#include <algorithm>
#include <limits>

bool SelectionChain::operator==(const SelectionChain &other) const
{
    return id == other.id && sourceFingerprint == other.sourceFingerprint &&
           setupFingerprint == other.setupFingerprint &&
           geometrySource == other.geometrySource &&
           selectionMode == other.selectionMode && machiningSide == other.machiningSide &&
           coordinateSystemId == other.coordinateSystemId &&
           orderedGeometryIds == other.orderedGeometryIds &&
           selectedSurfaceNormal == other.selectedSurfaceNormal && toolAxis == other.toolAxis &&
           startPoint == other.startPoint &&
           hasStartPoint == other.hasStartPoint && reversed == other.reversed &&
           closed == other.closed && outerLoopPointCount == other.outerLoopPointCount &&
           islandCount == other.islandCount && freeEndCount == other.freeEndCount &&
           explicitUserSelection == other.explicitUserSelection &&
           sortStrategy == other.sortStrategy;
}

bool SelectionChainController::GeometryRecord::operator==(
    const GeometryRecord &other) const
{
    return id == other.id && referencePoint == other.referencePoint &&
           selectionOrder == other.selectionOrder;
}

SelectionChainController::SelectionChainController(const SelectionChain &initial)
    : m_chain(initial), m_baselineChain(initial)
{
    for (int index = 0; index < initial.orderedGeometryIds.size(); ++index) {
        m_records.append({initial.orderedGeometryIds.at(index), QVector3D(), index});
    }
    m_baselineRecords = m_records;
    if (!m_records.isEmpty()) {
        m_state = SelectionChainState::Confirmed;
        m_baselineState = SelectionChainState::Confirmed;
    }
}

void SelectionChainController::begin()
{
    m_state = SelectionChainState::Picking;
    m_undoStack.clear();
    m_redoStack.clear();
}

bool SelectionChainController::addGeometry(const QString &geometryId,
                                           const QVector3D &referencePoint)
{
    const QString acceptedId = geometryId.trimmed();
    if (acceptedId.isEmpty()) {
        return false;
    }
    const auto duplicate = std::find_if(
        m_records.cbegin(), m_records.cend(), [&acceptedId](const GeometryRecord &record) {
            return record.id == acceptedId;
        });
    if (duplicate != m_records.cend()) {
        return false;
    }

    rememberForUndo();
    int nextOrder = 0;
    for (const GeometryRecord &record : m_records) {
        nextOrder = std::max(nextOrder, record.selectionOrder + 1);
    }
    m_records.append({acceptedId, referencePoint, nextOrder});
    syncGeometryIds();
    m_state = SelectionChainState::ChainBuilding;
    return true;
}

bool SelectionChainController::removeGeometry(const QString &geometryId)
{
    const auto selected = std::find_if(
        m_records.begin(), m_records.end(), [&geometryId](const GeometryRecord &record) {
            return record.id == geometryId;
        });
    if (selected == m_records.end()) {
        return false;
    }
    rememberForUndo();
    m_records.erase(selected);
    syncGeometryIds();
    m_state = m_records.isEmpty() ? SelectionChainState::Picking
                                  : SelectionChainState::ChainBuilding;
    return true;
}

QStringList SelectionChainController::geometryIds() const
{
    QStringList ids;
    ids.reserve(m_records.size());
    for (const GeometryRecord &record : m_records) {
        ids.append(record.id);
    }
    return ids;
}

bool SelectionChainController::finishChain()
{
    if (m_records.isEmpty()) {
        return false;
    }
    if (m_state != SelectionChainState::Ordered) {
        rememberForUndo();
    }
    m_state = SelectionChainState::Ordered;
    return true;
}

bool SelectionChainController::reverse()
{
    if (m_records.size() < 2) {
        return false;
    }
    rememberForUndo();
    std::reverse(m_records.begin(), m_records.end());
    m_chain.reversed = !m_chain.reversed;
    syncGeometryIds();
    m_state = SelectionChainState::Ordered;
    return true;
}

bool SelectionChainController::sort(ChainSortStrategy strategy)
{
    QList<GeometryRecord> sorted = m_records;
    switch (strategy) {
    case ChainSortStrategy::SelectionOrder:
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const GeometryRecord &left, const GeometryRecord &right) {
            return left.selectionOrder < right.selectionOrder;
        });
        break;
    case ChainSortStrategy::UnidirectionalX:
        std::stable_sort(sorted.begin(), sorted.end(), SelectionChainController::pointLessX);
        break;
    case ChainSortStrategy::UnidirectionalY:
        std::stable_sort(sorted.begin(), sorted.end(), SelectionChainController::pointLessY);
        break;
    case ChainSortStrategy::BidirectionalX: {
        std::stable_sort(sorted.begin(), sorted.end(), SelectionChainController::pointLessY);
        int first = 0;
        int row = 0;
        while (first < sorted.size()) {
            int last = first + 1;
            while (last < sorted.size() &&
                   sorted.at(last).referencePoint.y() == sorted.at(first).referencePoint.y()) {
                ++last;
            }
            if ((row % 2) != 0) {
                std::reverse(sorted.begin() + first, sorted.begin() + last);
            }
            first = last;
            ++row;
        }
        break;
    }
    case ChainSortStrategy::BidirectionalY: {
        std::stable_sort(sorted.begin(), sorted.end(), SelectionChainController::pointLessX);
        int first = 0;
        int column = 0;
        while (first < sorted.size()) {
            int last = first + 1;
            while (last < sorted.size() &&
                   sorted.at(last).referencePoint.x() == sorted.at(first).referencePoint.x()) {
                ++last;
            }
            if ((column % 2) != 0) {
                std::reverse(sorted.begin() + first, sorted.begin() + last);
            }
            first = last;
            ++column;
        }
        break;
    }
    case ChainSortStrategy::Diagonal:
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const GeometryRecord &left, const GeometryRecord &right) {
            const float leftDiagonal = left.referencePoint.x() + left.referencePoint.y();
            const float rightDiagonal = right.referencePoint.x() + right.referencePoint.y();
            return leftDiagonal == rightDiagonal ? SelectionChainController::pointLessX(left, right)
                                                 : leftDiagonal < rightDiagonal;
        });
        break;
    case ChainSortStrategy::PointToPoint: {
        if (sorted.size() > 1) {
            QList<GeometryRecord> remaining = sorted.mid(1);
            sorted = {m_records.first()};
            while (!remaining.isEmpty()) {
                const QVector3D current = sorted.constLast().referencePoint;
                int nearestIndex = 0;
                float nearestDistance = std::numeric_limits<float>::max();
                for (int index = 0; index < remaining.size(); ++index) {
                    const float distance =
                        (remaining.at(index).referencePoint - current).lengthSquared();
                    if (distance < nearestDistance ||
                        (distance == nearestDistance &&
                         remaining.at(index).id < remaining.at(nearestIndex).id)) {
                        nearestDistance = distance;
                        nearestIndex = index;
                    }
                }
                sorted.append(remaining.takeAt(nearestIndex));
            }
        }
        break;
    }
    }

    if (sorted == m_records && m_chain.sortStrategy == strategy) {
        return false;
    }
    rememberForUndo();
    m_records = sorted;
    m_chain.sortStrategy = strategy;
    syncGeometryIds();
    m_state = SelectionChainState::Ordered;
    return true;
}

void SelectionChainController::setGeometrySource(ChainGeometrySource source)
{
    if (m_chain.geometrySource == source) {
        return;
    }
    rememberForUndo();
    m_chain.geometrySource = source;
}

void SelectionChainController::setSelectionMode(ChainSelectionMode mode)
{
    if (m_chain.selectionMode == mode) {
        return;
    }
    rememberForUndo();
    m_chain.selectionMode = mode;
}

void SelectionChainController::setMachiningSide(ChainMachiningSide side)
{
    if (m_chain.machiningSide == side) {
        return;
    }
    rememberForUndo();
    m_chain.machiningSide = side;
}

void SelectionChainController::setCoordinateSystemId(const QString &coordinateSystemId)
{
    const QString accepted = coordinateSystemId.trimmed();
    if (accepted.isEmpty() || m_chain.coordinateSystemId == accepted) {
        return;
    }
    rememberForUndo();
    m_chain.coordinateSystemId = accepted;
}

void SelectionChainController::setSourceContext(const QString &sourceFingerprint,
                                                const QString &setupFingerprint)
{
    if (m_chain.sourceFingerprint == sourceFingerprint &&
        m_chain.setupFingerprint == setupFingerprint) {
        return;
    }
    rememberForUndo();
    m_chain.sourceFingerprint = sourceFingerprint;
    m_chain.setupFingerprint = setupFingerprint;
}

void SelectionChainController::setSelectedSurfaceNormal(const QVector3D &normal)
{
    const QVector3D accepted = normal.lengthSquared() <= 1.0e-8f
        ? QVector3D()
        : normal.normalized();
    if (m_chain.selectedSurfaceNormal == accepted) {
        return;
    }
    rememberForUndo();
    m_chain.selectedSurfaceNormal = accepted;
}

void SelectionChainController::setToolAxis(const QVector3D &axis)
{
    const QVector3D accepted = axis.lengthSquared() <= 1.0e-8f
        ? QVector3D()
        : axis.normalized();
    if (m_chain.toolAxis == accepted) {
        return;
    }
    rememberForUndo();
    m_chain.toolAxis = accepted;
}

void SelectionChainController::setTopologyEvidence(int outerLoopPointCount,
                                                   int islandCount,
                                                   int freeEndCount)
{
    outerLoopPointCount = std::max(0, outerLoopPointCount);
    islandCount = std::max(0, islandCount);
    freeEndCount = std::max(0, freeEndCount);
    if (m_chain.outerLoopPointCount == outerLoopPointCount &&
        m_chain.islandCount == islandCount &&
        m_chain.freeEndCount == freeEndCount) {
        return;
    }
    rememberForUndo();
    m_chain.outerLoopPointCount = outerLoopPointCount;
    m_chain.islandCount = islandCount;
    m_chain.freeEndCount = freeEndCount;
}

void SelectionChainController::setClosed(bool closed)
{
    if (m_chain.closed == closed) {
        return;
    }
    rememberForUndo();
    m_chain.closed = closed;
}

void SelectionChainController::setStartPoint(const QVector3D &point)
{
    if (m_chain.hasStartPoint && m_chain.startPoint == point) {
        return;
    }
    rememberForUndo();
    m_chain.startPoint = point;
    m_chain.hasStartPoint = true;
}

void SelectionChainController::setBranchChoices(const QStringList &geometryIds,
                                                int currentIndex)
{
    m_branchChoices = geometryIds;
    m_branchChoices.removeDuplicates();
    if (m_branchChoices.isEmpty()) {
        m_branchIndex = -1;
        return;
    }
    m_branchIndex = std::max(0, std::min(currentIndex, m_branchChoices.size() - 1));
}

QString SelectionChainController::previousBranch()
{
    return navigateBranch(-1);
}

QString SelectionChainController::switchBranch()
{
    return navigateBranch(1);
}

QString SelectionChainController::nextBranch()
{
    return navigateBranch(1);
}

QString SelectionChainController::currentBranch() const
{
    return m_branchIndex >= 0 && m_branchIndex < m_branchChoices.size()
        ? m_branchChoices.at(m_branchIndex)
        : QString();
}

bool SelectionChainController::undo()
{
    if (m_undoStack.isEmpty()) {
        return false;
    }
    m_redoStack.append(snapshot());
    restore(m_undoStack.takeLast());
    return true;
}

bool SelectionChainController::redo()
{
    if (m_redoStack.isEmpty()) {
        return false;
    }
    m_undoStack.append(snapshot());
    restore(m_redoStack.takeLast());
    return true;
}

SelectionChain SelectionChainController::commit()
{
    if (m_records.isEmpty()) {
        return m_chain;
    }
    syncGeometryIds();
    m_chain.explicitUserSelection = true;
    QByteArray stablePayload;
    stablePayload.append(m_chain.sourceFingerprint.toUtf8()).append('|');
    stablePayload.append(m_chain.setupFingerprint.toUtf8()).append('|');
    stablePayload.append(m_chain.coordinateSystemId.toUpper().toUtf8()).append('|');
    stablePayload.append(QByteArray::number(static_cast<int>(m_chain.geometrySource))).append('|');
    stablePayload.append(QByteArray::number(static_cast<int>(m_chain.selectionMode))).append('|');
    stablePayload.append(QByteArray::number(static_cast<int>(m_chain.machiningSide))).append('|');
    stablePayload.append(m_chain.orderedGeometryIds.join(QLatin1Char(',')).toUtf8()).append('|');
    stablePayload.append(QByteArray::number(m_chain.selectedSurfaceNormal.x(), 'g', 9)).append(',');
    stablePayload.append(QByteArray::number(m_chain.selectedSurfaceNormal.y(), 'g', 9)).append(',');
    stablePayload.append(QByteArray::number(m_chain.selectedSurfaceNormal.z(), 'g', 9)).append('|');
    stablePayload.append(QByteArray::number(m_chain.toolAxis.x(), 'g', 9)).append(',');
    stablePayload.append(QByteArray::number(m_chain.toolAxis.y(), 'g', 9)).append(',');
    stablePayload.append(QByteArray::number(m_chain.toolAxis.z(), 'g', 9)).append('|');
    stablePayload.append(QByteArray::number(m_chain.startPoint.x(), 'g', 9)).append(',');
    stablePayload.append(QByteArray::number(m_chain.startPoint.y(), 'g', 9)).append(',');
    stablePayload.append(QByteArray::number(m_chain.startPoint.z(), 'g', 9)).append('|');
    stablePayload.append(QByteArray::number(m_chain.hasStartPoint)).append('|');
    stablePayload.append(QByteArray::number(m_chain.reversed)).append('|');
    stablePayload.append(QByteArray::number(m_chain.closed)).append('|');
    stablePayload.append(QByteArray::number(m_chain.outerLoopPointCount)).append('|');
    stablePayload.append(QByteArray::number(m_chain.islandCount)).append('|');
    stablePayload.append(QByteArray::number(m_chain.freeEndCount)).append('|');
    stablePayload.append(QByteArray::number(static_cast<int>(m_chain.sortStrategy)));
    m_chain.id = QStringLiteral("selection:v1:") + QString::fromLatin1(
        QCryptographicHash::hash(stablePayload, QCryptographicHash::Sha256).toHex());
    m_state = SelectionChainState::Confirmed;
    m_baselineChain = m_chain;
    m_baselineRecords = m_records;
    m_baselineState = m_state;
    m_undoStack.clear();
    m_redoStack.clear();
    return m_chain;
}

void SelectionChainController::cancel()
{
    m_chain = m_baselineChain;
    m_records = m_baselineRecords;
    m_state = m_baselineState;
    m_undoStack.clear();
    m_redoStack.clear();
}

bool SelectionChainController::isDirty() const
{
    return !(m_chain == m_baselineChain) || m_records != m_baselineRecords;
}

SelectionChainController::Snapshot SelectionChainController::snapshot() const
{
    return {m_chain, m_records, m_state};
}

void SelectionChainController::restore(const Snapshot &snapshot)
{
    m_chain = snapshot.chain;
    m_records = snapshot.records;
    m_state = snapshot.state;
}

void SelectionChainController::rememberForUndo()
{
    m_undoStack.append(snapshot());
    m_redoStack.clear();
}

void SelectionChainController::syncGeometryIds()
{
    m_chain.orderedGeometryIds = geometryIds();
}

QString SelectionChainController::navigateBranch(int offset)
{
    if (m_branchChoices.isEmpty()) {
        return QString();
    }
    m_branchIndex = (m_branchIndex + offset) % m_branchChoices.size();
    if (m_branchIndex < 0) {
        m_branchIndex += m_branchChoices.size();
    }
    return currentBranch();
}

bool SelectionChainController::pointLessX(const GeometryRecord &left,
                                          const GeometryRecord &right)
{
    if (left.referencePoint.x() != right.referencePoint.x()) {
        return left.referencePoint.x() < right.referencePoint.x();
    }
    if (left.referencePoint.y() != right.referencePoint.y()) {
        return left.referencePoint.y() < right.referencePoint.y();
    }
    return left.id < right.id;
}

bool SelectionChainController::pointLessY(const GeometryRecord &left,
                                          const GeometryRecord &right)
{
    if (left.referencePoint.y() != right.referencePoint.y()) {
        return left.referencePoint.y() < right.referencePoint.y();
    }
    if (left.referencePoint.x() != right.referencePoint.x()) {
        return left.referencePoint.x() < right.referencePoint.x();
    }
    return left.id < right.id;
}
