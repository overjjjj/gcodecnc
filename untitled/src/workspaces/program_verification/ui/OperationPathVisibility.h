#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QSet>
#include <QRegularExpression>

// Preserve every original segment. A merged operation block remains visible until
// all its owners are hidden; untagged external programs have no inferred ownership.
inline QVector<bool> operationPathVisibility(const QString &code, const QVector<int> &sourceLines,
                                             const QSet<QString> &hidden)
{
    const auto lines = code.split(QLatin1Char('\n'));
    QVector<bool> lineVisible;
    const QRegularExpression marker(QStringLiteral("\\[op:([^\\]]+)\\]"));
    bool visible = true;
    for (const auto &line : lines) {
        auto matches = marker.globalMatch(line);
        if (matches.hasNext()) {
            visible = false;
            while (matches.hasNext())
                visible = !hidden.contains(matches.next().captured(1)) || visible;
        }
        lineVisible.append(visible);
    }
    QVector<bool> result;
    result.reserve(sourceLines.size());
    for (int line : sourceLines)
        result.append(line < 0 || line >= lineVisible.size() ? true : lineVisible.at(line));
    return result;
}
