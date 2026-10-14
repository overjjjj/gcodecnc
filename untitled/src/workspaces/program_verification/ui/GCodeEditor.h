#pragma once
#include <QPlainTextEdit>

class GCodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit GCodeEditor(QWidget *parent = nullptr);
    void setGCode(const QString &gcode);
    void setCurrentExecutionLine(int line);
    void clearCurrentExecutionLine();

signals:
    void lineSelected(int line);

private slots:
    void onCursorPositionChanged();

private:
    void updateCurrentLineSelection();

    int  m_executionLine = -1;
    bool m_updatingExecutionLine = false;
};
