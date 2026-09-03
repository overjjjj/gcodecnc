#include "HoleSelectionSession.h"

#include <algorithm>
#include <limits>

bool HoleSelectionRecord::operator==(const HoleSelectionRecord &other) const
{
    return geometryId == other.geometryId && diameter == other.diameter &&
           center == other.center && selectionOrder == other.selectionOrder;
}

HoleSelectionSession::HoleSelectionSession(
    const QList<HoleSelectionRecord> &initialRecords)
    : m_records(initialRecords), m_baselineRecords(initialRecords)
{
    renumber();
    m_baselineRecords = m_records;
}

bool HoleSelectionSession::add(const HoleSelectionRecord &record)
{
    if (record.geometryId.trimmed().isEmpty()) {
        return false;
    }
    const auto duplicate = std::find_if(
        m_records.cbegin(), m_records.cend(), [&record](const HoleSelectionRecord &existing) {
            return existing.geometryId == record.geometryId;
        });
    if (duplicate != m_records.cend()) {
        return false;
    }

    rememberForUndo();
    HoleSelectionRecord added = record;
    added.selectionOrder = m_records.size();
    m_records.append(added);
    return true;
}

bool HoleSelectionSession::remove(const QString &geometryId)
{
    const auto selected = std::find_if(
        m_records.begin(), m_records.end(), [&geometryId](const HoleSelectionRecord &record) {
            return record.geometryId == geometryId;
        });
    if (selected == m_records.end()) {
        return false;
    }

    rememberForUndo();
    m_records.erase(selected);
    renumber();
    return true;
}

bool HoleSelectionSession::sort(HoleSelectionSort strategy)
{
    QList<HoleSelectionRecord> sorted = m_records;
    switch (strategy) {
    case HoleSelectionSort::SelectionOrder:
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const HoleSelectionRecord &left,
                            const HoleSelectionRecord &right) {
            return left.selectionOrder < right.selectionOrder;
        });
        break;
    case HoleSelectionSort::DiameterAscending:
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const HoleSelectionRecord &left,
                            const HoleSelectionRecord &right) {
            return left.diameter < right.diameter;
        });
        break;
    case HoleSelectionSort::DiameterDescending:
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const HoleSelectionRecord &left,
                            const HoleSelectionRecord &right) {
            return left.diameter > right.diameter;
        });
        break;
    case HoleSelectionSort::XAscending:
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const HoleSelectionRecord &left,
                            const HoleSelectionRecord &right) {
            return left.center.x() < right.center.x();
        });
        break;
    case HoleSelectionSort::YAscending:
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const HoleSelectionRecord &left,
                            const HoleSelectionRecord &right) {
            return left.center.y() < right.center.y();
        });
        break;
    case HoleSelectionSort::NearestNeighbor: {
        if (sorted.size() < 2) {
            return false;
        }
        QList<HoleSelectionRecord> remaining = sorted.mid(1);
        sorted = {m_records.first()};
        while (!remaining.isEmpty()) {
            const QVector3D current = sorted.constLast().center;
            int nearestIndex = 0;
            float nearestDistance = std::numeric_limits<float>::max();
            for (int index = 0; index < remaining.size(); ++index) {
                const float distance = (remaining.at(index).center - current).lengthSquared();
                if (distance < nearestDistance ||
                    (distance == nearestDistance &&
                     remaining.at(index).geometryId < remaining.at(nearestIndex).geometryId)) {
                    nearestDistance = distance;
                    nearestIndex = index;
                }
            }
            sorted.append(remaining.takeAt(nearestIndex));
        }
        break;
    }
    }

    if (sorted == m_records) {
        return false;
    }
    rememberForUndo();
    m_records = sorted;
    renumber();
    return true;
}

bool HoleSelectionSession::undo()
{
    if (m_undoStack.isEmpty()) {
        return false;
    }
    m_redoStack.append(snapshot());
    restore(m_undoStack.takeLast());
    return true;
}

bool HoleSelectionSession::redo()
{
    if (m_redoStack.isEmpty()) {
        return false;
    }
    m_undoStack.append(snapshot());
    restore(m_redoStack.takeLast());
    return true;
}

QList<HoleSelectionRecord> HoleSelectionSession::commit()
{
    m_baselineRecords = m_records;
    m_baselineDisplaySerialNumbers = m_displaySerialNumbers;
    m_undoStack.clear();
    m_redoStack.clear();
    return m_records;
}

void HoleSelectionSession::cancel()
{
    m_records = m_baselineRecords;
    m_displaySerialNumbers = m_baselineDisplaySerialNumbers;
    m_undoStack.clear();
    m_redoStack.clear();
}

bool HoleSelectionSession::isDirty() const
{
    return m_records != m_baselineRecords ||
           m_displaySerialNumbers != m_baselineDisplaySerialNumbers;
}

HoleSelectionSession::Snapshot HoleSelectionSession::snapshot() const
{
    return {m_records, m_displaySerialNumbers};
}

void HoleSelectionSession::restore(const Snapshot &snapshot)
{
    m_records = snapshot.records;
    m_displaySerialNumbers = snapshot.displaySerialNumbers;
}

void HoleSelectionSession::rememberForUndo()
{
    m_undoStack.append(snapshot());
    m_redoStack.clear();
}

void HoleSelectionSession::renumber()
{
    for (int index = 0; index < m_records.size(); ++index) {
        m_records[index].selectionOrder = index;
    }
}
