#pragma once

#include "PostProcessorBase.h"

class Cq8PostProcessor : public PostProcessorBase
{
public:
    QString id() const override { return QStringLiteral("cq8"); }
    QString displayName() const override { return QStringLiteral("CQ8 (Macro B)"); }
    QString fileExtension() const override { return QStringLiteral(".nc"); }

    QString wrapGCode(const QStringList &gcodeBlocks,
                      const PostProcessorOptions &opts) const override;
};
