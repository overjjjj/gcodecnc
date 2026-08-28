#include "AutomationTemplateDocument.h"

#include "../../services/RestrictedFormulaEvaluator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr int kSupportedSchemaVersion = 1;

void AddError(QStringList *errors, const QString &path, const QString &message)
{
    errors->append(QStringLiteral("%1: %2").arg(path, message));
}

void RejectUnknownFields(const QJsonObject &object,
                         const QSet<QString> &allowed,
                         const QString &path,
                         QStringList *errors)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) {
            AddError(errors, path,
                     QStringLiteral("unknown field '%1'").arg(it.key()));
        }
    }
}

bool RequireString(const QJsonObject &object,
                   const QString &key,
                   const QString &path,
                   QStringList *errors,
                   QString *value,
                   bool allowEmpty = false)
{
    const QJsonValue json_value = object.value(key);
    if (!json_value.isString() ||
        (!allowEmpty && json_value.toString().trimmed().isEmpty())) {
        AddError(errors, path + QLatin1Char('.') + key,
                 allowEmpty ? QStringLiteral("must be a string")
                            : QStringLiteral("must be a non-empty string"));
        return false;
    }
    *value = json_value.toString();
    return true;
}

bool RequireNumber(const QJsonObject &object,
                   const QString &key,
                   const QString &path,
                   QStringList *errors,
                   double *value)
{
    const QJsonValue json_value = object.value(key);
    if (!json_value.isDouble() || !std::isfinite(json_value.toDouble())) {
        AddError(errors, path + QLatin1Char('.') + key,
                 QStringLiteral("must be a finite number"));
        return false;
    }
    *value = json_value.toDouble();
    return true;
}

bool RequireInteger(const QJsonObject &object,
                    const QString &key,
                    const QString &path,
                    QStringList *errors,
                    int *value)
{
    double number = 0.0;
    if (!RequireNumber(object, key, path, errors, &number)) {
        return false;
    }
    if (number != std::floor(number) || number < 0.0 ||
        number > static_cast<double>(std::numeric_limits<int>::max())) {
        AddError(errors, path + QLatin1Char('.') + key,
                 QStringLiteral("must be a non-negative integer"));
        return false;
    }
    *value = static_cast<int>(number);
    return true;
}

bool RequireBool(const QJsonObject &object,
                 const QString &key,
                 const QString &path,
                 QStringList *errors,
                 bool *value)
{
    const QJsonValue json_value = object.value(key);
    if (!json_value.isBool()) {
        AddError(errors, path + QLatin1Char('.') + key,
                 QStringLiteral("must be a boolean"));
        return false;
    }
    *value = json_value.toBool();
    return true;
}

bool RequireArray(const QJsonObject &object,
                  const QString &key,
                  const QString &path,
                  QStringList *errors,
                  QJsonArray *value)
{
    const QJsonValue json_value = object.value(key);
    if (!json_value.isArray()) {
        AddError(errors, path + QLatin1Char('.') + key,
                 QStringLiteral("must be an array"));
        return false;
    }
    *value = json_value.toArray();
    return true;
}

void RequirePositive(double value,
                     const QString &path,
                     QStringList *errors)
{
    if (!(value > 0.0)) {
        AddError(errors, path, QStringLiteral("must be greater than zero"));
    }
}

void RequireNonNegative(double value,
                        const QString &path,
                        QStringList *errors)
{
    if (value < 0.0) {
        AddError(errors, path, QStringLiteral("must not be negative"));
    }
}

void RequirePercent(double value,
                    const QString &path,
                    QStringList *errors)
{
    if (!(value > 0.0 && value <= 100.0)) {
        AddError(errors, path,
                 QStringLiteral("must be greater than 0 and at most 100"));
    }
}

bool IsAllowedCoolant(const QString &coolant)
{
    static const QSet<QString> kAllowed{
        QStringLiteral("none"), QStringLiteral("external"),
        QStringLiteral("internal"), QStringLiteral("air"),
        QStringLiteral("internal_external")};
    return kAllowed.contains(coolant);
}

bool IsAllowedStrategy(const QString &strategy)
{
    static const QSet<QString> kAllowed{
        QStringLiteral("hole_spot"),
        QStringLiteral("hole_peck"),
        QStringLiteral("hole_peck_g73"),
        QStringLiteral("hole_deephole"),
        QStringLiteral("hole_chamfer"),
        QStringLiteral("hole_reaming"),
        QStringLiteral("hole_bore_g86"),
        QStringLiteral("hole_tapping"),
        QStringLiteral("hole_thread_mill"),
        QStringLiteral("hole_circular_mill"),
        QStringLiteral("mill_pocket_rough"),
        QStringLiteral("mill_pocket_finish"),
        QStringLiteral("mill_pocket_floor_finish"),
        QStringLiteral("mill_slot"),
        QStringLiteral("mill_blind_slot"),
        QStringLiteral("mill_island"),
        QStringLiteral("mill_contour_finish"),
        QStringLiteral("mill_closed_contour"),
        QStringLiteral("mill_open_contour"),
        QStringLiteral("mill_outer_chamfer")};
    return kAllowed.contains(strategy);
}

void ValidateCoolant(const QString &coolant,
                     const QString &path,
                     QStringList *errors)
{
    if (!IsAllowedCoolant(coolant)) {
        AddError(errors, path, QStringLiteral("uses an unknown coolant value"));
    }
}

