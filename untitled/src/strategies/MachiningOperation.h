#pragma once
#include "StrategyBase.h"
#include <QVariant>

enum class OperationType {
    Roughing,
    SemiFinish,
    Finish,
    Hole,
    Contour
};

enum class OperationStage {
    Setup,
    RoughCut,
    FinishCut,
    DeepHole,
    Cleanup
};

struct MachiningOperation {
    QString        id;
    OperationType  opType     = OperationType::Hole;
    OperationStage stage      = OperationStage::RoughCut;
    QString        featureRef;   // human-readable label (e.g. "Hole D12 Z-20")
    QString        strategyId;
    int            toolId     = -1;
    StrategyParams params;

    // Exactly one of these is valid, depending on opType
    HoleFeature    holeFeature;
    ContourFeature contourFeature;
};
