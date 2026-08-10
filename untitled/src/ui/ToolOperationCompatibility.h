#pragma once

#include "../strategies/StrategyBase.h"
#include "../tool/ToolEntry.h"

#include <QVector>

enum class ToolCompatibilitySeverity {
    Warning,
    Blocking
};

struct ToolCompatibilityIssue {
    QString code;
    ToolCompatibilitySeverity severity = ToolCompatibilitySeverity::Warning;
    QString message;
};

struct ToolCompatibilityReport {
    QVector<ToolCompatibilityIssue> issues;

    bool hasBlockingIssues() const;
    bool hasWarnings() const;
};

ToolCompatibilityReport reviewToolCompatibility(const QString &strategyId,
                                                const ToolEntry &tool,
                                                const HoleFeature &feature,
                                                bool chinese,
                                                double requestedDepth = -1.0);
ToolCompatibilityReport reviewToolCompatibility(const QString &strategyId,
                                                const ToolEntry &tool,
                                                const ContourFeature &feature,
                                                bool chinese,
                                                double requestedDepth = -1.0);
