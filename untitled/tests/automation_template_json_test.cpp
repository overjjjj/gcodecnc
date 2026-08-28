#include "../src/core/automation/AutomationTemplateDocument.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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

bool HasError(const AutomationTemplateParseResult &result,
              const QString &fragment)
{
    for (const QString &error : result.errors) {
        if (error.contains(fragment, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QByteArray ValidJson()
{
    return R"json({
        "schemaVersion": 1,
        "documentId": "factory-a-automation",
        "documentVersion": "2026.08.1",
        "source": {
            "type": "offline_conversion",
            "name": "approved-template-export",
            "revision": "rev-3",
            "checksumSha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        },
        "units": {
            "length": "mm",
            "feed": "mm/min",
            "spindle": "rpm",
            "percentage": "percent"
        },
        "toolCuttingParameterSets": [{
            "id": "aluminum-drill-3-8",
            "material": "aluminum-6061",
            "toolType": "drill",
            "toolName": "carbide-drill",
            "minimumDiameterMm": 3.0,
            "maximumDiameterMm": 8.0,
            "plungeFeedMmPerMin": 90.0,
            "cuttingFeedMmPerMin": 180.0,
            "retractFeedMmPerMin": 300.0,
            "spindleRpm": 4200.0,
            "stepDownMm": 1.5,
            "stepOverPercent": 40.0,
            "finishAllowanceMm": 0.1,
            "coolant": "external"
        }],
        "featureMatchingRules": [{
            "id": "counterbore-rule",
            "featureKind": "hole",
            "category": "counterbore",
            "minimumSizeMm": 3.0,
            "maximumSizeMm": 20.0,
            "diameterBands": [
                {"minimumDiameterMm": 3.0, "maximumDiameterMm": 8.0},
                {"minimumDiameterMm": 8.1, "maximumDiameterMm": 20.0}
            ],
            "closed": false,
            "through": false
        }],
        "machiningPlanSteps": [{
            "id": "drill-core",
            "strategy": "hole_peck",
            "toolSelector": "type:drill;diameter:D1",
            "referenceTool": "",
            "startExpression": "AT",
            "depthExpression": "H1-AT",
            "coolant": "external",
            "enabled": true,
            "unsupportedReason": "",
            "cornerInsertionEnabled": false,
            "dynamicMillingEnabled": false,
            "threeDMachiningEnabled": false
        }],
        "deepHoleStageParameters": [{
            "id": "deep-drill-default",
            "normalSpindleRpm": 3500.0,
            "normalFeedMmPerMin": 120.0,
            "coolant": "internal",
            "guideEnabled": true,
            "guideStartMm": 0.0,
            "guideEndMm": 4.0,
            "guideSpindleRpm": 900.0,
            "guideFeedMmPerMin": 40.0,
            "slowEntryEnabled": true,
            "slowEntryStartMm": 4.0,
            "slowEntryEndMm": 8.0,
            "slowEntryPercent": 50.0,
            "breakthroughEnabled": false,
            "breakthroughStartMm": 0.0,
            "breakthroughEndMm": 0.0,
            "breakthroughPercent": 50.0,
            "retractEnabled": false,
            "retractSpindleRpm": 1000.0,
            "retractFeedMmPerMin": 100.0,
            "retractDirection": "reverse",
            "transitionEnabled": true,
            "minimumRapidDistancePercent": 100.0,
            "rapidFeedMmPerMin": 500.0,
            "maximumPeckMm": 3.0,
            "minimumPeckMm": 1.0,
            "peckReductionMm": 0.5,
            "variableSpeedEnabled": false
        }],
        "threadSpecifications": [{
            "id": "metric-m10",
            "system": "metric",
            "standard": "ISO-metric",
            "designation": "M10x1.5",
            "pilotHoleDiameterMm": 8.5,
            "pitchMm": 1.5,
            "threadDiameterMm": 10.0,
            "candidateToolSelector": "type:tap;pitch:1.5",
            "xyLayeringEnabled": false,
            "roughPassCount": 0,
            "finishPassCount": 0,
            "roughAllowanceMm": 0.0,
            "finishAllowanceMm": 0.0
        }]
    })json";
}

QByteArray WithTopLevelValue(const QString &key, const QJsonValue &value)
{
    QJsonObject object = QJsonDocument::fromJson(ValidJson()).object();
    object.insert(key, value);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray WithArrayObjectValue(const QString &arrayName,
                               const QString &key,
                               const QJsonValue &value)
{
    QJsonObject object = QJsonDocument::fromJson(ValidJson()).object();
    QJsonArray array = object.value(arrayName).toArray();
    QJsonObject item = array.first().toObject();
    item.insert(key, value);
    array[0] = item;
    object.insert(arrayName, array);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray WithUnitValue(const QString &key, const QString &value)
{
    QJsonObject object = QJsonDocument::fromJson(ValidJson()).object();
    QJsonObject units = object.value(QStringLiteral("units")).toObject();
    units.insert(key, value);
    object.insert(QStringLiteral("units"), units);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const AutomationTemplateParseResult parsed =
        AutomationTemplateDocument::FromJson(ValidJson());
    if (!Expect(parsed.ok && parsed.errors.isEmpty(),
                "valid versioned automation JSON should parse") ||
        !Expect(parsed.document.toolCuttingParameterSets.size() == 1 &&
                    parsed.document.featureMatchingRules.size() == 1 &&
                    parsed.document.machiningPlanSteps.size() == 1 &&
                    parsed.document.deepHoleStageParameters.size() == 1 &&
                    parsed.document.threadSpecifications.size() == 1,
                "all frozen DTO collections should be populated") ||
        !Expect(parsed.document.machiningPlanSteps.first().coolant ==
                    QStringLiteral("external"),
                "coolant should be preserved as compatibility data")) {
        return 1;
    }

    QByteArray serialized;
    QStringList serializationErrors;
    if (!Expect(parsed.document.ToJson(&serialized, &serializationErrors),
                "a valid document should serialize") ||
        !Expect(serializationErrors.isEmpty(),
                "valid serialization should not report errors")) {
        return 1;
    }
    const AutomationTemplateParseResult reparsed =
        AutomationTemplateDocument::FromJson(serialized);
    if (!Expect(reparsed.ok &&
                    reparsed.document.source.checksumSha256 ==
                        parsed.document.source.checksumSha256 &&
                    reparsed.document.deepHoleStageParameters.first().maximumPeckMm ==
                        3.0,
                "version, source metadata and DTO values should round-trip")) {
        return 1;
    }

    const QList<QPair<QByteArray, QString>> rejectedDocuments{
        {WithTopLevelValue(QStringLiteral("futureField"), true),
         QStringLiteral("unknown field")},
        {WithTopLevelValue(QStringLiteral("schemaVersion"), 2),
         QStringLiteral("schemaVersion")},
        {WithUnitValue(QStringLiteral("feed"), QStringLiteral("inch/min")),
         QStringLiteral("unsupported unit")},
        {WithArrayObjectValue(QStringLiteral("toolCuttingParameterSets"),
                              QStringLiteral("feedUnit"),
                              QStringLiteral("inch/min")),
         QStringLiteral("unknown field")},
        {WithArrayObjectValue(QStringLiteral("toolCuttingParameterSets"),
                              QStringLiteral("stepOverPercent"), 101.0),
         QStringLiteral("stepOverPercent")},
        {WithArrayObjectValue(QStringLiteral("machiningPlanSteps"),
                              QStringLiteral("depthExpression"),
                              QStringLiteral("system('dir')")),
         QStringLiteral("depthExpression")},
        {WithArrayObjectValue(QStringLiteral("machiningPlanSteps"),
                              QStringLiteral("strategy"),
                              QStringLiteral("future_3d_strategy")),
         QStringLiteral("strategy")},
        {WithArrayObjectValue(QStringLiteral("machiningPlanSteps"),
                              QStringLiteral("cornerInsertionEnabled"), true),
         QStringLiteral("corner insertion")},
        {WithArrayObjectValue(QStringLiteral("machiningPlanSteps"),
                              QStringLiteral("dynamicMillingEnabled"), true),
         QStringLiteral("dynamic milling")},
        {WithArrayObjectValue(QStringLiteral("machiningPlanSteps"),
                              QStringLiteral("threeDMachiningEnabled"), true),
         QStringLiteral("3D machining")},
        {WithArrayObjectValue(QStringLiteral("deepHoleStageParameters"),
                              QStringLiteral("variableSpeedEnabled"), true),
         QStringLiteral("variable speed")},
        {WithArrayObjectValue(QStringLiteral("threadSpecifications"),
                              QStringLiteral("xyLayeringEnabled"), true),
         QStringLiteral("XY layering")}
    };
    for (const auto &rejected : rejectedDocuments) {
        const AutomationTemplateParseResult result =
            AutomationTemplateDocument::FromJson(rejected.first);
        if (!Expect(!result.ok && HasError(result, rejected.second),
                    "invalid or unsupported JSON should be explicitly rejected")) {
            return 1;
        }
    }

    QJsonObject overlap = QJsonDocument::fromJson(ValidJson()).object();
    QJsonArray sets = overlap.value(
        QStringLiteral("toolCuttingParameterSets")).toArray();
    QJsonObject secondSet = sets.first().toObject();
    secondSet.insert(QStringLiteral("id"), QStringLiteral("overlap"));
    secondSet.insert(QStringLiteral("minimumDiameterMm"), 7.0);
    secondSet.insert(QStringLiteral("maximumDiameterMm"), 12.0);
    sets.append(secondSet);
    overlap.insert(QStringLiteral("toolCuttingParameterSets"), sets);
    const AutomationTemplateParseResult overlapping =
        AutomationTemplateDocument::FromJson(
            QJsonDocument(overlap).toJson(QJsonDocument::Compact));
    if (!Expect(!overlapping.ok && HasError(overlapping, QStringLiteral("overlap")),
                "overlapping diameter tiers for the same key should be rejected")) {
        return 1;
    }

    return 0;
}
