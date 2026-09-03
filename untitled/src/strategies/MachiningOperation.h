#pragma once
#include "StrategyBase.h"
#include "../core/SelectionChainController.h"
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

enum class ToolpathState {
    Empty,
    Calculating,
    Valid,
    Stale,
    Error
};

struct MachiningOperation {
    QString        id;
    OperationType  opType     = OperationType::Hole;
    OperationStage stage      = OperationStage::RoughCut;
    QString        featureRef;   // human-readable label (e.g. "Hole D12 Z-20")
    QString        strategyId;
    int            toolId     = -1;
    StrategyParams params;
    bool           enabled = true;
    QStringList    geometryRefs;
    SelectionChain selectionEvidence;
    ToolpathState  toolpathState = ToolpathState::Empty;
    QStringList    warnings;

    // Exactly one of these is valid, depending on opType
    HoleFeature    holeFeature;
    ContourFeature contourFeature;

    void markToolpathValid()
    {
        toolpathState = ToolpathState::Valid;
        warnings.clear();
    }

    void markToolpathStale(const QString &reason)
    {
        toolpathState = ToolpathState::Stale;
        if (!reason.trimmed().isEmpty() && !warnings.contains(reason)) {
            warnings.append(reason);
        }
    }

    void markToolpathError(const QString &reason)
    {
        toolpathState = ToolpathState::Error;
        warnings = reason.trimmed().isEmpty() ? QStringList() : QStringList{reason};
    }
};
