#include "Cq8PostProcessor.h"

#include "FanucPostProcessor.h"

// 中文说明：将通用刀路块包装为 CQ8 程序格式；不改变刀路几何，只负责方言级
// 程序头、块边界和收尾，具体安全约束由生成服务和校验器提前完成。
QString Cq8PostProcessor::wrapGCode(const QStringList &gcodeBlocks,
                                    const PostProcessorOptions &opts) const
{
    QString output = FanucPostProcessor().wrapGCode(gcodeBlocks, opts);
    output.replace(QStringLiteral("Post-processor: Fanuc 0i"),
                   QStringLiteral("Post-processor: CQ8 Macro B"));
    return output;
}