void ValidateExpression(const QString &expression,
                        const QString &path,
                        QStringList *errors)
{
    QMap<QString, double> variables;
    const QRegularExpression variable_pattern(
        QStringLiteral("\\b[A-Za-z][A-Za-z0-9]*\\b"));
    QRegularExpressionMatchIterator matches =
        variable_pattern.globalMatch(expression);
    while (matches.hasNext()) {
        variables.insert(matches.next().captured(), 1.0);
    }
    const FormulaEvaluationResult result =
        RestrictedFormulaEvaluator::Evaluate(expression, variables);
    if (!result.ok) {
        AddError(errors, path,
                 QStringLiteral("contains an invalid restricted expression: %1")
                     .arg(result.error));
    }
}

void ParseSource(const QJsonObject &object,
                 AutomationTemplateSourceMetadata *source,
                 QStringList *errors)
{
    const QString path = QStringLiteral("source");
    RejectUnknownFields(
        object,
        {QStringLiteral("type"), QStringLiteral("name"),
         QStringLiteral("revision"), QStringLiteral("checksumSha256")},
        path, errors);
    RequireString(object, QStringLiteral("type"), path, errors, &source->type);
    RequireString(object, QStringLiteral("name"), path, errors, &source->name);
    RequireString(object, QStringLiteral("revision"), path, errors,
                  &source->revision);
    RequireString(object, QStringLiteral("checksumSha256"), path, errors,
                  &source->checksumSha256);
    const QSet<QString> allowed_types{
        QStringLiteral("manual"), QStringLiteral("offline_conversion")};
    if (!allowed_types.contains(source->type)) {
        AddError(errors, path + QStringLiteral(".type"),
                 QStringLiteral("must be 'manual' or 'offline_conversion'"));
    }
    const QRegularExpression checksum_pattern(
        QStringLiteral("^[0-9A-Fa-f]{64}$"));
    if (!checksum_pattern.match(source->checksumSha256).hasMatch()) {
        AddError(errors, path + QStringLiteral(".checksumSha256"),
                 QStringLiteral("must contain 64 hexadecimal characters"));
    }
}

void ParseUnits(const QJsonObject &object, QStringList *errors)
{
    const QString path = QStringLiteral("units");
    RejectUnknownFields(
        object,
        {QStringLiteral("length"), QStringLiteral("feed"),
         QStringLiteral("spindle"), QStringLiteral("percentage")},
        path, errors);
    const QList<QPair<QString, QString>> required{
        {QStringLiteral("length"), QStringLiteral("mm")},
        {QStringLiteral("feed"), QStringLiteral("mm/min")},
        {QStringLiteral("spindle"), QStringLiteral("rpm")},
        {QStringLiteral("percentage"), QStringLiteral("percent")}};
    for (const auto &unit : required) {
        QString value;
        if (RequireString(object, unit.first, path, errors, &value) &&
            value != unit.second) {
            AddError(errors, path + QLatin1Char('.') + unit.first,
                     QStringLiteral("unsupported unit '%1'; expected '%2'")
                         .arg(value, unit.second));
        }
    }
}

ToolCuttingParameterSet ParseToolCuttingParameterSet(
    const QJsonObject &object, const QString &path, QStringList *errors)
{
    RejectUnknownFields(
        object,
        {QStringLiteral("id"), QStringLiteral("material"),
         QStringLiteral("toolType"), QStringLiteral("toolName"),
         QStringLiteral("minimumDiameterMm"),
         QStringLiteral("maximumDiameterMm"),
         QStringLiteral("plungeFeedMmPerMin"),
         QStringLiteral("cuttingFeedMmPerMin"),
         QStringLiteral("retractFeedMmPerMin"),
         QStringLiteral("spindleRpm"), QStringLiteral("stepDownMm"),
         QStringLiteral("stepOverPercent"),
         QStringLiteral("finishAllowanceMm"), QStringLiteral("coolant")},
        path, errors);
    ToolCuttingParameterSet set;
    RequireString(object, QStringLiteral("id"), path, errors, &set.id);
    RequireString(object, QStringLiteral("material"), path, errors,
                  &set.material);
    RequireString(object, QStringLiteral("toolType"), path, errors,
                  &set.toolType);
    RequireString(object, QStringLiteral("toolName"), path, errors,
                  &set.toolName);
    RequireNumber(object, QStringLiteral("minimumDiameterMm"), path, errors,
                  &set.minimumDiameterMm);
    RequireNumber(object, QStringLiteral("maximumDiameterMm"), path, errors,
                  &set.maximumDiameterMm);
    RequireNumber(object, QStringLiteral("plungeFeedMmPerMin"), path, errors,
                  &set.plungeFeedMmPerMin);
    RequireNumber(object, QStringLiteral("cuttingFeedMmPerMin"), path, errors,
                  &set.cuttingFeedMmPerMin);
    RequireNumber(object, QStringLiteral("retractFeedMmPerMin"), path, errors,
                  &set.retractFeedMmPerMin);
    RequireNumber(object, QStringLiteral("spindleRpm"), path, errors,
                  &set.spindleRpm);
    RequireNumber(object, QStringLiteral("stepDownMm"), path, errors,
                  &set.stepDownMm);
    RequireNumber(object, QStringLiteral("stepOverPercent"), path, errors,
                  &set.stepOverPercent);
    RequireNumber(object, QStringLiteral("finishAllowanceMm"), path, errors,
                  &set.finishAllowanceMm);
    RequireString(object, QStringLiteral("coolant"), path, errors, &set.coolant);

    RequirePositive(set.minimumDiameterMm, path + QStringLiteral(".minimumDiameterMm"),
                    errors);
    if (set.maximumDiameterMm < set.minimumDiameterMm) {
        AddError(errors, path + QStringLiteral(".maximumDiameterMm"),
                 QStringLiteral("must be at least minimumDiameterMm"));
    }
    RequirePositive(set.plungeFeedMmPerMin,
                    path + QStringLiteral(".plungeFeedMmPerMin"), errors);
    RequirePositive(set.cuttingFeedMmPerMin,
                    path + QStringLiteral(".cuttingFeedMmPerMin"), errors);
    RequirePositive(set.retractFeedMmPerMin,
                    path + QStringLiteral(".retractFeedMmPerMin"), errors);
    RequirePositive(set.spindleRpm, path + QStringLiteral(".spindleRpm"), errors);
    RequirePositive(set.stepDownMm, path + QStringLiteral(".stepDownMm"), errors);
    RequirePercent(set.stepOverPercent,
                   path + QStringLiteral(".stepOverPercent"), errors);
    RequireNonNegative(set.finishAllowanceMm,
                       path + QStringLiteral(".finishAllowanceMm"), errors);
    ValidateCoolant(set.coolant, path + QStringLiteral(".coolant"), errors);
    return set;
}

