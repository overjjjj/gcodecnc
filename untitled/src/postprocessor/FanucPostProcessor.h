#pragma once
#include "PostProcessorBase.h"

class FanucPostProcessor : public PostProcessorBase
{
public:
    QString id()          const override { return "fanuc"; }
    QString displayName() const override { return QStringLiteral("发那科 0i"); }
    QString fileExtension() const override { return ".nc"; }

    QString wrapGCode(const QStringList &gcodeBlocks,
                      const PostProcessorOptions &opts) const override;
};
