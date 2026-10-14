#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include "../../../core/Coordinate3D.h"

enum class HoleSelectionSort {
    SelectionOrder,
    DiameterAscending,
    DiameterDescending,
    XAscending,
    YAscending,
    NearestNeighbor
};

struct HoleSelectionRecord {
    QString geometryId;
    double diameter = 0.0;
    Coordinate3D center;
    int selectionOrder = 0;

    bool operator==(const HoleSelectionRecord &other) const;
};

class HoleSelectionSession
{
public:
    explicit HoleSelectionSession(const QList<HoleSelectionRecord> &initialRecords = {});

    const QList<HoleSelectionRecord> &records() const { return m_records; }

    bool add(const HoleSelectionRecord &record);
    bool remove(const QString &geometryId);
    bool sort(HoleSelectionSort strategy);
    bool setSelected(const QString &geometryId, bool selected);
    bool isSelected(const QString &geometryId) const { return !m_excluded.contains(geometryId); }
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    bool undo();
    bool redo();

    QList<HoleSelectionRecord> commit();
    void cancel();
    bool isDirty() const;

    void setDisplaySerialNumbers(bool visible) { m_displaySerialNumbers = visible; }
    bool displaySerialNumbers() const { return m_displaySerialNumbers; }

private:
    struct Snapshot {
        QList<HoleSelectionRecord> records;
        bool displaySerialNumbers = false;
        QSet<QString> excluded;
    };

    QSet<QString> m_excluded;
    QSet<QString> m_baselineExcluded;
    QList<HoleSelectionRecord> m_records;
    QList<HoleSelectionRecord> m_baselineRecords;
    QList<Snapshot> m_undoStack;
    QList<Snapshot> m_redoStack;
    bool m_displaySerialNumbers = false;
    bool m_baselineDisplaySerialNumbers = false;

    Snapshot snapshot() const;
    void restore(const Snapshot &snapshot);
    void rememberForUndo();
    void renumber();
};
