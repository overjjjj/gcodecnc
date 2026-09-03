#include "GCodeHighlighter.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QRegularExpression>

GCodeHighlighter::GCodeHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    QTextCharFormat addressFormat;
    addressFormat.setForeground(QColor(70, 130, 220));
    addressFormat.setFontWeight(QFont::Bold);

    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(40, 40, 40));

    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(125, 72, 180));
    keywordFormat.setFontWeight(QFont::Bold);

    QTextCharFormat macroFormat;
    macroFormat.setForeground(QColor(180, 88, 28));
    macroFormat.setFontWeight(QFont::Bold);

    m_commentFormat.setForeground(QColor(120, 120, 120));
    m_commentFormat.setFontItalic(true);

    m_rules.append({QRegularExpression(QStringLiteral("\\b[GMXYZFST](?=\\s*[-+]?\\d)")),
                    addressFormat});
    m_rules.append({QRegularExpression(QStringLiteral("[-+]?\\d+(?:\\.\\d+)?")),
                    numberFormat});
    m_rules.append({QRegularExpression(QStringLiteral("\\b(?:MCALL|CYCLE83|CYCLE84|CYCLE85)\\b")),
                    keywordFormat});
    m_rules.append({QRegularExpression(QStringLiteral("\\bM(?:98|99)\\b")), macroFormat});
    m_rules.append({QRegularExpression(QStringLiteral("\\bO\\d+\\b")), macroFormat});
    m_rules.append({QRegularExpression(QStringLiteral("#\\d+\\b")), macroFormat});
}

void GCodeHighlighter::highlightBlock(const QString &text)
{
    for (const HighlightRule &rule : m_rules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    const int commentStart = text.indexOf(QLatin1Char(';'));
    if (commentStart >= 0) {
        setFormat(commentStart, text.length() - commentStart, m_commentFormat);
    }
}
