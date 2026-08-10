#pragma once
#include <QString>
#include <QStringList>

struct PostProcessorOptions {
    bool   useAbsoluteCoords = true;
    bool   addComments       = true;
    QString programNumber    = "O0001";
    QString workOffset       = "G54";
    QStringList safeStartBlocks;
};

QStringList resolvedSafeStartBlocks(const PostProcessorOptions &opts);

class PostProcessorBase
{
public:
    virtual ~PostProcessorBase() = default;

    virtual QString id()          const = 0;
    virtual QString displayName() const = 0;

    virtual QString wrapGCode(const QStringList &gcodeBlocks,
                              const PostProcessorOptions &opts) const = 0;
    virtual QString fileExtension() const { return ".nc"; }
};
