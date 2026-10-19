#include "../../process_design/operations/MachiningInput.h"
#include "ProgramGenerationService.h"
#include "../../../core/MachiningCoordinates.h"
#include "../../../core/SketchSelectionBinding.h"
#include "RegionToolpathValidation.h"
#include "ChamferMotionValidation.h"
#include "../../../strategies/hole/ChamferStrategy.h"
#include "../../process_design/selection/PocketOpening.h"
#include "../../process_design/selection/SlopedFloorOperation.h"
#include "../../../strategies/mill/ContourSideGeometry.h"

#include "../../../gcode/GCodeModalOptimizer.h"
#include "../safety/GCodeSafetyValidator.h"
#include "../../../gcode/Cq8MacroProgramBuilder.h"
#include "../fingerprint/ProgramSnapshotFingerprint.h"
#include "../../../strategies/mill/PlanarMachiningValidation.h"
#include "../../../gcode/SiemensProgramPackage.h"
#include "../../../strategies/StrategyBase.h"
#include "../../../strategies/hole/HoleStrategyUtils.h"
#include "../../../strategies/mill/SlotMachiningGeometry.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// 函数说明：isSlotMillingStrategy负责当前模块的核心处理。
bool isSlotMillingStrategy(const QString &strategyId)
{
    return strategyId == QStringLiteral("mill_slot") ||
           strategyId == QStringLiteral("mill_blind_slot") ||
           strategyId == QStringLiteral("mill_tapered_slot");
}

// 函数说明：isPocketMillingStrategy负责当前模块的核心处理。
bool isPocketMillingStrategy(const QString &strategyId)
{
    return strategyId == QStringLiteral("mill_pocket_rough") ||
           strategyId == QStringLiteral("mill_pocket_finish") ||
           strategyId == QStringLiteral("mill_pocket_floor_finish");
}


// 函数说明：expectedHoleToolType负责当前模块的核心处理。
QString expectedHoleToolType(const QString &strategyId)
{
    if (strategyId == QStringLiteral("hole_spot")) return QStringLiteral("spot_drill");
    if (strategyId == QStringLiteral("hole_tapping")) return QStringLiteral("tap");
    if (strategyId == QStringLiteral("hole_reaming")) return QStringLiteral("reamer");
    if (strategyId == QStringLiteral("hole_chamfer")) return QStringLiteral("chamfer_mill");
    if (strategyId == QStringLiteral("hole_g76") || strategyId == QStringLiteral("hole_g86")) return QStringLiteral("boring_bar");
    if (strategyId == QStringLiteral("hole_g81") ||
        strategyId == QStringLiteral("hole_peck") ||
        strategyId == QStringLiteral("hole_deephole") ||
        strategyId == QStringLiteral("hole_g73")) {
        return QStringLiteral("drill");
    }
    if (strategyId == QStringLiteral("hole_circular_mill")) {
        return QStringLiteral("end_mill");
    }
    return QString();
}

QString nonfiniteParameterError(const StrategyParams &params)
{
    for (auto it = params.values.cbegin(); it != params.values.cend(); ++it) {
        bool converted = false;
        const double value = it.value().toDouble(&converted);
        if (!converted || !std::isfinite(value)) {
            return QStringLiteral("parameter '%1' must be a finite number.").arg(it.key());
        }
    }
    return QString();
}

QString nonfiniteToolError(const ToolEntry &tool)
{
    const struct {
        const char *name;
        double value;
    } dimensions[] = {
        {"diameter", tool.diameter},
        {"flute length", tool.fluteLen},
        {"total length", tool.totalLen},
        {"shank diameter", tool.shankDia},
        {"point angle", tool.pointAngle},
        {"pitch", tool.pitch},
    };
    for (const auto &dimension : dimensions) {
        if (!std::isfinite(dimension.value)) {
            return QStringLiteral("tool T%1 %2 must be finite.")
                .arg(tool.id)
                .arg(QString::fromLatin1(dimension.name));
        }
    }
    return QString();
}

// 函数说明：holeToolError负责当前模块的核心处理。
QString holeToolError(const MachiningOperation &operation, const ToolEntry &tool)
{
    const QString expectedToolType = expectedHoleToolType(operation.strategyId);
    if (!expectedToolType.isEmpty() && tool.type != expectedToolType) {
        return QStringLiteral("tool T%1 is type '%2', but strategy '%3' requires '%4'.")
            .arg(operation.toolId)
            .arg(tool.type)
            .arg(operation.strategyId)
            .arg(expectedToolType);
    }

    const double cuttingDepth = holeCuttingDepth(operation.strategyId, operation.holeFeature, operation.params, tool);
    const bool isBoringCycle = operation.strategyId == QStringLiteral("hole_g76") ||
                               operation.strategyId == QStringLiteral("hole_g86");
    if (isBoringCycle &&
        (!std::isfinite(tool.fluteLen) || !std::isfinite(tool.totalLen) ||
         tool.fluteLen <= 0.0 || tool.totalLen <= 0.0)) {
        return QStringLiteral("boring bar T%1 must define positive flute and total lengths for reach verification.")
            .arg(operation.toolId);
    }
    if (tool.fluteLen > 0.0 &&
        cuttingDepth > tool.fluteLen + 0.01) {
        return QStringLiteral("cutting depth %1 mm exceeds tool flute length %2 mm.")
            .arg(cuttingDepth, 0, 'f', 3)
            .arg(tool.fluteLen, 0, 'f', 3);
    }
    if (tool.totalLen > 0.0 &&
        cuttingDepth >= tool.totalLen - 0.01) {
        return QStringLiteral("cutting depth %1 mm reaches tool total length %2 mm; safe stick-out is impossible.")
            .arg(cuttingDepth, 0, 'f', 3)
            .arg(tool.totalLen, 0, 'f', 3);
    }

    if (operation.strategyId == QStringLiteral("hole_g81") ||
        operation.strategyId == QStringLiteral("hole_peck") ||
        operation.strategyId == QStringLiteral("hole_deephole") ||
        operation.strategyId == QStringLiteral("hole_g73")) {
        const double targetDiameter = operation.holeFeature.radius * 2.0;
        const double tolerance = std::max(0.05, targetDiameter * 0.01);
        if (targetDiameter > 0.0 && tool.diameter > targetDiameter + tolerance) {
            return QStringLiteral("drill diameter %1 mm exceeds target hole diameter %2 mm.")
                .arg(tool.diameter, 0, 'f', 3)
                .arg(targetDiameter, 0, 'f', 3);
        }
    }
    if (operation.strategyId == QStringLiteral("hole_reaming")) {
        const double targetDiameter = operation.holeFeature.radius * 2.0;
        const double tolerance = std::max(0.05, targetDiameter * 0.01);
        if (targetDiameter > 0.0 && std::abs(tool.diameter - targetDiameter) > tolerance) {
            return QStringLiteral("reamer diameter %1 mm does not match target hole diameter %2 mm.")
                .arg(tool.diameter, 0, 'f', 3)
                .arg(targetDiameter, 0, 'f', 3);
        }
    }
    if (operation.strategyId == QStringLiteral("hole_tapping") &&
        operation.holeFeature.pitch > 0.0) {
        const double tolerance = std::max(0.01, operation.holeFeature.pitch * 0.02);
        if (tool.pitch <= 0.0 ||
            std::abs(tool.pitch - operation.holeFeature.pitch) > tolerance) {
            return QStringLiteral("tap pitch %1 mm does not match target thread pitch %2 mm.")
                .arg(tool.pitch, 0, 'f', 3)
                .arg(operation.holeFeature.pitch, 0, 'f', 3);
        }
    }
    return QString();
}

