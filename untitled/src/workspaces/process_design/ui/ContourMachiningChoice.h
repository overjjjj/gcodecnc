#pragma once

#include "../../../strategies/StrategyBase.h"
#include "../selection/SelectionChainController.h"

enum class ContourTraversalDirection {
    Forward,
    Reverse
};

enum class ContourCompensationChoice {
    CamOffsetG40,
    LeftG41,
    RightG42
};

struct ContourMachiningChoice {
    ContourTraversalDirection direction = ContourTraversalDirection::Forward;
    ContourCompensationChoice compensation = ContourCompensationChoice::LeftG41;
    bool closedContour = true;
    int startPointIndex = 0;
    ChainMachiningSide machiningSide = ChainMachiningSide::Mixed;
};

bool isManualContourChoiceStrategy(const QString &strategyId);
bool canReverseContourDirection(const ContourFeature &feature);
QVector<QVector3D> contourPreviewPoints(const ContourFeature &feature);
void applyContourMachiningChoice(ContourFeature &feature,
                                 StrategyParams &params,
                                 const ContourMachiningChoice &choice);

struct MeshData;
bool bindContourMachiningChoice(SelectionChain &selection,
                                const QVector<QVector3D> &sourcePoints,
                                const ContourMachiningChoice &choice,
                                const MeshData &mesh);
