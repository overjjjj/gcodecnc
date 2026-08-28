#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

struct DiameterBand {
    double minimumDiameterMm = 0.0;
    double maximumDiameterMm = 0.0;
};

struct ToolCuttingParameterSet {
    QString id;
    QString material;
    QString toolType;
    QString toolName;
    double minimumDiameterMm = 0.0;
    double maximumDiameterMm = 0.0;
    double plungeFeedMmPerMin = 0.0;
    double cuttingFeedMmPerMin = 0.0;
    double retractFeedMmPerMin = 0.0;
    double spindleRpm = 0.0;
    double stepDownMm = 0.0;
    double stepOverPercent = 0.0;
    double finishAllowanceMm = 0.0;
    QString coolant;
};

struct FeatureMatchingRule {
    QString id;
    QString featureKind;
    QString category;
    double minimumSizeMm = 0.0;
    double maximumSizeMm = 0.0;
    QList<DiameterBand> diameterBands;
    bool closed = false;
    bool through = false;
};

struct AutomationMachiningPlanStep {
    QString id;
    QString strategy;
    QString toolSelector;
    QString referenceTool;
    QString startExpression;
    QString depthExpression;
    QString coolant;
    bool enabled = true;
    QString unsupportedReason;
    bool cornerInsertionEnabled = false;
    bool dynamicMillingEnabled = false;
    bool threeDMachiningEnabled = false;
};

struct DeepHoleStageParameters {
    QString id;
    double normalSpindleRpm = 0.0;
    double normalFeedMmPerMin = 0.0;
    QString coolant;
    bool guideEnabled = false;
    double guideStartMm = 0.0;
    double guideEndMm = 0.0;
    double guideSpindleRpm = 0.0;
    double guideFeedMmPerMin = 0.0;
    bool slowEntryEnabled = false;
    double slowEntryStartMm = 0.0;
    double slowEntryEndMm = 0.0;
    double slowEntryPercent = 0.0;
    bool breakthroughEnabled = false;
    double breakthroughStartMm = 0.0;
    double breakthroughEndMm = 0.0;
    double breakthroughPercent = 0.0;
    bool retractEnabled = false;
    double retractSpindleRpm = 0.0;
    double retractFeedMmPerMin = 0.0;
    QString retractDirection;
    bool transitionEnabled = false;
    double minimumRapidDistancePercent = 0.0;
    double rapidFeedMmPerMin = 0.0;
    double maximumPeckMm = 0.0;
    double minimumPeckMm = 0.0;
    double peckReductionMm = 0.0;
    bool variableSpeedEnabled = false;
};

struct ThreadSpecification {
    QString id;
    QString system;
    QString standard;
    QString designation;
    double pilotHoleDiameterMm = 0.0;
    double pitchMm = 0.0;
    double threadDiameterMm = 0.0;
    QString candidateToolSelector;
    bool xyLayeringEnabled = false;
    int roughPassCount = 0;
    int finishPassCount = 0;
    double roughAllowanceMm = 0.0;
    double finishAllowanceMm = 0.0;
};

struct AutomationTemplateSourceMetadata {
    QString type;
    QString name;
    QString revision;
    QString checksumSha256;
};

struct AutomationTemplateParseResult;

struct AutomationTemplateDocument {
    int schemaVersion = 1;
    QString documentId;
    QString documentVersion;
    AutomationTemplateSourceMetadata source;
    QList<ToolCuttingParameterSet> toolCuttingParameterSets;
    QList<FeatureMatchingRule> featureMatchingRules;
    QList<AutomationMachiningPlanStep> machiningPlanSteps;
    QList<DeepHoleStageParameters> deepHoleStageParameters;
    QList<ThreadSpecification> threadSpecifications;

    bool ToJson(QByteArray *json, QStringList *errors = nullptr) const;
    static AutomationTemplateParseResult FromJson(const QByteArray &json);
};

struct AutomationTemplateParseResult {
    bool ok = false;
    QStringList errors;
    AutomationTemplateDocument document;
};
