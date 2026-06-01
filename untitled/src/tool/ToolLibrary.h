#pragma once
#include "ToolEntry.h"
#include <QObject>
#include <QVector>

class ToolLibrary : public QObject
{
    Q_OBJECT
public:
    static ToolLibrary &instance();

    void            addTool(const ToolEntry &t);
    void            updateTool(const ToolEntry &t);
    void            removeTool(int id);
    ToolEntry       tool(int id) const;
    QVector<ToolEntry> allTools() const { return m_tools; }
    QVector<ToolEntry> toolsByType(const QString &type) const;

    bool saveToFile(const QString &path);
    bool loadFromFile(const QString &path);

    void loadDefaults();

signals:
    void toolLibraryChanged();

private:
    explicit ToolLibrary(QObject *parent = nullptr);
    QVector<ToolEntry> m_tools;
    int m_nextId = 1;
};
