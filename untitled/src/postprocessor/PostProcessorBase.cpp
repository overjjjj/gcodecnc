#include "PostProcessorBase.h"

QStringList resolvedSafeStartBlocks(const PostProcessorOptions &opts)
{
    QStringList blocks = opts.safeStartBlocks;
    const QString workOffset = opts.workOffset.trimmed().isEmpty()
                                   ? QStringLiteral("G54")
                                   : opts.workOffset.trimmed().toUpper();
    if (blocks.isEmpty()) {
        blocks = QStringList{
            QStringLiteral("G17 G40 G49 G80"),
            QStringLiteral("G21"),
            QStringLiteral("G90"),
            workOffset,
            QStringLiteral("G94")
        };
    }

    bool hasAbsoluteMode = false;
    bool hasWorkOffset = false;
    for (QString &block : blocks) {
        QStringList words = block.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (QString &word : words) {
            const QString upper = word.toUpper();
            if (upper == QStringLiteral("G90") || upper == QStringLiteral("G91")) {
                word = QStringLiteral("G90");
                hasAbsoluteMode = true;
            } else if (upper == QStringLiteral("G54") || upper == QStringLiteral("G55")
                       || upper == QStringLiteral("G56") || upper == QStringLiteral("G57")
                       || upper == QStringLiteral("G58") || upper == QStringLiteral("G59")) {
                word = workOffset;
                hasWorkOffset = true;
            }
        }
        block = words.join(QLatin1Char(' '));
    }
    if (!hasAbsoluteMode) {
        blocks.append(QStringLiteral("G90"));
    }
    if (!hasWorkOffset) {
        blocks.append(workOffset);
    }
    return blocks;
}