FeatureMatchingRule ParseFeatureMatchingRule(const QJsonObject &object,
                                             const QString &path,
                                             QStringList *errors)
{
    RejectUnknownFields(
        object,
        {QStringLiteral("id"), QStringLiteral("featureKind"),
         QStringLiteral("category"), QStringLiteral("minimumSizeMm"),
         QStringLiteral("maximumSizeMm"), QStringLiteral("diameterBands"),
         QStringLiteral("closed"), QStringLiteral("through")},
        path, errors);
    FeatureMatchingRule rule;
    RequireString(object, QStringLiteral("id"), path, errors, &rule.id);
    RequireString(object, QStringLiteral("featureKind"), path, errors,
                  &rule.featureKind);
    RequireString(object, QStringLiteral("category"), path, errors,
                  &rule.category);
    RequireNumber(object, QStringLiteral("minimumSizeMm"), path, errors,
                  &rule.minimumSizeMm);
    RequireNumber(object, QStringLiteral("maximumSizeMm"), path, errors,
                  &rule.maximumSizeMm);
    RequireBool(object, QStringLiteral("closed"), path, errors, &rule.closed);
    RequireBool(object, QStringLiteral("through"), path, errors, &rule.through);
    QJsonArray bands;
    RequireArray(object, QStringLiteral("diameterBands"), path, errors, &bands);
    for (int index = 0; index < bands.size(); ++index) {
        const QString band_path =
            path + QStringLiteral(".diameterBands[%1]").arg(index);
        if (!bands.at(index).isObject()) {
            AddError(errors, band_path, QStringLiteral("must be an object"));
            continue;
        }
        const QJsonObject band_object = bands.at(index).toObject();
        RejectUnknownFields(
            band_object,
            {QStringLiteral("minimumDiameterMm"),
             QStringLiteral("maximumDiameterMm")},
            band_path, errors);
        DiameterBand band;
        RequireNumber(band_object, QStringLiteral("minimumDiameterMm"),
                      band_path, errors, &band.minimumDiameterMm);
        RequireNumber(band_object, QStringLiteral("maximumDiameterMm"),
                      band_path, errors, &band.maximumDiameterMm);
        RequirePositive(band.minimumDiameterMm,
                        band_path + QStringLiteral(".minimumDiameterMm"), errors);
        if (band.maximumDiameterMm < band.minimumDiameterMm) {
            AddError(errors, band_path + QStringLiteral(".maximumDiameterMm"),
                     QStringLiteral("must be at least minimumDiameterMm"));
        }
        rule.diameterBands.append(band);
    }
    if (rule.featureKind != QStringLiteral("hole") &&
        rule.featureKind != QStringLiteral("frame")) {
        AddError(errors, path + QStringLiteral(".featureKind"),
                 QStringLiteral("must be 'hole' or 'frame'"));
    }
    RequirePositive(rule.minimumSizeMm,
                    path + QStringLiteral(".minimumSizeMm"), errors);
    if (rule.maximumSizeMm < rule.minimumSizeMm) {
        AddError(errors, path + QStringLiteral(".maximumSizeMm"),
                 QStringLiteral("must be at least minimumSizeMm"));
    }
    QList<DiameterBand> sorted_bands = rule.diameterBands;
    std::sort(sorted_bands.begin(), sorted_bands.end(),
              [](const DiameterBand &left, const DiameterBand &right) {
                  return left.minimumDiameterMm < right.minimumDiameterMm;
              });
    for (int index = 1; index < sorted_bands.size(); ++index) {
        if (sorted_bands.at(index).minimumDiameterMm <=
            sorted_bands.at(index - 1).maximumDiameterMm) {
            AddError(errors, path + QStringLiteral(".diameterBands"),
                     QStringLiteral("diameter bands overlap"));
        }
    }
    return rule;
}

