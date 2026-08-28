#pragma once

#include "../core/automation/AutomationTemplateDocument.h"

enum class ToolCuttingParameterMatchError {
    None,
    MaterialRequired,
    ToolTypeRequired,
    InvalidDiameter,
    NoMatch,
    AmbiguousMatch
};

struct ToolCuttingParameterMatchRequest {
    QString material;
    QString toolType;
    QString toolName;
    double toolDiameterMm = 0.0;
};

struct ToolCuttingParameterMatchSource {
    QString documentId;
    QString documentVersion;
    QString effectiveDate;
};

struct ToolCuttingParameterMatchResult {
    bool matched = false;
    ToolCuttingParameterMatchError error = ToolCuttingParameterMatchError::None;
    QString rejectionReason;
    QString explanation;
    ToolCuttingParameterSet parameterSet;
    ToolCuttingParameterMatchSource source;
};

QString ToolCuttingParameterMatchErrorCode(ToolCuttingParameterMatchError error);

class ToolCuttingParameterService {
public:
    static ToolCuttingParameterMatchResult Match(
        const AutomationTemplateDocument &document,
        const ToolCuttingParameterMatchRequest &request);
};
