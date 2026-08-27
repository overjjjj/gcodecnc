#pragma once

#include <QList>
#include <QString>
#include <QVector3D>

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
    QVector3D center;
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
    };

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