// 函数说明：slotToolError负责当前模块的核心处理。
QString slotToolError(const MachiningOperation &operation, const ToolEntry &tool)
{
    if (tool.type != QStringLiteral("end_mill")) {
        return QStringLiteral("tool T%1 is type '%2', but slot machining requires 'end_mill'.")
            .arg(operation.toolId)
            .arg(tool.type);
    }
    if (operation.contourFeature.width > 0.0 &&
        tool.diameter >= operation.contourFeature.width - 0.01) {
        return QStringLiteral("tool diameter %1 mm must be smaller than slot width %2 mm.")
            .arg(tool.diameter, 0, 'f', 3)
            .arg(operation.contourFeature.width, 0, 'f', 3);
    }
    if (tool.fluteLen > 0.0 &&
        operation.contourFeature.depth > tool.fluteLen + 0.01) {
        return QStringLiteral("slot depth %1 mm exceeds tool flute length %2 mm.")
            .arg(operation.contourFeature.depth, 0, 'f', 3)
            .arg(tool.fluteLen, 0, 'f', 3);
    }
    if (tool.totalLen > 0.0 &&
        operation.contourFeature.depth >= tool.totalLen - 0.01) {
        return QStringLiteral("slot depth %1 mm reaches tool total length %2 mm; safe stick-out is impossible.")
            .arg(operation.contourFeature.depth, 0, 'f', 3)
            .arg(tool.totalLen, 0, 'f', 3);
    }
    return QString();
}

// 函数说明：pocketToolError负责当前模块的核心处理。
QString pocketToolError(const MachiningOperation &operation, const ToolEntry &tool)
{
    if (tool.type != QStringLiteral("end_mill")) {
        return QStringLiteral("tool T%1 is type '%2', but pocket machining requires 'end_mill'.")
            .arg(operation.toolId)
            .arg(tool.type);
    }
    const ContourFeature &feature = operation.contourFeature;
    const double limitingSize = feature.length > 0.0 && feature.width > 0.0
        ? std::min(feature.length, feature.width)
        : feature.radius * 2.0;
    if (limitingSize > 0.0 && tool.diameter >= limitingSize - 0.01) {
        return QStringLiteral("tool diameter %1 mm must be smaller than pocket limiting dimension %2 mm.")
            .arg(tool.diameter, 0, 'f', 3)
            .arg(limitingSize, 0, 'f', 3);
    }
    if (tool.fluteLen > 0.0 && feature.depth > tool.fluteLen + 0.01) {
        return QStringLiteral("pocket depth %1 mm exceeds tool flute length %2 mm.")
            .arg(feature.depth, 0, 'f', 3)
            .arg(tool.fluteLen, 0, 'f', 3);
    }
    if (tool.totalLen > 0.0 && feature.depth >= tool.totalLen - 0.01) {
        return QStringLiteral("pocket depth %1 mm reaches tool total length %2 mm; safe stick-out is impossible.")
            .arg(feature.depth, 0, 'f', 3)
            .arg(tool.totalLen, 0, 'f', 3);
    }
    return QString();
}

// 函数说明：generationParams负责当前模块的核心处理。
StrategyParams generationParams(const MachiningOperation &operation)
{
    StrategyParams params = operation.params;
    if (isSlotMillingStrategy(operation.strategyId)) {
        params.values.remove(QStringLiteral("depth"));
        params.values.remove(QStringLiteral("slotLength"));
        params.values.remove(QStringLiteral("slotWidth"));
        params.values.remove(QStringLiteral("angle"));
    }
    return params;
}

// 函数说明：holeKey负责当前模块的核心处理。
QString holeKey(const HoleFeature &hole)
{
    return QStringLiteral("%1|%2|%3")
        .arg(hole.center.x(), 0, 'g', 17)
        .arg(hole.center.y(), 0, 'g', 17)
        .arg(hole.center.z(), 0, 'g', 17);
}

// 函数说明：holeBatchKey负责当前模块的核心处理。
QString holeBatchKey(const HoleFeature &hole)
{
    const QVector3D axis = hole.axis.normalized();
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11")
        .arg(static_cast<int>(hole.kind))
        .arg(hole.subType)
        .arg(hole.radius, 0, 'g', 17)
        .arg(hole.depth, 0, 'g', 17)
        .arg(hole.secondaryRadius, 0, 'g', 17)
        .arg(hole.pitch, 0, 'g', 17)
        .arg(static_cast<int>(hole.region))
        .arg(std::abs(axis.x()), 0, 'g', 17)
        .arg(std::abs(axis.y()), 0, 'g', 17)
        .arg(std::abs(axis.z()), 0, 'g', 17)
        .arg(hole.center.z(), 0, 'g', 17);
}

// 函数说明：sharesHoleBatch负责当前模块的核心处理。
bool sharesHoleBatch(const MachiningOperation &first,
                     const MachiningOperation &candidate)
{
    if (first.strategyId == QStringLiteral("hole_spot")) {
        return true;
    }
    return holeBatchKey(first.holeFeature) == holeBatchKey(candidate.holeFeature);
}

