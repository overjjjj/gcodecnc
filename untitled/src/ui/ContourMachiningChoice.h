#pragma once

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
    bool closedContour = true;
    int startPointIndex = 0;
};

bool isManualContourChoiceStrategy(const QString &strategyId);
bool canReverseContourDirection(const ContourFeature &feature);
QVector<QVector3D> contourPreviewPoints(const ContourFeature &feature);
void applyContourMachiningChoice(ContourFeature &feature,
                                 StrategyParams &params,
                                 const ContourMachiningChoice &choice);
