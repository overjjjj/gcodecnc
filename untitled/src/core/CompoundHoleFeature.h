#pragma once

#include "../import/StepImporter.h"

#include <QStringList>

enum class HoleLayerKind {
    Cylindrical,
    ConicalCountersink,
    SealGroove
};

struct HoleLayer {
    HoleLayerKind kind = HoleLayerKind::Cylindrical;
    double diameter = 0.0;
    double startZ = 0.0;
    double endZ = 0.0;
    bool through = false;

    double depth() const;
};

struct CompoundHoleFeature {
    QString geometryRef;
    QVector3D entryPoint;
    QVector3D axis = {0.0f, 0.0f, 1.0f};
    FaceRegion region = FaceRegion::Unknown;
    QList<HoleLayer> layers;
    double confidence = 0.0;
    QStringList warnings;

    QStringList validateForFrontSetup() const;
    double totalDepth() const;
    bool isThrough() const;
};
