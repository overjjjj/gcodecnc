#include "ToolCuttingParameterService.h"

#include <cmath>

namespace {

bool IsWithinDiameterRange(double diameterMm,
                           const ToolCuttingParameterSet &parameterSet)
{
    if (diameterMm < parameterSet.minimumDiameterMm
        || (diameterMm == parameterSet.minimumDiameterMm
            && !parameterSet.minimumInclusive)) {
        return false;
    }
    if (diameterMm > parameterSet.maximumDiameterMm
        || (diameterMm == parameterSet.maximumDiameterMm
            && !parameterSet.maximumInclusive)) {
        return false;
    }
    return true;
}

ToolCuttingParameterMatchResult Rejected(
    ToolCuttingParameterMatchError error, const QString &reason)
{
    ToolCuttingParameterMatchResult result;
    result.error = error;
    result.rejectionReason = reason;
    return result;
}

} // namespace

QString ToolCuttingParameterMatchErrorCode(ToolCuttingParameterMatchError error)
{
    switch (error) {
    case ToolCuttingParameterMatchError::None:
        return QStringLiteral("TOOLING_NONE");
    case ToolCuttingParameterMatchError::MaterialRequired:
        return QStringLiteral("TOOLING_MATERIAL_REQUIRED");
    case ToolCuttingParameterMatchError::ToolTypeRequired:
        return QStringLiteral("TOOLING_TOOL_TYPE_REQUIRED");
    case ToolCuttingParameterMatchError::InvalidDiameter:
        return QStringLiteral("TOOLING_DIAMETER_INVALID");
    case ToolCuttingParameterMatchError::NoMatch:
        return QStringLiteral("TOOLING_PARAMETER_NOT_FOUND");
    case ToolCuttingParameterMatchError::AmbiguousMatch:
        return QStringLiteral("TOOLING_PARAMETER_AMBIGUOUS");
    }
    return QStringLiteral("TOOLING_UNKNOWN_ERROR");
}

ToolCuttingParameterMatchResult ToolCuttingParameterService::Match(
    const AutomationTemplateDocument &document,
    const ToolCuttingParameterMatchRequest &request)
{
    if (request.material.trimmed().isEmpty()) {
        return Rejected(ToolCuttingParameterMatchError::MaterialRequired,
                        QStringLiteral("Material is required."));
    }
    if (request.toolType.trimmed().isEmpty()) {
        return Rejected(ToolCuttingParameterMatchError::ToolTypeRequired,
                        QStringLiteral("Tool type is required."));
    }
    if (!std::isfinite(request.toolDiameterMm) || request.toolDiameterMm <= 0.0) {
        return Rejected(ToolCuttingParameterMatchError::InvalidDiameter,
                        QStringLiteral("Tool diameter must be finite and positive."));
    }

    QList<const ToolCuttingParameterSet *> matches;
    for (const ToolCuttingParameterSet &parameterSet :
         document.toolCuttingParameterSets) {
        if (parameterSet.material != request.material
            || parameterSet.toolType != request.toolType
            || (!request.toolName.isEmpty()
                && parameterSet.toolName != request.toolName)
            || !IsWithinDiameterRange(request.toolDiameterMm, parameterSet)) {
            continue;
        }
        matches.append(&parameterSet);
    }

    if (matches.isEmpty()) {
        return Rejected(ToolCuttingParameterMatchError::NoMatch,
                        QStringLiteral("No parameter set matches the request."));
    }
    if (matches.size() != 1) {
        return Rejected(ToolCuttingParameterMatchError::AmbiguousMatch,
                        QStringLiteral("Multiple parameter sets match the request."));
    }

    const ToolCuttingParameterSet &parameterSet = *matches.first();
    ToolCuttingParameterMatchResult result;
    result.matched = true;
    result.parameterSet = parameterSet;
    result.source.documentId = document.documentId;
    result.source.documentVersion = document.documentVersion;
    result.source.effectiveDate = document.source.effectiveDate;
    result.explanation = QStringLiteral(
        "Matched parameter set '%1' using exact material, tool type, tool name, "
        "and diameter range conditions.").arg(parameterSet.id);
    return result;
}
