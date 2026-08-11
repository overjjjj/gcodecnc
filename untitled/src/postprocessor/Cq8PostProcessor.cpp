#include "Cq8PostProcessor.h"

#include "FanucPostProcessor.h"

QString Cq8PostProcessor::wrapGCode(const QStringList &gcodeBlocks,
                                    const PostProcessorOptions &opts) const
{
    QString output = FanucPostProcessor().wrapGCode(gcodeBlocks, opts);
    output.replace(QStringLiteral("Post-processor: Fanuc 0i"),
                   QStringLiteral("Post-processor: CQ8 Macro B"));
    return output;
}
