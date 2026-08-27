#pragma once

#include "../core/SelectionChainController.h"
#include "../strategies/StrategyBase.h"

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
    ChainGeometrySource geometrySource = ChainGeometrySource::Wire;
    ChainSelectionMode selectionMode = ChainSelectionMode::Chain;
    ChainMachiningSide machiningSide = ChainMachiningSide::Mixed;
    ChainSortStrategy sortStrategy = ChainSortStrategy::SelectionOrder;
    QString selectedBranchGeometryId = QStringLiteral("outer");
    bool closedContour = true;
    int startPointIndex = 0;
};

bool isManualContourChoiceStrategy(const QString &strategyId);
bool canReverseContourDirection(const ContourFeature &feature);
QVector<QVector3D> contourPreviewPoints(const ContourFeature &feature);
void applyContourMachiningChoice(ContourFeature &feature,
                                 StrategyParams &params,
                                 const ContourMachiningChoice &choice);
SelectionChain selectionChainForContourChoice(
    const ContourFeature &feature,
    const ContourMachiningChoice &choice,
    const QString &coordinateSystemId);
