#pragma once
#include "PostProcessorBase.h"

class SiemensPostProcessor : public PostProcessorBase
{
public:
    QString id()          const override { return "siemens"; }
    QString displayName() const override { return QStringLiteral("西门子 840D"); }
    QString fileExtension() const override { return ".mpf"; }

    QString wrapGCode(const QStringList &gcodeBlocks,
                      const PostProcessorOptions &opts) const override;
};