AutomationMachiningPlanStep ParseMachiningPlanStep(
    const QJsonObject &object, const QString &path, QStringList *errors)
{
    RejectUnknownFields(
        object,
        {QStringLiteral("id"), QStringLiteral("strategy"),
         QStringLiteral("toolSelector"), QStringLiteral("referenceTool"),
         QStringLiteral("startExpression"), QStringLiteral("depthExpression"),
         QStringLiteral("coolant"), QStringLiteral("enabled"),
         QStringLiteral("unsupportedReason"),
         QStringLiteral("cornerInsertionEnabled"),
         QStringLiteral("dynamicMillingEnabled"),
         QStringLiteral("threeDMachiningEnabled")},
        path, errors);
    AutomationMachiningPlanStep step;
    RequireString(object, QStringLiteral("id"), path, errors, &step.id);
    RequireString(object, QStringLiteral("strategy"), path, errors,
                  &step.strategy);
    RequireString(object, QStringLiteral("toolSelector"), path, errors,
                  &step.toolSelector);
    RequireString(object, QStringLiteral("referenceTool"), path, errors,
                  &step.referenceTool, true);
    RequireString(object, QStringLiteral("startExpression"), path, errors,
                  &step.startExpression);
    RequireString(object, QStringLiteral("depthExpression"), path, errors,
                  &step.depthExpression);
    RequireString(object, QStringLiteral("coolant"), path, errors, &step.coolant);
    RequireBool(object, QStringLiteral("enabled"), path, errors, &step.enabled);
    RequireString(object, QStringLiteral("unsupportedReason"), path, errors,
                  &step.unsupportedReason, true);
    RequireBool(object, QStringLiteral("cornerInsertionEnabled"), path, errors,
                &step.cornerInsertionEnabled);
    RequireBool(object, QStringLiteral("dynamicMillingEnabled"), path, errors,
                &step.dynamicMillingEnabled);
    RequireBool(object, QStringLiteral("threeDMachiningEnabled"), path, errors,
                &step.threeDMachiningEnabled);
    if (!IsAllowedStrategy(step.strategy)) {
        AddError(errors, path + QStringLiteral(".strategy"),
                 QStringLiteral("is not in the verified strategy whitelist"));
    }
    ValidateExpression(step.startExpression,
                       path + QStringLiteral(".startExpression"), errors);
    ValidateExpression(step.depthExpression,
                       path + QStringLiteral(".depthExpression"), errors);
    ValidateCoolant(step.coolant, path + QStringLiteral(".coolant"), errors);
    if (!step.enabled && step.unsupportedReason.trimmed().isEmpty()) {
        AddError(errors, path + QStringLiteral(".unsupportedReason"),
                 QStringLiteral("is required when the step is disabled"));
    }
    if (step.cornerInsertionEnabled) {
        AddError(errors, path + QStringLiteral(".cornerInsertionEnabled"),
                 QStringLiteral("corner insertion is unsupported"));
    }
    if (step.dynamicMillingEnabled) {
        AddError(errors, path + QStringLiteral(".dynamicMillingEnabled"),
                 QStringLiteral("dynamic milling is unsupported"));
    }
    if (step.threeDMachiningEnabled) {
        AddError(errors, path + QStringLiteral(".threeDMachiningEnabled"),
                 QStringLiteral("3D machining is unsupported"));
    }
    return step;
}

