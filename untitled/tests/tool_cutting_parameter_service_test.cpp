#include "../src/services/ToolCuttingParameterService.h"

#include <QCoreApplication>

#include <cmath>
#include <iostream>

namespace {

bool Expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

ToolCuttingParameterSet MakeParameter(const QString &id,
                                      double minimumDiameterMm,
                                      bool minimumInclusive,
                                      double maximumDiameterMm,
                                      bool maximumInclusive)
{
    ToolCuttingParameterSet parameter;
    parameter.id = id;
    parameter.material = QStringLiteral("steel");
    parameter.toolType = QStringLiteral("end_mill");
    parameter.toolName = QStringLiteral("EM-10");
    parameter.minimumDiameterMm = minimumDiameterMm;
    parameter.maximumDiameterMm = maximumDiameterMm;
    parameter.minimumInclusive = minimumInclusive;
    parameter.maximumInclusive = maximumInclusive;
    parameter.plungeFeedMmPerMin = 100.0;
    parameter.cuttingFeedMmPerMin = 200.0;
    parameter.retractFeedMmPerMin = 300.0;
    parameter.spindleRpm = 4000.0;
    parameter.stepDownMm = 1.0;
    parameter.stepOverPercent = 40.0;
    parameter.finishAllowanceMm = 0.1;
    parameter.coolant = QStringLiteral("external");
    return parameter;
}

AutomationTemplateDocument MakeDocument()
{
    AutomationTemplateDocument document;
    document.documentId = QStringLiteral("factory-a-tooling");
    document.documentVersion = QStringLiteral("2026.08.1");
    document.source.effectiveDate = QStringLiteral("2026-08-28");
    return document;
}

ToolCuttingParameterMatchRequest MakeRequest(double diameterMm)
{
    ToolCuttingParameterMatchRequest request;
    request.material = QStringLiteral("steel");
    request.toolType = QStringLiteral("end_mill");
    request.toolDiameterMm = diameterMm;
    return request;
}

bool SameParameter(const ToolCuttingParameterSet &left,
                   const ToolCuttingParameterSet &right)
{
    return left.id == right.id && left.material == right.material
        && left.toolType == right.toolType && left.toolName == right.toolName
        && left.minimumDiameterMm == right.minimumDiameterMm
        && left.maximumDiameterMm == right.maximumDiameterMm
        && left.minimumInclusive == right.minimumInclusive
        && left.maximumInclusive == right.maximumInclusive
        && left.plungeFeedMmPerMin == right.plungeFeedMmPerMin
        && left.cuttingFeedMmPerMin == right.cuttingFeedMmPerMin
        && left.retractFeedMmPerMin == right.retractFeedMmPerMin
        && left.spindleRpm == right.spindleRpm
        && left.stepDownMm == right.stepDownMm
        && left.stepOverPercent == right.stepOverPercent
        && left.finishAllowanceMm == right.finishAllowanceMm
        && left.coolant == right.coolant;
}

bool TestIntervalEndpoints()
{
    AutomationTemplateDocument document = MakeDocument();
    document.toolCuttingParameterSets = {
        MakeParameter(QStringLiteral("closed-closed"), 1.0, true, 2.0, true),
        MakeParameter(QStringLiteral("open-closed"), 3.0, false, 4.0, true),
        MakeParameter(QStringLiteral("closed-open"), 5.0, true, 6.0, false),
        MakeParameter(QStringLiteral("open-open"), 7.0, false, 8.0, false)
    };

    const auto match = [&document](double diameterMm) {
        return ToolCuttingParameterService::Match(document,
                                                   MakeRequest(diameterMm));
    };
    return Expect(match(1.0).matched, "[min,max] should include min")
        && Expect(match(2.0).matched, "[min,max] should include max")
        && Expect(!match(3.0).matched, "(min,max] should exclude min")
        && Expect(match(4.0).matched, "(min,max] should include max")
        && Expect(match(5.0).matched, "[min,max) should include min")
        && Expect(!match(6.0).matched, "[min,max) should exclude max")
        && Expect(!match(7.0).matched, "(min,max) should exclude min")
        && Expect(match(7.5).matched, "(min,max) should include interior")
        && Expect(!match(8.0).matched, "(min,max) should exclude max");
}

bool TestExactFieldsAndOptionalName()
{
    AutomationTemplateDocument document = MakeDocument();
    ToolCuttingParameterSet named =
        MakeParameter(QStringLiteral("named-tool"), 9.0, true, 11.0, true);
    ToolCuttingParameterSet other = named;
    other.id = QStringLiteral("other-tool");
    other.toolName = QStringLiteral("EM-10B");
    document.toolCuttingParameterSets = {named, other};

    ToolCuttingParameterMatchRequest request = MakeRequest(10.0);
    request.toolName = QStringLiteral("EM-10");
    const ToolCuttingParameterMatchResult namedResult =
        ToolCuttingParameterService::Match(document, request);
    if (!Expect(namedResult.matched
                    && namedResult.parameterSet.id == QStringLiteral("named-tool"),
                "specified tool name should match exactly")) {
        return false;
    }

    request.material = QStringLiteral("Steel");
    if (!Expect(ToolCuttingParameterService::Match(document, request).error
                    == ToolCuttingParameterMatchError::NoMatch,
                "material matching should be exact")) {
        return false;
    }
    request.material = QStringLiteral("steel");
    request.toolType = QStringLiteral("drill");
    if (!Expect(ToolCuttingParameterService::Match(document, request).error
                    == ToolCuttingParameterMatchError::NoMatch,
                "tool type matching should be exact")) {
        return false;
    }
    request.toolType = QStringLiteral("end_mill");
    request.toolName = QStringLiteral("em-10");
    if (!Expect(ToolCuttingParameterService::Match(document, request).error
                    == ToolCuttingParameterMatchError::NoMatch,
                "tool name matching should be exact")) {
        return false;
    }

    document.toolCuttingParameterSets = {named};
    request.toolName.clear();
    return Expect(ToolCuttingParameterService::Match(document, request).matched,
                  "an omitted tool name should not filter a unique match");
}

bool TestRejectionsAndStableErrors()
{
    AutomationTemplateDocument document = MakeDocument();
    const ToolCuttingParameterSet first =
        MakeParameter(QStringLiteral("first"), 9.0, true, 11.0, true);
    ToolCuttingParameterSet second = first;
    second.id = QStringLiteral("second");
    document.toolCuttingParameterSets = {first};

    ToolCuttingParameterMatchRequest request = MakeRequest(12.0);
    ToolCuttingParameterMatchResult result =
        ToolCuttingParameterService::Match(document, request);
    if (!Expect(result.error == ToolCuttingParameterMatchError::NoMatch
                    && ToolCuttingParameterMatchErrorCode(result.error)
                        == QStringLiteral("TOOLING_PARAMETER_NOT_FOUND"),
                "out-of-range input should have a stable no-match error")) {
        return false;
    }

    request.toolDiameterMm = 10.0;
    document.toolCuttingParameterSets.append(second);
    result = ToolCuttingParameterService::Match(document, request);
    if (!Expect(result.error == ToolCuttingParameterMatchError::AmbiguousMatch
                    && ToolCuttingParameterMatchErrorCode(result.error)
                        == QStringLiteral("TOOLING_PARAMETER_AMBIGUOUS"),
                "multiple equal matches should be rejected as ambiguous")) {
        return false;
    }

    request.toolDiameterMm = 0.0;
    result = ToolCuttingParameterService::Match(document, request);
    if (!Expect(result.error == ToolCuttingParameterMatchError::InvalidDiameter
                    && ToolCuttingParameterMatchErrorCode(result.error)
                        == QStringLiteral("TOOLING_DIAMETER_INVALID"),
                "zero diameter should be rejected")) {
        return false;
    }
    request.toolDiameterMm = std::nan("");
    if (!Expect(ToolCuttingParameterService::Match(document, request).error
                    == ToolCuttingParameterMatchError::InvalidDiameter,
                "non-finite diameter should be rejected")) {
        return false;
    }

    request = MakeRequest(10.0);
    request.material.clear();
    if (!Expect(ToolCuttingParameterService::Match(document, request).error
                    == ToolCuttingParameterMatchError::MaterialRequired,
                "missing material should be rejected")) {
        return false;
    }
    request = MakeRequest(10.0);
    request.toolType.clear();
    return Expect(ToolCuttingParameterService::Match(document, request).error
                      == ToolCuttingParameterMatchError::ToolTypeRequired,
                  "missing tool type should be rejected");
}

bool TestSnapshotProvenanceAndInputImmutability()
{
    AutomationTemplateDocument document = MakeDocument();
    document.toolCuttingParameterSets = {
        MakeParameter(QStringLiteral("traceable"), 9.0, true, 11.0, true)
    };
    const QList<ToolCuttingParameterSet> before =
        document.toolCuttingParameterSets;

    const ToolCuttingParameterMatchResult result =
        ToolCuttingParameterService::Match(document, MakeRequest(10.0));
    if (!Expect(result.matched
                    && result.source.documentId == document.documentId
                    && result.source.documentVersion == document.documentVersion
                    && result.source.effectiveDate
                        == document.source.effectiveDate,
                "matched snapshot should retain document provenance")) {
        return false;
    }
    if (!Expect(result.parameterSet.coolant == QStringLiteral("external"),
                "coolant should be copied as compatibility data")) {
        return false;
    }
    if (!Expect(result.explanation.contains(QStringLiteral("traceable")),
                "match explanation should identify the selected record")) {
        return false;
    }
    return Expect(document.toolCuttingParameterSets.size() == before.size()
                      && SameParameter(document.toolCuttingParameterSets.first(),
                                       before.first()),
                  "matching should not modify the input parameter list");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    if (!TestIntervalEndpoints() || !TestExactFieldsAndOptionalName()
        || !TestRejectionsAndStableErrors()
        || !TestSnapshotProvenanceAndInputImmutability()) {
        return 1;
    }

    std::cout << "Tool cutting parameter service tests passed." << std::endl;
    return 0;
}
