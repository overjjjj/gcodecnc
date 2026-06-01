#include "GCodeEditor.h"
#include "GCodeHighlighter.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QTextBlock>

GCodeEditor::GCodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    QFont font("Courier New", 10);
    font.setFixedPitch(true);
    setFont(font);
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setStyleSheet("background:#ffffff; color:#202020;");
    new GCodeHighlighter(document());
    connect(this, &QPlainTextEdit::cursorPositionChanged,
            this, &GCodeEditor::onCursorPositionChanged);
}

void GCodeEditor::setGCode(const QString &gcode)
{
    m_updatingExecutionLine = true;
    setPlainText(gcode);
    m_updatingExecutionLine = false;
    clearCurrentExecutionLine();
}

void GCodeEditor::setCurrentExecutionLine(int line)
{
    if (line < 0 || line >= document()->blockCount()) {
        clearCurrentExecutionLine();
        return;
    }

    m_executionLine = line;
    m_updatingExecutionLine = true;
    QTextBlock block = document()->findBlockByNumber(line);
    if (block.isValid()) {
        QTextCursor c(block);
        setTextCursor(c);
        centerCursor();
    }
    m_updatingExecutionLine = false;
    updateCurrentLineSelection();
}

void GCodeEditor::clearCurrentExecutionLine()
{
    m_executionLine = -1;
    updateCurrentLineSelection();
}

void GCodeEditor::onCursorPositionChanged()
{
    if (m_updatingExecutionLine) {
        return;
    }
    emit lineSelected(textCursor().blockNumber());
}

void GCodeEditor::updateCurrentLineSelection()
{
    QList<QTextEdit::ExtraSelection> selections;
    if (m_executionLine >= 0) {
        const QTextBlock block = document()->findBlockByNumber(m_executionLine);
        if (block.isValid()) {
            QTextEdit::ExtraSelection selection;
            selection.format.setBackground(QColor(255, 238, 160));
            selection.format.setProperty(QTextFormat::FullWidthSelection, true);
            selection.cursor = QTextCursor(block);
            selection.cursor.clearSelection();
            selections.append(selection);
        }
    }
    setExtraSelections(selections);
}