// 函数说明：operationSummary负责当前模块的核心处理。
QString operationSummary(const MachiningOperation &operation)
{
    if (operation.opType == OperationType::Hole) {
        return QStringLiteral("%1 | D%2 Z-%3 | T%4")
            .arg(operation.strategyId)
            .arg(operation.holeFeature.radius * 2.0, 0, 'f', 3)
            .arg(operation.holeFeature.depth, 0, 'f', 3)
            .arg(operation.toolId);
    }
    return QStringLiteral("%1 | Z-%2 | T%3")
        .arg(operation.strategyId)
        .arg(operation.contourFeature.depth, 0, 'f', 3)
        .arg(operation.toolId);
}

// 函数说明：operationBlock负责当前模块的核心处理。
QString operationBlock(int operationNumber,
                       const MachiningOperation &operation,
                       const QStringList &sourceOperationIds,
                       const QString &gcode)
{
    QStringList lines;
    QString header = QStringLiteral("; ---- Operation %1").arg(operationNumber);
    for (const QString &operationId : sourceOperationIds) {
        if (!operationId.trimmed().isEmpty()) {
            header += QStringLiteral(" [op:%1]").arg(operationId);
        }
    }
    header += QStringLiteral(" ----");
    lines << header;
    lines << QStringLiteral("; %1").arg(operationSummary(operation));
    const int workOffset = static_cast<int>(std::round(
        operation.params.get(QStringLiteral("workOffset"), 54.0)));
    lines << QStringLiteral("G%1").arg(workOffset);
    lines << gcode.trimmed();
    lines << QStringLiteral("; ---- End Operation %1 ----").arg(operationNumber);
    return lines.join(QLatin1Char('\n'));
}

// 函数说明：internalHoleCycleError负责当前模块的核心处理。
QString internalHoleCycleError(const QString &gcode)
{
    static const QRegularExpression codePattern(
        QStringLiteral("(?:^|\\s)code=([^\\s]+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression peckPattern(
        QStringLiteral("(?:^|\\s)q=([^\\s]+)"),
        QRegularExpression::CaseInsensitiveOption);
    const QSet<QString> supportedCodes{
        QStringLiteral("G73"),
        QStringLiteral("G81"),
        QStringLiteral("G82"),
        QStringLiteral("G83"),
        QStringLiteral("G84"),
        QStringLiteral("G85"),
        QStringLiteral("G76"),
        QStringLiteral("G86")};

    const QStringList lines = gcode.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QStringLiteral(";CNEXT_HOLE_CYCLE"))) {
            continue;
        }

        const QRegularExpressionMatch match = codePattern.match(trimmed);
        if (!match.hasMatch()) {
            return QStringLiteral("CNEXT hole cycle marker is missing its code.");
        }

        const QString code = match.captured(1).toUpper();
        if (!supportedCodes.contains(code)) {
            return QStringLiteral("CNEXT hole cycle '%1' is not supported by the postprocessor.")
                .arg(code);
        }
        if (code == QStringLiteral("G73") || code == QStringLiteral("G83") ||
            code == QStringLiteral("G76")) {
            const QRegularExpressionMatch peckMatch = peckPattern.match(trimmed);
            bool peckOk = false;
            const double peckDepth = peckMatch.hasMatch()
                ? peckMatch.captured(1).toDouble(&peckOk)
                : 0.0;
            if (!peckOk || !std::isfinite(peckDepth) || peckDepth <= 0.0) {
                return code == QStringLiteral("G76")
                    ? QStringLiteral("CNEXT hole cycle 'G76' requires a positive finite lateral offset Q.")
                    : QStringLiteral("CNEXT hole cycle '%1' requires a positive peck depth Q.").arg(code);
            }
        }
    }
    return QString();
}

bool siemensCycle83ContractConfirmed(const MachineProfile &profile)
{
    return profile.controllerId == QStringLiteral("siemens-840d") &&
           profile.id == QStringLiteral("siemens-840d-pgz-1102") &&
           profile.version == QStringLiteral("sw6");
}

QString siemensCycleContractError(const QString &gcode, const MachineProfile &profile)
{
    if (gcode.contains(QStringLiteral("code=G76"), Qt::CaseInsensitive) ||
        gcode.contains(QStringLiteral("code=G86"), Qt::CaseInsensitive)) {
        return QStringLiteral(
            "Siemens G76/G86 generation requires a machine-builder-specific boring-cycle contract; "
            "Fanuc syntax is not emitted for Siemens.");
    }
    if (siemensCycle83ContractConfirmed(profile)) {
        return QString();
    }
    if (gcode.contains(QStringLiteral("code=G73"), Qt::CaseInsensitive) ||
        gcode.contains(QStringLiteral("code=G83"), Qt::CaseInsensitive)) {
        return QStringLiteral(
            "Siemens G73/G83 generation requires the explicitly confirmed "
            "siemens-840d-pgz-1102/sw6 cycle contract.");
    }
    return QString();
}

// 函数说明：expandParametricProgram负责当前模块的核心处理。
QString expandParametricProgram(const ParametricToolpathProgram &program)
{
    QStringList lines = program.prefixLines;
    for (const ParametricToolpathCall &call : program.calls) {
        for (QString line : program.bodyTemplateLines) {
            for (auto argument = call.arguments.cbegin(); argument != call.arguments.cend();
                 ++argument) {
                line.replace(QStringLiteral("${%1}").arg(argument.key()), argument.value());
            }
            lines.append(line);
        }
    }
    lines.append(program.suffixLines);
    return lines.join(QLatin1Char('\n'));
}

// 函数说明：sha256负责当前模块的核心处理。
QString sha256(const QString &content)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256).toHex())
        .toUpper();
}

} // namespace

// 函数说明：ProgramGenerationService负责当前模块的核心处理。
ProgramGenerationService::ProgramGenerationService(StrategyLookup strategyLookup,
                                                   ToolLookup toolLookup)
    // 函数说明：m_strategyLookup负责当前模块的核心处理。
    : m_strategyLookup(std::move(strategyLookup)),
      // 函数说明：m_toolLookup负责当前模块的核心处理。
      m_toolLookup(std::move(toolLookup))
{
}

