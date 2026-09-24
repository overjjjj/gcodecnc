#pragma once
#include "../../../core/StrategyParams.h"
#include "../../../import/StepImporter.h"
#include "../selection/SelectionChainController.h"
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

// 刀路的计算状态独立于人工确认：正式工序初始仍为 Empty，输入变化后转为 Stale。
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
    QString        featureRef;   // 供界面显示的标签，不作为几何身份依据。
    QString        strategyId;
    int            toolId     = -1;
    StrategyParams params;
    bool           enabled = true;
    QStringList    geometryRefs;
    SelectionChain selectionEvidence;  // 保留确认时的选择、来源和 Setup 证据。
    ToolpathState  toolpathState = ToolpathState::Empty;
    QStringList    warnings;

    // opType 为 Hole 时使用 holeFeature，其余工序使用 contourFeature。
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
