#pragma once

#include "../core/CompoundHoleFeature.h"
#include "../core/ProcessTemplateLibrary.h"
#include "OperationFactory.h"

#include <QList>
#include <QMap>

struct AutoHoleToolInfo {
    int id = -1;
    QString type;
    double diameter = 0.0;
    double fluteLength = 0.0;
    double totalLength = 0.0;
};

enum class AutoHoleShape {
    Any,
    Single,
    Counterbore,
    Countersink
};

struct AutoHolePlanStep {
    QString id;
    QString strategyId;
    QString toolExpression;
    QString startExpression;
    QString depthExpression;
    int layerIndex = 0;
    bool countersinkStep = false;
};

enum class AutoCandidateState {
    Draft,
    Conflict,
    Stale,
    Rejected
};

struct AutoHolePlan {
    QString id;
    QString version;
    QString strategyId;
    int toolId = -1;
    QString startExpression;
    QString depthExpression;
    OperationParameterLayers layers;
    AutoHoleShape shape = AutoHoleShape::Any;
    QMap<QString, double> formulaVariables;
    QList<AutoHolePlanStep> steps;
};

struct AutoHoleCandidate {
    QString id;
    QString geometryRef;
    HoleFeature holeFeature;
    CompoundHoleFeature compoundHole;
    QList<AutoHolePlan> plans;
    QString selectedPlanId;
    StrategyParams manualOverrides;
    bool manuallyEdited = false;
    bool cancelCountersink = false;
    AutoCandidateState state = AutoCandidateState::Draft;
    QStringList reasons;
};

struct AutoHoleConfirmationResult {
    bool ok = false;
    QStringList errors;
    MachiningOperation operation;
    QList<MachiningOperation> operations;
};

class AutoHolePlanningService
{
public:
    static QList<AutoHolePlan> BuildPlans(
        const ProcessTemplateLibrary &library,
        QStringList *errors = nullptr);

    static QList<AutoHoleCandidate> Recognize(
        const QVector<MachiningFeature> &selectedFeatures,
        const QList<AutoHolePlan> &plans,
        const QList<AutoHoleCandidate> &previousCandidates);

    static QList<AutoHoleCandidate> RecognizeCompound(
        const QList<CompoundHoleFeature> &selectedFeatures,
        const QList<AutoHolePlan> &plans,
        const QList<AutoHoleCandidate> &previousCandidates);

    static AutoHoleConfirmationResult Confirm(
        const AutoHoleCandidate &candidate,
        const QMap<int, AutoHoleToolInfo> &availableTools,
        const ProcessContext &context,
        double allowance);

    static AutoHoleConfirmationResult ConfirmCompound(
        const AutoHoleCandidate &candidate,
        const QMap<int, AutoHoleToolInfo> &availableTools,
        const ProcessContext &context,
        double allowance);
};