DeepHoleStageParameters ParseDeepHoleStageParameters(
    const QJsonObject &object, const QString &path, QStringList *errors)
{
    const QSet<QString> fields{
        QStringLiteral("id"), QStringLiteral("normalSpindleRpm"),
        QStringLiteral("normalFeedMmPerMin"), QStringLiteral("coolant"),
        QStringLiteral("guideEnabled"), QStringLiteral("guideStartMm"),
        QStringLiteral("guideEndMm"), QStringLiteral("guideSpindleRpm"),
        QStringLiteral("guideFeedMmPerMin"),
        QStringLiteral("slowEntryEnabled"),
        QStringLiteral("slowEntryStartMm"), QStringLiteral("slowEntryEndMm"),
        QStringLiteral("slowEntryPercent"),
        QStringLiteral("breakthroughEnabled"),
        QStringLiteral("breakthroughStartMm"),
        QStringLiteral("breakthroughEndMm"),
        QStringLiteral("breakthroughPercent"), QStringLiteral("retractEnabled"),
        QStringLiteral("retractSpindleRpm"),
        QStringLiteral("retractFeedMmPerMin"),
        QStringLiteral("retractDirection"),
        QStringLiteral("transitionEnabled"),
        QStringLiteral("minimumRapidDistancePercent"),
        QStringLiteral("rapidFeedMmPerMin"), QStringLiteral("maximumPeckMm"),
        QStringLiteral("minimumPeckMm"), QStringLiteral("peckReductionMm"),
        QStringLiteral("variableSpeedEnabled")};
    RejectUnknownFields(object, fields, path, errors);
    DeepHoleStageParameters parameters;
#define READ_NUMBER(name) \
    RequireNumber(object, QStringLiteral(#name), path, errors, &parameters.name)
#define READ_BOOL(name) \
    RequireBool(object, QStringLiteral(#name), path, errors, &parameters.name)
    RequireString(object, QStringLiteral("id"), path, errors, &parameters.id);
    READ_NUMBER(normalSpindleRpm);
    READ_NUMBER(normalFeedMmPerMin);
    RequireString(object, QStringLiteral("coolant"), path, errors,
                  &parameters.coolant);
    READ_BOOL(guideEnabled);
    READ_NUMBER(guideStartMm);
    READ_NUMBER(guideEndMm);
    READ_NUMBER(guideSpindleRpm);
    READ_NUMBER(guideFeedMmPerMin);
    READ_BOOL(slowEntryEnabled);
    READ_NUMBER(slowEntryStartMm);
    READ_NUMBER(slowEntryEndMm);
    READ_NUMBER(slowEntryPercent);
    READ_BOOL(breakthroughEnabled);
    READ_NUMBER(breakthroughStartMm);
    READ_NUMBER(breakthroughEndMm);
    READ_NUMBER(breakthroughPercent);
    READ_BOOL(retractEnabled);
    READ_NUMBER(retractSpindleRpm);
    READ_NUMBER(retractFeedMmPerMin);
    RequireString(object, QStringLiteral("retractDirection"), path, errors,
                  &parameters.retractDirection);
    READ_BOOL(transitionEnabled);
    READ_NUMBER(minimumRapidDistancePercent);
    READ_NUMBER(rapidFeedMmPerMin);
    READ_NUMBER(maximumPeckMm);
    READ_NUMBER(minimumPeckMm);
    READ_NUMBER(peckReductionMm);
    READ_BOOL(variableSpeedEnabled);
#undef READ_BOOL
#undef READ_NUMBER

    RequirePositive(parameters.normalSpindleRpm,
                    path + QStringLiteral(".normalSpindleRpm"), errors);
    RequirePositive(parameters.normalFeedMmPerMin,
                    path + QStringLiteral(".normalFeedMmPerMin"), errors);
    ValidateCoolant(parameters.coolant, path + QStringLiteral(".coolant"), errors);
    RequireNonNegative(parameters.guideStartMm,
                       path + QStringLiteral(".guideStartMm"), errors);
    if (parameters.guideEnabled) {
        if (parameters.guideEndMm <= parameters.guideStartMm) {
            AddError(errors, path + QStringLiteral(".guideEndMm"),
                     QStringLiteral("must be greater than guideStartMm"));
        }
        RequirePositive(parameters.guideSpindleRpm,
                        path + QStringLiteral(".guideSpindleRpm"), errors);
        RequirePositive(parameters.guideFeedMmPerMin,
                        path + QStringLiteral(".guideFeedMmPerMin"), errors);
    }
    RequireNonNegative(parameters.slowEntryStartMm,
                       path + QStringLiteral(".slowEntryStartMm"), errors);
    if (parameters.slowEntryEnabled &&
        parameters.slowEntryEndMm <= parameters.slowEntryStartMm) {
        AddError(errors, path + QStringLiteral(".slowEntryEndMm"),
                 QStringLiteral("must be greater than slowEntryStartMm"));
    }
    RequirePercent(parameters.slowEntryPercent,
                   path + QStringLiteral(".slowEntryPercent"), errors);
    RequireNonNegative(parameters.breakthroughStartMm,
                       path + QStringLiteral(".breakthroughStartMm"), errors);
    if (parameters.breakthroughEnabled &&
        parameters.breakthroughEndMm <= parameters.breakthroughStartMm) {
        AddError(errors, path + QStringLiteral(".breakthroughEndMm"),
                 QStringLiteral("must be greater than breakthroughStartMm"));
    }
    RequirePercent(parameters.breakthroughPercent,
                   path + QStringLiteral(".breakthroughPercent"), errors);
    if (parameters.retractEnabled) {
        RequirePositive(parameters.retractSpindleRpm,
                        path + QStringLiteral(".retractSpindleRpm"), errors);
        RequirePositive(parameters.retractFeedMmPerMin,
                        path + QStringLiteral(".retractFeedMmPerMin"), errors);
    }
    if (parameters.retractDirection != QStringLiteral("forward") &&
        parameters.retractDirection != QStringLiteral("reverse")) {
        AddError(errors, path + QStringLiteral(".retractDirection"),
                 QStringLiteral("must be 'forward' or 'reverse'"));
    }
    RequireNonNegative(parameters.minimumRapidDistancePercent,
                       path + QStringLiteral(".minimumRapidDistancePercent"),
                       errors);
    if (parameters.transitionEnabled) {
        RequirePositive(parameters.rapidFeedMmPerMin,
                        path + QStringLiteral(".rapidFeedMmPerMin"), errors);
    }
    RequirePositive(parameters.maximumPeckMm,
                    path + QStringLiteral(".maximumPeckMm"), errors);
    RequirePositive(parameters.minimumPeckMm,
                    path + QStringLiteral(".minimumPeckMm"), errors);
    if (parameters.maximumPeckMm < parameters.minimumPeckMm) {
        AddError(errors, path + QStringLiteral(".maximumPeckMm"),
                 QStringLiteral("must be at least minimumPeckMm"));
    }
    RequirePositive(parameters.peckReductionMm,
                    path + QStringLiteral(".peckReductionMm"), errors);
    if (parameters.variableSpeedEnabled) {
        AddError(errors, path + QStringLiteral(".variableSpeedEnabled"),
                 QStringLiteral("variable speed staging is unsupported"));
    }
    return parameters;
}

ThreadSpecification ParseThreadSpecification(const QJsonObject &object,
                                             const QString &path,
                                             QStringList *errors)
{
    RejectUnknownFields(
        object,
        {QStringLiteral("id"), QStringLiteral("system"),
         QStringLiteral("standard"), QStringLiteral("designation"),
         QStringLiteral("pilotHoleDiameterMm"), QStringLiteral("pitchMm"),
         QStringLiteral("threadDiameterMm"),
         QStringLiteral("candidateToolSelector"),
         QStringLiteral("xyLayeringEnabled"),
         QStringLiteral("roughPassCount"), QStringLiteral("finishPassCount"),
         QStringLiteral("roughAllowanceMm"),
         QStringLiteral("finishAllowanceMm")},
        path, errors);
    ThreadSpecification specification;
    RequireString(object, QStringLiteral("id"), path, errors, &specification.id);
    RequireString(object, QStringLiteral("system"), path, errors,
                  &specification.system);
    RequireString(object, QStringLiteral("standard"), path, errors,
                  &specification.standard);
    RequireString(object, QStringLiteral("designation"), path, errors,
                  &specification.designation);
    RequireNumber(object, QStringLiteral("pilotHoleDiameterMm"), path, errors,
                  &specification.pilotHoleDiameterMm);
    RequireNumber(object, QStringLiteral("pitchMm"), path, errors,
                  &specification.pitchMm);
    RequireNumber(object, QStringLiteral("threadDiameterMm"), path, errors,
                  &specification.threadDiameterMm);
    RequireString(object, QStringLiteral("candidateToolSelector"), path, errors,
                  &specification.candidateToolSelector);
    RequireBool(object, QStringLiteral("xyLayeringEnabled"), path, errors,
                &specification.xyLayeringEnabled);
    RequireInteger(object, QStringLiteral("roughPassCount"), path, errors,
                   &specification.roughPassCount);
    RequireInteger(object, QStringLiteral("finishPassCount"), path, errors,
                   &specification.finishPassCount);
    RequireNumber(object, QStringLiteral("roughAllowanceMm"), path, errors,
                  &specification.roughAllowanceMm);
    RequireNumber(object, QStringLiteral("finishAllowanceMm"), path, errors,
                  &specification.finishAllowanceMm);
    const QSet<QString> systems{
        QStringLiteral("metric"), QStringLiteral("imperial"),
        QStringLiteral("unified")};
    if (!systems.contains(specification.system)) {
        AddError(errors, path + QStringLiteral(".system"),
                 QStringLiteral("must be metric, imperial or unified"));
    }
    RequirePositive(specification.pilotHoleDiameterMm,
                    path + QStringLiteral(".pilotHoleDiameterMm"), errors);
    RequirePositive(specification.pitchMm, path + QStringLiteral(".pitchMm"),
                    errors);
    RequirePositive(specification.threadDiameterMm,
                    path + QStringLiteral(".threadDiameterMm"), errors);
    if (specification.pilotHoleDiameterMm >= specification.threadDiameterMm) {
        AddError(errors, path + QStringLiteral(".pilotHoleDiameterMm"),
                 QStringLiteral("must be smaller than threadDiameterMm"));
    }
    RequireNonNegative(specification.roughAllowanceMm,
                       path + QStringLiteral(".roughAllowanceMm"), errors);
    RequireNonNegative(specification.finishAllowanceMm,
                       path + QStringLiteral(".finishAllowanceMm"), errors);
    if (specification.xyLayeringEnabled) {
        AddError(errors, path + QStringLiteral(".xyLayeringEnabled"),
                 QStringLiteral("XY layering is unsupported"));
    }
    return specification;
}

template <typename Item, typename Parser>
QList<Item> ParseObjectArray(const QJsonObject &root,
                             const QString &key,
                             QStringList *errors,
                             Parser parser)
{
    QList<Item> items;
    QJsonArray array;
    if (!RequireArray(root, key, QStringLiteral("document"), errors, &array)) {
        return items;
    }
    QSet<QString> ids;
    for (int index = 0; index < array.size(); ++index) {
        const QString path = QStringLiteral("%1[%2]").arg(key).arg(index);
        if (!array.at(index).isObject()) {
            AddError(errors, path, QStringLiteral("must be an object"));
            continue;
        }
        const Item item = parser(array.at(index).toObject(), path, errors);
        if (!item.id.isEmpty() && ids.contains(item.id)) {
            AddError(errors, path + QStringLiteral(".id"),
                     QStringLiteral("duplicates id '%1'").arg(item.id));
        }
        ids.insert(item.id);
        items.append(item);
    }
    return items;
}

QJsonObject SourceToJson(const AutomationTemplateSourceMetadata &source)
{
    return {{QStringLiteral("type"), source.type},
            {QStringLiteral("name"), source.name},
            {QStringLiteral("revision"), source.revision},
            {QStringLiteral("checksumSha256"), source.checksumSha256}};
}

QJsonObject ToolSetToJson(const ToolCuttingParameterSet &set)
{
    return {{QStringLiteral("id"), set.id},
            {QStringLiteral("material"), set.material},
            {QStringLiteral("toolType"), set.toolType},
            {QStringLiteral("toolName"), set.toolName},
            {QStringLiteral("minimumDiameterMm"), set.minimumDiameterMm},
            {QStringLiteral("maximumDiameterMm"), set.maximumDiameterMm},
            {QStringLiteral("plungeFeedMmPerMin"), set.plungeFeedMmPerMin},
            {QStringLiteral("cuttingFeedMmPerMin"), set.cuttingFeedMmPerMin},
            {QStringLiteral("retractFeedMmPerMin"), set.retractFeedMmPerMin},
            {QStringLiteral("spindleRpm"), set.spindleRpm},
            {QStringLiteral("stepDownMm"), set.stepDownMm},
            {QStringLiteral("stepOverPercent"), set.stepOverPercent},
            {QStringLiteral("finishAllowanceMm"), set.finishAllowanceMm},
            {QStringLiteral("coolant"), set.coolant}};
}

QJsonObject FeatureRuleToJson(const FeatureMatchingRule &rule)
{
    QJsonArray bands;
    for (const DiameterBand &band : rule.diameterBands) {
        bands.append(QJsonObject{
            {QStringLiteral("minimumDiameterMm"), band.minimumDiameterMm},
            {QStringLiteral("maximumDiameterMm"), band.maximumDiameterMm}});
    }
    return {{QStringLiteral("id"), rule.id},
            {QStringLiteral("featureKind"), rule.featureKind},
            {QStringLiteral("category"), rule.category},
            {QStringLiteral("minimumSizeMm"), rule.minimumSizeMm},
            {QStringLiteral("maximumSizeMm"), rule.maximumSizeMm},
            {QStringLiteral("diameterBands"), bands},
            {QStringLiteral("closed"), rule.closed},
            {QStringLiteral("through"), rule.through}};
}

QJsonObject PlanStepToJson(const AutomationMachiningPlanStep &step)
{
    return {{QStringLiteral("id"), step.id},
            {QStringLiteral("strategy"), step.strategy},
            {QStringLiteral("toolSelector"), step.toolSelector},
            {QStringLiteral("referenceTool"), step.referenceTool},
            {QStringLiteral("startExpression"), step.startExpression},
            {QStringLiteral("depthExpression"), step.depthExpression},
            {QStringLiteral("coolant"), step.coolant},
            {QStringLiteral("enabled"), step.enabled},
            {QStringLiteral("unsupportedReason"), step.unsupportedReason},
            {QStringLiteral("cornerInsertionEnabled"),
             step.cornerInsertionEnabled},
            {QStringLiteral("dynamicMillingEnabled"),
             step.dynamicMillingEnabled},
            {QStringLiteral("threeDMachiningEnabled"),
             step.threeDMachiningEnabled}};
}

QJsonObject DeepHoleToJson(const DeepHoleStageParameters &parameters)
{
    return {{QStringLiteral("id"), parameters.id},
            {QStringLiteral("normalSpindleRpm"), parameters.normalSpindleRpm},
            {QStringLiteral("normalFeedMmPerMin"), parameters.normalFeedMmPerMin},
            {QStringLiteral("coolant"), parameters.coolant},
            {QStringLiteral("guideEnabled"), parameters.guideEnabled},
            {QStringLiteral("guideStartMm"), parameters.guideStartMm},
            {QStringLiteral("guideEndMm"), parameters.guideEndMm},
            {QStringLiteral("guideSpindleRpm"), parameters.guideSpindleRpm},
            {QStringLiteral("guideFeedMmPerMin"), parameters.guideFeedMmPerMin},
            {QStringLiteral("slowEntryEnabled"), parameters.slowEntryEnabled},
            {QStringLiteral("slowEntryStartMm"), parameters.slowEntryStartMm},
            {QStringLiteral("slowEntryEndMm"), parameters.slowEntryEndMm},
            {QStringLiteral("slowEntryPercent"), parameters.slowEntryPercent},
            {QStringLiteral("breakthroughEnabled"),
             parameters.breakthroughEnabled},
            {QStringLiteral("breakthroughStartMm"),
             parameters.breakthroughStartMm},
            {QStringLiteral("breakthroughEndMm"), parameters.breakthroughEndMm},
            {QStringLiteral("breakthroughPercent"),
             parameters.breakthroughPercent},
            {QStringLiteral("retractEnabled"), parameters.retractEnabled},
            {QStringLiteral("retractSpindleRpm"), parameters.retractSpindleRpm},
            {QStringLiteral("retractFeedMmPerMin"),
             parameters.retractFeedMmPerMin},
            {QStringLiteral("retractDirection"), parameters.retractDirection},
            {QStringLiteral("transitionEnabled"), parameters.transitionEnabled},
            {QStringLiteral("minimumRapidDistancePercent"),
             parameters.minimumRapidDistancePercent},
            {QStringLiteral("rapidFeedMmPerMin"), parameters.rapidFeedMmPerMin},
            {QStringLiteral("maximumPeckMm"), parameters.maximumPeckMm},
            {QStringLiteral("minimumPeckMm"), parameters.minimumPeckMm},
            {QStringLiteral("peckReductionMm"), parameters.peckReductionMm},
            {QStringLiteral("variableSpeedEnabled"),
             parameters.variableSpeedEnabled}};
}

QJsonObject ThreadToJson(const ThreadSpecification &specification)
{
    return {{QStringLiteral("id"), specification.id},
            {QStringLiteral("system"), specification.system},
            {QStringLiteral("standard"), specification.standard},
            {QStringLiteral("designation"), specification.designation},
            {QStringLiteral("pilotHoleDiameterMm"),
             specification.pilotHoleDiameterMm},
            {QStringLiteral("pitchMm"), specification.pitchMm},
            {QStringLiteral("threadDiameterMm"), specification.threadDiameterMm},
            {QStringLiteral("candidateToolSelector"),
             specification.candidateToolSelector},
            {QStringLiteral("xyLayeringEnabled"),
             specification.xyLayeringEnabled},
            {QStringLiteral("roughPassCount"), specification.roughPassCount},
            {QStringLiteral("finishPassCount"), specification.finishPassCount},
            {QStringLiteral("roughAllowanceMm"), specification.roughAllowanceMm},
            {QStringLiteral("finishAllowanceMm"),
             specification.finishAllowanceMm}};
}

template <typename Item, typename Converter>
QJsonArray ToJsonArray(const QList<Item> &items, Converter converter)
{
    QJsonArray array;
    for (const Item &item : items) {
        array.append(converter(item));
    }
    return array;
}

} // namespace

AutomationTemplateParseResult AutomationTemplateDocument::FromJson(
    const QByteArray &json)
{
    AutomationTemplateParseResult result;
    QJsonParseError parse_error;
    const QJsonDocument json_document = QJsonDocument::fromJson(json, &parse_error);
    if (parse_error.error != QJsonParseError::NoError ||
        !json_document.isObject()) {
        result.errors.append(
            QStringLiteral("document: invalid JSON object: %1")
                .arg(parse_error.errorString()));
        return result;
    }
    const QJsonObject root = json_document.object();
    RejectUnknownFields(
        root,
        {QStringLiteral("schemaVersion"), QStringLiteral("documentId"),
         QStringLiteral("documentVersion"), QStringLiteral("source"),
         QStringLiteral("units"),
         QStringLiteral("toolCuttingParameterSets"),
         QStringLiteral("featureMatchingRules"),
         QStringLiteral("machiningPlanSteps"),
         QStringLiteral("deepHoleStageParameters"),
         QStringLiteral("threadSpecifications")},
        QStringLiteral("document"), &result.errors);

    double schema_version = 0.0;
    if (RequireNumber(root, QStringLiteral("schemaVersion"),
                      QStringLiteral("document"), &result.errors,
                      &schema_version)) {
        if (schema_version != kSupportedSchemaVersion) {
            AddError(&result.errors, QStringLiteral("document.schemaVersion"),
                     QStringLiteral("unknown version; migration is not available"));
        } else {
            result.document.schemaVersion = static_cast<int>(schema_version);
        }
    }
    RequireString(root, QStringLiteral("documentId"), QStringLiteral("document"),
                  &result.errors, &result.document.documentId);
    RequireString(root, QStringLiteral("documentVersion"),
                  QStringLiteral("document"), &result.errors,
                  &result.document.documentVersion);
    if (!root.value(QStringLiteral("source")).isObject()) {
        AddError(&result.errors, QStringLiteral("document.source"),
                 QStringLiteral("must be an object"));
    } else {
        ParseSource(root.value(QStringLiteral("source")).toObject(),
                    &result.document.source, &result.errors);
    }
    if (!root.value(QStringLiteral("units")).isObject()) {
        AddError(&result.errors, QStringLiteral("document.units"),
                 QStringLiteral("must be an object"));
    } else {
        ParseUnits(root.value(QStringLiteral("units")).toObject(),
                   &result.errors);
    }
    result.document.toolCuttingParameterSets =
        ParseObjectArray<ToolCuttingParameterSet>(
            root, QStringLiteral("toolCuttingParameterSets"), &result.errors,
            ParseToolCuttingParameterSet);
    result.document.featureMatchingRules =
        ParseObjectArray<FeatureMatchingRule>(
            root, QStringLiteral("featureMatchingRules"), &result.errors,
            ParseFeatureMatchingRule);
    result.document.machiningPlanSteps =
        ParseObjectArray<AutomationMachiningPlanStep>(
            root, QStringLiteral("machiningPlanSteps"), &result.errors,
            ParseMachiningPlanStep);
    result.document.deepHoleStageParameters =
        ParseObjectArray<DeepHoleStageParameters>(
            root, QStringLiteral("deepHoleStageParameters"), &result.errors,
            ParseDeepHoleStageParameters);
    result.document.threadSpecifications =
        ParseObjectArray<ThreadSpecification>(
            root, QStringLiteral("threadSpecifications"), &result.errors,
            ParseThreadSpecification);

    QSet<QString> thread_keys;
    for (const ThreadSpecification &specification :
         result.document.threadSpecifications) {
        const QString key = specification.system + QLatin1Char('\n') +
                            specification.standard + QLatin1Char('\n') +
                            specification.designation;
        if (thread_keys.contains(key)) {
            AddError(&result.errors,
                     QStringLiteral("threadSpecifications"),
                     QStringLiteral("duplicate thread specification key for '%1'")
                         .arg(specification.designation));
        }
        thread_keys.insert(key);
    }

    for (int left = 0;
         left < result.document.toolCuttingParameterSets.size(); ++left) {
        const ToolCuttingParameterSet &first =
            result.document.toolCuttingParameterSets.at(left);
        for (int right = left + 1;
             right < result.document.toolCuttingParameterSets.size(); ++right) {
            const ToolCuttingParameterSet &second =
                result.document.toolCuttingParameterSets.at(right);
            const bool same_key = first.material == second.material &&
                                  first.toolType == second.toolType &&
                                  first.toolName == second.toolName;
            const bool overlaps =
                first.minimumDiameterMm <= second.maximumDiameterMm &&
                second.minimumDiameterMm <= first.maximumDiameterMm;
            if (same_key && overlaps) {
                AddError(&result.errors,
                         QStringLiteral("toolCuttingParameterSets"),
                         QStringLiteral("diameter tiers '%1' and '%2' overlap")
                             .arg(first.id, second.id));
            }
        }
    }
    result.ok = result.errors.isEmpty();
    return result;
}

bool AutomationTemplateDocument::ToJson(QByteArray *json,
                                        QStringList *errors) const
{
    if (json == nullptr) {
        if (errors != nullptr) {
            errors->append(QStringLiteral("output JSON pointer must not be null"));
        }
        return false;
    }
    const QJsonObject root{
        {QStringLiteral("schemaVersion"), schemaVersion},
        {QStringLiteral("documentId"), documentId},
        {QStringLiteral("documentVersion"), documentVersion},
        {QStringLiteral("source"), SourceToJson(source)},
        {QStringLiteral("units"),
         QJsonObject{{QStringLiteral("length"), QStringLiteral("mm")},
                     {QStringLiteral("feed"), QStringLiteral("mm/min")},
                     {QStringLiteral("spindle"), QStringLiteral("rpm")},
                     {QStringLiteral("percentage"),
                      QStringLiteral("percent")}}},
        {QStringLiteral("toolCuttingParameterSets"),
         ToJsonArray(toolCuttingParameterSets, ToolSetToJson)},
        {QStringLiteral("featureMatchingRules"),
         ToJsonArray(featureMatchingRules, FeatureRuleToJson)},
        {QStringLiteral("machiningPlanSteps"),
         ToJsonArray(machiningPlanSteps, PlanStepToJson)},
        {QStringLiteral("deepHoleStageParameters"),
         ToJsonArray(deepHoleStageParameters, DeepHoleToJson)},
        {QStringLiteral("threadSpecifications"),
         ToJsonArray(threadSpecifications, ThreadToJson)}};
    const QByteArray candidate =
        QJsonDocument(root).toJson(QJsonDocument::Indented);
    const AutomationTemplateParseResult validation = FromJson(candidate);
    if (!validation.ok) {
        if (errors != nullptr) {
            errors->append(validation.errors);
        }
        return false;
    }
    *json = candidate;
    return true;
}