// 中文说明：按工序顺序构造后处理输入，执行 Setup/刀具/参数安全门，
// 再生成程序快照并校验最终文本；任何一步失败都不返回可发布程序。
ProgramGenerationResult ProgramGenerationService::generate(
    const QList<MachiningOperation> &input,
    const PostProcessorBase &postProcessor,
    const PostProcessorOptions &options,
    const ProgramGenerationSnapshotOptions &snapshotOptions) const
{
    auto operations = input;
    for (auto &operation : operations) if (operation.opType != OperationType::Hole)
        operation.contourFeature = applyMachiningParameters(operation.contourFeature,operation.params);
    ProgramGenerationResult output;
    if ((snapshotOptions.sourceType==ProjectSourceType::Sketch)!=bool(snapshotOptions.sketchContext)) {
        output.errors<<QStringLiteral("Sketch generation requires current sketch verification context.");return output;
    }
    if(snapshotOptions.sketchContext) {
        const auto &context=*snapshotOptions.sketchContext;
        if(!context.error.isEmpty() || context.sourceFingerprint!=snapshotOptions.sourceFingerprint ||
           context.setupFingerprint!=snapshotOptions.setupFingerprint || context.modelOrigin!=snapshotOptions.modelOrigin ||
           context.operations.size()!=operations.size()) {
            output.errors<<(context.error.isEmpty()?QStringLiteral("Sketch context is stale."):context.error);return output;
        }
        for(int i=0;i<operations.size();++i) {
            auto &operation=operations[i];
            const auto &checked=context.operations[i];
            if(operation.strategyId!="mill_closed_contour" || operation.id!=checked.id ||
               checked.selectionFingerprint!=ProgramSnapshotFingerprint::calculate({input[i]},{input[i].id}) ||
               ProgramSnapshotFingerprint::toolFingerprint(checked.tool)!=
                   ProgramSnapshotFingerprint::toolFingerprint(m_toolLookup(operation.toolId))) {
                output.errors<<QStringLiteral("Sketch operation or tool evidence is stale.");return output;
            }
            const auto binding=bindSketchSelection(context.document,operation.selectionEvidence,operation.params,
                                                   context.setupFingerprint,context.workOffset);
            if(!binding.error.isEmpty()){output.errors<<binding.error;return output;}
            operation.contourFeature.precisePoints=binding.points;
            operation.contourFeature.center.setZ(context.document.planeZ);
        }
    } else {
        for(auto &operation:operations) operation.contourFeature.precisePoints.clear();
    }
    for (int axis=0; axis<3; ++axis) if (!std::isfinite(snapshotOptions.modelOrigin[axis])) {
        output.errors << QStringLiteral("The resolved work origin must be finite.");
        return output;
    }
    if (operations.isEmpty()) {
        output.errors << QStringLiteral("No confirmed operations were provided.");
        return output;
    }

    QSet<QString> operationIds;
    const QRegularExpression safeOperationId(QStringLiteral("\\A[A-Za-z0-9_.:-]+\\z"));
    for (int index = 0; index < operations.size(); ++index) {
        if (!safeOperationId.match(operations[index].id).hasMatch()) {
            output.errors << QStringLiteral("Operation %1: INVALID_OPERATION_ID.").arg(index + 1);
        }
        if(operationIds.contains(operations[index].id))
            output.errors << QStringLiteral("Operation %1 has a duplicate operation ID.").arg(index+1);
        operationIds.insert(operations[index].id);
        if (!operations[index].enabled) {
            output.errors << QStringLiteral("Operation %1 is disabled.").arg(index + 1);
        }
        if (operations[index].toolpathState == ToolpathState::Stale ||
            operations[index].toolpathState == ToolpathState::Error) {
            output.errors << QStringLiteral(
                "Operation %1 has an invalidated toolpath and must be recalculated.")
                                 .arg(index + 1);
        }
        if (operations[index].id.trimmed().isEmpty()) {
            output.errors << QStringLiteral("Operation %1 is not a confirmed operation.")
                                 .arg(index + 1);
        }
        const QString parameterError = nonfiniteParameterError(operations[index].params);
        if (!parameterError.isEmpty()) {
            output.errors << QStringLiteral("Operation %1: %2")
                                 .arg(index + 1)
                                 .arg(parameterError);
        }
        const double configuredWorkOffset =
            operations[index].params.get(QStringLiteral("workOffset"), 54.0);
        if (!std::isfinite(configuredWorkOffset) ||
            std::round(configuredWorkOffset) != configuredWorkOffset ||
            configuredWorkOffset < 54.0 || configuredWorkOffset > 59.0) {
            output.errors << QStringLiteral(
                "Operation %1: work offset must be an integer from G54-G59.")
                                 .arg(index + 1);
        }
        const double depthMode =
            operations[index].params.get(QStringLiteral("depthMode"), 0.0);
        if (operations[index].selectionEvidence.explicitUserSelection) {
            const SetupAccessResult access = evaluateSelectionAccess(
                operations[index].selectionEvidence,
                snapshotOptions.sourceFingerprint, snapshotOptions.setupFingerprint,
                QStringLiteral("G%1").arg(configuredWorkOffset),
                machiningRequirements(operations[index].strategyId).closedBoundary);
            if (!access.ok()) {
                output.errors << QStringLiteral("Operation %1: %2")
                                     .arg(index + 1).arg(access.issueCode);
            }
        }
        if (operations[index].opType != OperationType::Hole) {
            const QString sideError = contourSideOperationError(operations[index].selectionEvidence,
                operations[index].contourFeature,operations[index].params,operations[index].strategyId);
            if (!sideError.isEmpty()) output.errors << sideError;
            const QString openingError = pocketOpeningOperationError(operations[index].selectionEvidence,
                operations[index].contourFeature,operations[index].params,operations[index].strategyId,snapshotOptions.sourceMesh);
            if (!openingError.isEmpty()) output.errors << openingError;
            const QString slopeError=slopedFloorOperationError(operations[index].selectionEvidence,
                operations[index].contourFeature,operations[index].strategyId,snapshotOptions.sourceMesh);
            if(!slopeError.isEmpty())output.errors<<slopeError;
        }
        if (depthMode != 0.0) {
            output.errors << QStringLiteral(
                "Operation %1: incremental depth mode is not supported; use absolute depth mode.")
                                 .arg(index + 1);
        }
        if (operations[index].opType == OperationType::Hole) {
            if (operations[index].holeFeature.region == FaceRegion::Side) {
                output.errors << QStringLiteral(
                    "Operation %1: Side-face hole requires a dedicated Setup before G-code generation.")
                                     .arg(index + 1);
            } else if (operations[index].holeFeature.region == FaceRegion::Back) {
                output.errors << QStringLiteral(
                    "Operation %1: Back-face hole requires a transformed Setup before G-code generation.")
                                     .arg(index + 1);
            }
        } else if (isSlotMillingStrategy(operations[index].strategyId)) {
            if (!slotAxisSupportsZMachining(operations[index].contourFeature.axis)) {
                output.errors << QStringLiteral(
                    "Operation %1: slot axis is not aligned with the front-face Z workflow; a transformed Setup is required before G-code generation.")
                                     .arg(index + 1);
            } else if (operations[index].contourFeature.region == FaceRegion::Side) {
                if (!operations[index].selectionEvidence.explicitUserSelection) {
                    output.errors << QStringLiteral(
                        "Operation %1: Side-face slot requires an explicit accessible machining-face selection.")
                                         .arg(index + 1);
                } else {
                    const SetupAccessResult access = evaluateSelectionAccess(
                        operations[index].selectionEvidence,
                        snapshotOptions.sourceFingerprint,
                        snapshotOptions.setupFingerprint,
                        QStringLiteral("G%1").arg(static_cast<int>(std::round(configuredWorkOffset))),
                        machiningRequirements(operations[index].strategyId).closedBoundary);
                    if (!access.ok()) {
                        output.errors << QStringLiteral(
                            "Operation %1: side-face slot selection is not accessible (%2).")
                                             .arg(index + 1)
                                             .arg(access.issueCode);
                    }
                }
            } else if (operations[index].contourFeature.region == FaceRegion::Back) {
                output.errors << QStringLiteral(
                    "Operation %1: Back-face slot requires a transformed Setup before G-code generation.")
                                     .arg(index + 1);
            }
        } else if (isPocketMillingStrategy(operations[index].strategyId)) {
            const QVector3D pocketAxis = operations[index].contourFeature.axis.normalized();
            if (pocketAxis.lengthSquared() > 1.0e-8f && std::abs(pocketAxis.z()) < 0.65f) {
                output.errors << QStringLiteral(
                    "Operation %1: pocket axis is not aligned with the front-face Z workflow; a transformed Setup is required before G-code generation.")
                                     .arg(index + 1);
            } else if (operations[index].contourFeature.region == FaceRegion::Side) {
                if (!operations[index].selectionEvidence.explicitUserSelection) {
                    output.errors << QStringLiteral(
                        "Operation %1: Side-face pocket requires an explicit accessible machining-face selection.")
                                         .arg(index + 1);
                } else {
                    const SetupAccessResult access = evaluateSelectionAccess(
                        operations[index].selectionEvidence,
                        snapshotOptions.sourceFingerprint,
                        snapshotOptions.setupFingerprint,
                        QStringLiteral("G%1").arg(static_cast<int>(std::round(configuredWorkOffset))),
                        machiningRequirements(operations[index].strategyId).closedBoundary);
                    if (!access.ok()) {
                        output.errors << QStringLiteral(
                            "Operation %1: side-face pocket selection is not accessible (%2).")
                                             .arg(index + 1)
                                             .arg(access.issueCode);
                    }
                }
            } else if (operations[index].contourFeature.region == FaceRegion::Back) {
                output.errors << QStringLiteral(
                    "Operation %1: Back-face pocket requires a transformed Setup before G-code generation.")
                                     .arg(index + 1);
            }
        } else if (operations[index].opType == OperationType::Contour) {
            const QVector3D contourAxis = operations[index].contourFeature.axis.normalized();
            if (contourAxis.lengthSquared() > 1.0e-8f && std::abs(contourAxis.z()) < 0.65f) {
                output.errors << QStringLiteral(
                    "Operation %1: contour axis is not aligned with the front-face Z workflow; a transformed Setup is required before G-code generation.")
                                     .arg(index + 1);
            } else if (operations[index].contourFeature.region == FaceRegion::Side) {
                if (!operations[index].selectionEvidence.explicitUserSelection) {
                    output.errors << QStringLiteral(
                        "Operation %1: Side-face contour requires an explicit accessible machining-face selection.")
                                         .arg(index + 1);
                } else {
                    const SetupAccessResult access = evaluateSelectionAccess(
                        operations[index].selectionEvidence,
                        snapshotOptions.sourceFingerprint,
                        snapshotOptions.setupFingerprint,
                        QStringLiteral("G%1").arg(static_cast<int>(std::round(configuredWorkOffset))),
                        machiningRequirements(operations[index].strategyId).closedBoundary);
                    if (!access.ok()) {
                        output.errors << QStringLiteral(
                            "Operation %1: side-face contour selection is not accessible (%2).")
                                             .arg(index + 1)
                                             .arg(access.issueCode);
                    }
                }
            } else if (operations[index].contourFeature.region == FaceRegion::Back) {
                output.errors << QStringLiteral(
                    "Operation %1: Back-face contour requires a transformed Setup before G-code generation.")
                                     .arg(index + 1);
            }
        }
    }
    if (!output.errors.isEmpty()) {
        return output;
    }

    const auto sourceOperations = operations;
    for (auto &operation : operations) {
        operation.holeFeature = inWorkCoordinates(operation.holeFeature,snapshotOptions.modelOrigin);
        operation.contourFeature = inWorkCoordinates(operation.contourFeature,snapshotOptions.modelOrigin);
        const auto heightError=operation.opType==OperationType::Hole
            ? workHeightError(operation.holeFeature,operation.params,operation.strategyId)
            : workHeightError(operation.contourFeature,operation.params);
        if(!heightError.isEmpty())output.errors << heightError;
    }
    if(!output.errors.isEmpty())return output;
    const auto workStock = inWorkCoordinates(snapshotOptions.stockBounds,snapshotOptions.modelOrigin);
    QStringList blocks;
    QMap<QString, QString> chamferReferenceBodies;
    QList<ParametricToolpathProgram> parametricPrograms;
    QSet<QString> holesWithDeepCycle;
    QSet<QString> holesWithCircularMill;
    QHash<int, ToolEntry> generationTools;
    for (const MachiningOperation &operation : operations) {
        if (operation.opType != OperationType::Hole) {
            continue;
        }
        if (operation.strategyId == QStringLiteral("hole_deephole")) {
            holesWithDeepCycle.insert(holeKey(operation.holeFeature));
        } else if (operation.strategyId == QStringLiteral("hole_circular_mill")) {
            holesWithCircularMill.insert(holeKey(operation.holeFeature));
        }
    }

    QSet<QString> peckedHoles;
    int index = 0;
    while (index < operations.size()) {
        const MachiningOperation &operation = operations[index];
        if (operation.opType == OperationType::Hole &&
            operation.strategyId == QStringLiteral("hole_peck") &&
            holesWithDeepCycle.contains(holeKey(operation.holeFeature))) {
            ++index;
            continue;
        }
        if (operation.opType == OperationType::Hole &&
            operation.strategyId == QStringLiteral("hole_peck") &&
            holesWithCircularMill.contains(holeKey(operation.holeFeature)) &&
            peckedHoles.contains(holeKey(operation.holeFeature))) {
            ++index;
            continue;
        }

        const std::shared_ptr<StrategyBase> strategy =
            m_strategyLookup ? m_strategyLookup(operation.strategyId)
                             : std::shared_ptr<StrategyBase>();
        if (!strategy) {
            output.errors << QStringLiteral("Operation %1: strategy '%2' was not found.")
                                     .arg(index + 1)
                                     .arg(operation.strategyId);
            ++index;
            continue;
        }

        const ToolEntry tool = generationTools.contains(operation.toolId)
            ? generationTools.value(operation.toolId)
            : m_toolLookup ? m_toolLookup(operation.toolId) : ToolEntry();
        const QString numericToolError = nonfiniteToolError(tool);
        if (!numericToolError.isEmpty()) {
            output.errors << QStringLiteral("Operation %1: %2")
                                 .arg(index + 1)
                                 .arg(numericToolError);
            ++index;
            continue;
        }
        if (tool.id <= 0 || tool.diameter <= 0.0) {
            output.errors << QStringLiteral("Operation %1: tool T%2 is invalid or missing.")
                                     .arg(index + 1)
                                     .arg(operation.toolId);
            ++index;
            continue;
        }

        generationTools.insert(operation.toolId, tool);
        const QString toolError = operation.opType == OperationType::Hole
            ? holeToolError(operation, tool)
            : isSlotMillingStrategy(operation.strategyId)
                ? slotToolError(operation, tool)
                : isPocketMillingStrategy(operation.strategyId)
                    ? pocketToolError(operation, tool)
            : QString();
        if (!toolError.isEmpty()) {
            output.errors << QStringLiteral("Operation %1: %2")
                                 .arg(index + 1)
                                 .arg(toolError);
            ++index;
            continue;
        }

        if (operation.opType == OperationType::Hole) {
            QVector<HoleFeature> batch;
            int next = index;
            while (next < operations.size() &&
                   operations[next].opType == OperationType::Hole &&
                   operations[next].strategyId == operation.strategyId &&
                   operations[next].toolId == operation.toolId &&
                   operations[next].params.values == operation.params.values &&
                   sharesHoleBatch(operation, operations[next])) {
                batch.append(operations[next].holeFeature);
                ++next;
            }

            const ToolpathResult result = strategy->generate(batch, tool, operation.params);
            if (result.ok && !result.gcode.trimmed().isEmpty()) {
                const QString cycleError = internalHoleCycleError(result.gcode);
                if (!cycleError.isEmpty()) {
                    output.errors << QStringLiteral("Operation %1: %2")
                                         .arg(index + 1)
                                         .arg(cycleError);
                } else {
                    QStringList batchOperationIds;
                    for (int batchIndex = index; batchIndex < next; ++batchIndex) {
                        batchOperationIds << operations[batchIndex].id;
                    }
                    QString body = result.gcode;
                    if (operation.strategyId == "hole_chamfer" && operation.params.get("chamferMode", 0) == 0) {
                        const auto reference = ChamferStrategy().generate(batch, tool, operation.params);
                        if (!reference.ok) {
                            output.errors << reference.errorMsg;
                        } else {
                            const QString key = QString("[%1]").arg(index + 1);
                            chamferReferenceBodies.insert(key, reference.gcode);
                            body = "; CNEXT_CHAMFER_BEGIN " + key + '\n' + body
                                + "\n; CNEXT_CHAMFER_END " + key + '\n';
                        }
                    }
                    blocks << operationBlock(index + 1,
                                             operation,
                                             batchOperationIds,
                                             body);
                    if (!result.parametricProgram.isEmpty()) {
                        ParametricToolpathProgram parametricProgram =
                            result.parametricProgram;
                        parametricProgram.sourceOperationIds = batchOperationIds;
                        parametricPrograms.append(parametricProgram);
                    }
                }
            } else {
                const QString reason = result.errorMsg.trimmed().isEmpty()
                    ? QStringLiteral("strategy produced no G-code")
                    : result.errorMsg;
                output.errors << QStringLiteral("Operation %1: %2")
                                     .arg(index + 1)
                                     .arg(reason);
            }
            if (operation.strategyId == QStringLiteral("hole_peck")) {
                for (const HoleFeature &hole : batch) {
                    peckedHoles.insert(holeKey(hole));
                }
            }
            index = next;
            continue;
        }

        if (isPlanarMillingStrategy(operation.strategyId)) {
            QString error=planarMachiningError(operation.contourFeature,tool,operation.params);
            if(error.isEmpty()) error=snapshotOptions.stockConfirmed
                ? planarStockEntryError(operation.contourFeature,tool,operation.params,workStock)
                : QStringLiteral("Confirm stock bounds before generating planar milling.");
            if(!error.isEmpty()) {output.errors << QStringLiteral("Operation %1: %2").arg(index+1).arg(error);++index;continue;}
        }
        const ToolpathResult result = strategy->generate(
            operation.contourFeature, tool, generationParams(operation));
        if (result.ok && !result.gcode.trimmed().isEmpty()) {
            blocks << operationBlock(index + 1,
                                     operation,
                                     QStringList{operation.id},
                                     result.gcode);
            if (!result.parametricProgram.isEmpty()) {
                ParametricToolpathProgram parametricProgram =
                    result.parametricProgram;
                parametricProgram.sourceOperationIds = QStringList{operation.id};
                parametricPrograms.append(parametricProgram);
            }
        } else {
            const QString reason = result.errorMsg.trimmed().isEmpty()
                ? QStringLiteral("strategy produced no G-code")
                : result.errorMsg;
            output.errors << QStringLiteral("Operation %1: %2")
                                 .arg(index + 1)
                                 .arg(reason);
        }
        ++index;
    }

    if (!output.errors.isEmpty()) {
        return output;
    }
    if (blocks.isEmpty()) {
        output.errors << QStringLiteral("No G-code blocks were generated.");
        return output;
    }

    const QString siemensCycleError = postProcessor.id() == QStringLiteral("siemens")
        ? siemensCycleContractError(blocks.join(QLatin1Char('\n')),
                                    snapshotOptions.machineProfile)
        : QString();
    if (!siemensCycleError.isEmpty()) {
        output.errors << siemensCycleError;
        return output;
    }

    const Cq8MacroProgram cq8MacroProgram = Cq8MacroProgramBuilder::build(parametricPrograms);
    if (!cq8MacroProgram.ok) {
        output.errors << cq8MacroProgram.error;
        return output;
    }

    const QString expandedGCode = GCodeModalOptimizer::optimize(
        postProcessor.wrapGCode(blocks.join(QLatin1Char('\n')).split(QLatin1Char('\n')),
                                options));
    const GCodeSafetyReport safetyReport = GCodeSafetyValidator::validate(expandedGCode);
    if (!safetyReport.ok) {
        output.errors = safetyReport.messages;
        return output;
    }

    if(snapshotOptions.sketchContext) {
        SimulationController verifier;
        verifier.setSketchVerificationContext(*snapshotOptions.sketchContext);
        verifier.loadGCode(expandedGCode,false,0,snapshotOptions.modelOrigin);
        if(verifier.checkResult().blocksOutput()) {
            output.errors<<verifier.checkResult().message;return output;
        }
    }

    QHash<QString,const MachiningOperation *> regionOperations;
    QHash<QString,double> slopedSourceTops;
    bool hasSlopedFloor=false;
    for(const auto &operation:operations)
        if(operation.strategyId=="mill_sloped_floor" || (isPocketRegionStrategy(operation.strategyId) && operation.contourFeature.points.size()>=3 &&
           (operation.strategyId=="mill_pocket_rough" || operation.contourFeature.subType=="verified_slot_opening"))) {
            regionOperations.insert(operation.id,&operation);
            hasSlopedFloor|=operation.strategyId=="mill_sloped_floor";
            if(operation.strategyId=="mill_sloped_floor") {
                const auto bound=bindSlopedFloor(*snapshotOptions.sourceMesh,operation.selectionEvidence);
                if(!bound.error.isEmpty()){output.errors<<bound.error;return output;}
                slopedSourceTops.insert(operation.id,bound.sourceTopZ);
            }
        }
    if(!regionOperations.isEmpty()) {
        const auto lines=expandedGCode.split('\n');
        QHash<QString,const MachiningOperation *> allOperations;
        double initialSafeZ=0;
        for(const auto &operation:operations) {
            allOperations.insert(operation.id,&operation);
            initialSafeZ=std::max(initialSafeZ,operation.params.get("safeHeight",50));
            if(regionOperations.contains(operation.id)) {
                for(const auto &point:operation.contourFeature.points)
                    initialSafeZ=std::max(initialSafeZ,double(point.z())+operation.params.get("feedHeight",3));
                if(slopedSourceTops.contains(operation.id))
                    initialSafeZ=std::max(initialSafeZ,slopedSourceTops.value(operation.id)-snapshotOptions.modelOrigin.z()+operation.params.get("feedHeight",3));
            }
        }
        // Match the complete, ordered headers emitted by this generation, not
        // arbitrary op tags in strategy comments or postprocessor output.
        QStringList expectedHeaders;
        for(const auto &block:blocks) {
            expectedHeaders.append(block.section('\n',0,0));
            expectedHeaders.append(block.section('\n',-1));
        }
        int nextHeader=0;
        QVector<const MachiningOperation *> owners;const MachiningOperation *owner=nullptr;
        const QRegularExpression marker("\\[op:([^\\]]+)\\]");
        for(const auto &line:lines) {
            if(line.contains("[op:") || line.contains("; ---- End Operation ")) {
                if(nextHeader>=expectedHeaders.size() || line!=expectedHeaders[nextHeader]) {
                    output.errors<<QStringLiteral("FINAL_NC_OPERATION_OWNER_MISMATCH");return output;
                }
                if(nextHeader%2==0) {
                    const auto match=marker.match(line);
                    owner=allOperations.value(match.captured(1),nullptr);
                    if(!owner) {output.errors<<QStringLiteral("FINAL_NC_OPERATION_OWNER_UNKNOWN");return output;}
                } else owner=nullptr;
                ++nextHeader;
            }
            owners.append(owner);
        }
        if(nextHeader!=expectedHeaders.size()) {
            output.errors<<QStringLiteral("FINAL_NC_OPERATION_OWNER_MISSING");return output;
        }
        SimulationController parser;parser.loadGCode(expandedGCode,false);
        if(!parser.motionError().isEmpty()){output.errors<<parser.motionError();return output;}
        for(const auto &move:parser.moves()) {
            const auto *operation=move.sourceLine>=0 && move.sourceLine<owners.size()?owners[move.sourceLine]:nullptr;
            if(!operation) {
                // A program header may establish safe Z before any operation.
                if(move.isRapid && move.commandedAxes==4 && (move.toKnownAxes&4) &&
                   move.to.z()>=initialSafeZ &&
                   (!(move.fromKnownAxes&4) || move.from.z()>=initialSafeZ))continue;
                output.errors<<QStringLiteral("FINAL_NC_MOTION_OWNER_MISSING");return output;
            }
            if(!regionOperations.contains(operation->id))continue;
            if(operation->strategyId=="mill_sloped_floor") {
                QString error;
                const auto *mesh=snapshotOptions.sourceMesh;
                const double ceiling=slopedSourceTops.value(operation->id)+operation->params.get("feedHeight",3);
                const auto from=move.from+snapshotOptions.modelOrigin,to=move.to+snapshotOptions.modelOrigin;
                if(!mesh || move.chordDeviation!=0)error="SLOPED_FLOOR_LINEAR_SOURCE_MOTION_REQUIRED";
                else if(move.isRapid && move.commandedAxes==4 && (move.toKnownAxes&4) && move.fromKnownAxes!=7 &&
                        to.z()>=ceiling) {
                    // Establish the initial safe Z before the first known XY position.
                }
                else if(move.isRapid && move.toKnownAxes==7 && to.z()>=ceiling &&
                        (move.fromKnownAxes!=7 || from.z()>=ceiling)) {
                    // Initial positioning and XY links are wholly above all source material.
                } else if(move.fromKnownAxes!=7 || move.toKnownAxes!=7)error="SLOPED_FLOOR_UNKNOWN_MOTION";
                else if(move.isRapid && (from.x()!=to.x() || from.y()!=to.y() || to.z()<from.z()))error="SLOPED_FLOOR_UNSAFE_RAPID";
                else {
                    const int faceIndex=operation->selectionEvidence.orderedTopologyRefs.first().id.section(':',1).toInt();
                    error=slopedFloorMoveError(*mesh,faceIndex,from,to,generationTools.value(operation->toolId));
                }
                if(!error.isEmpty()){output.errors<<QString("%1 at final NC line %2").arg(error).arg(move.sourceLine+1);return output;}
                continue;
            }
            const QString error=regionMoveError(move,operation->contourFeature,operation->params,generationTools.value(operation->toolId).diameter);
            if(!error.isEmpty()){output.errors<<QString("%1 at final NC line %2").arg(error).arg(move.sourceLine+1);return output;}
        }
    }

    QStringList sourceOperationIds;
    for (const MachiningOperation &operation : operations) {
        if (!operation.id.trimmed().isEmpty()) {
            sourceOperationIds << operation.id;
        }
    }

    QString finalGCode = expandedGCode;
    // Keep mixed chamfer programs expanded: the final-motion proof cannot parse
    // controller macro calls. Macro-only programs retain their compact output.
    if (postProcessor.id() == QStringLiteral("cq8") && !parametricPrograms.isEmpty()
        && chamferReferenceBodies.isEmpty() && !hasSlopedFloor) {
        QStringList compactBlocks = blocks;
        for (int parametricIndex = 0; parametricIndex < parametricPrograms.size(); ++parametricIndex) {
            const ParametricToolpathProgram &parametricProgram = parametricPrograms.at(parametricIndex);
            const QString expanded = expandParametricProgram(parametricProgram).trimmed();
            const QString compact = (parametricProgram.prefixLines +
                                     cq8MacroProgram.callBlocks.at(parametricIndex).split(QLatin1Char('\n')) +
                                     parametricProgram.suffixLines)
                                        .join(QLatin1Char('\n')).trimmed();
            bool replaced = false;
            for (QString &block : compactBlocks) {
                if (block.contains(expanded)) {
                    block.replace(expanded, compact);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                output.errors << QStringLiteral(
                    "CQ8 routine '%1' does not reproduce its expanded toolpath exactly.")
                                     .arg(parametricProgram.routineName);
                return output;
            }
        }
        finalGCode = GCodeModalOptimizer::optimize(
            postProcessor.wrapGCode(compactBlocks.join(QLatin1Char('\n')).split(QLatin1Char('\n')),
                                    options));
        const GCodeSafetyReport compactSafetyReport = GCodeSafetyValidator::validate(finalGCode);
        if (!compactSafetyReport.ok) {
            output.errors = compactSafetyReport.messages;
            return output;
        }
    }

    const QString chamferMotionError = chamferFinalMotionError(finalGCode, chamferReferenceBodies);
    if (!chamferMotionError.isEmpty()) {
        output.errors << chamferMotionError;
        return output;
    }

    ProgramEntry snapshot;
    snapshot.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    snapshot.createdAt = QDateTime::currentDateTime();
    snapshot.name = snapshotOptions.name;
    snapshot.postProcessorId = postProcessor.id();
    snapshot.sourceOperationIds = sourceOperationIds;
    snapshot.sourceOperationFingerprint =
        ProgramSnapshotFingerprint::calculate(sourceOperations, sourceOperationIds);
    snapshot.toolFingerprint = ProgramSnapshotFingerprint::calculateTools(
        operations, sourceOperationIds, [&generationTools](int id) { return generationTools.value(id); });
    snapshot.machineProfileId = snapshotOptions.machineProfile.id;
    snapshot.machineProfileVersion = snapshotOptions.machineProfile.version;
    snapshot.setupFingerprint = snapshotOptions.setupFingerprint;
    snapshot.modelOrigin = snapshotOptions.modelOrigin;
    snapshot.coordinateVersion = 1;
    snapshot.coordinateEvidence = snapshot.coordinateFingerprint();
    snapshot.safeStartBlocks = resolvedSafeStartBlocks(options);
    snapshot.sourceSummary = snapshotOptions.sourceSummary;
    snapshot.gcodeText = finalGCode;
    snapshot.expandedGcodeText = finalGCode == expandedGCode ? QString() : expandedGCode;
    if (!cq8MacroProgram.libraryText.isEmpty() && chamferReferenceBodies.isEmpty() && !hasSlopedFloor) {
        snapshot.macroText = QStringLiteral("; CQ8 MACRO CALLS\n%1\n\n; CQ8 MACRO LIBRARY\n%2")
                                 .arg(cq8MacroProgram.callText,
                                      cq8MacroProgram.libraryText);
    }
    snapshot.parametricPrograms = parametricPrograms;
    snapshot.lineCount = finalGCode.count(QLatin1Char('\n')) + 1;
    snapshot.expandedLineCount = expandedGCode.count(QLatin1Char('\n')) + 1;

    if (postProcessor.id() == QStringLiteral("cq8") && !snapshot.macroText.isEmpty()) {
        ProgramFileEntry mainFile;
        mainFile.kind = QStringLiteral("main");
        mainFile.fileName = QStringLiteral("CQ8_MAIN.NC");
        mainFile.content = finalGCode;
        mainFile.sha256 = sha256(mainFile.content);
        snapshot.mainProgramFileName = mainFile.fileName;
        snapshot.packageFiles << mainFile;

        ProgramFileEntry macroFile;
        macroFile.kind = QStringLiteral("macro");
        macroFile.fileName = QStringLiteral("CQ8_MACROS.NC");
        macroFile.content = cq8MacroProgram.libraryText;
        macroFile.sha256 = sha256(macroFile.content);
        snapshot.packageFiles << macroFile;
    }

    if (postProcessor.id() == QStringLiteral("siemens")) {
        const SiemensProgramPackage package =
            SiemensProgramPackageBuilder::fromValidatedMainProgram(
                snapshotOptions.mainProgramName,
                finalGCode);
        if (!package.ok) {
            output.errors << package.error;
            return output;
        }

        ProgramFileEntry mainFile;
        mainFile.kind = QStringLiteral("main");
        mainFile.fileName = package.mainProgram.fileName;
        mainFile.content = package.mainProgram.content;
        mainFile.sha256 = package.mainProgram.sha256;
        snapshot.mainProgramFileName = mainFile.fileName;
        snapshot.packageFiles << mainFile;
    }

    snapshot.generationContentEvidence = snapshot.generationContentFingerprint();
    output.snapshot = snapshot;
    output.ok = true;
    return output;
}
