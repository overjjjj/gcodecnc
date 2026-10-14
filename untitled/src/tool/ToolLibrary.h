#pragma once
#include "ToolEntry.h"
#include <QObject>
#include <QVector>

class ToolLibrary : public QObject
{
    Q_OBJECT
public:
    static ToolLibrary &instance();

    bool            addTool(const ToolEntry &t);
    bool            updateTool(const ToolEntry &t);
    void            removeTool(int id);
    ToolEntry       tool(int id) const;
    QVector<ToolEntry> allTools() const { return m_tools; }
    QVector<ToolEntry> toolsByType(const QString &type) const;

    bool saveToFile(const QString &path);
    bool loadFromFile(const QString &path);
    QString lastError() const { return m_lastError; }

    void loadDefaults();

signals:
    // Loading refreshes dependents without writing the file back.
    void toolLibraryChanged(bool persist = true);

private:
    explicit ToolLibrary(QObject *parent = nullptr);
    QVector<ToolEntry> m_tools;
    int m_nextId = 1;
    QString m_lastError;
};
