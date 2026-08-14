#include "ProgramGenerationService.h"

#include "../gcode/GCodeModalOptimizer.h"
#include "../gcode/GCodeSafetyValidator.h"
#include "../gcode/Cq8MacroProgramBuilder.h"
#include "../gcode/ProgramSnapshotFingerprint.h"
#include "../gcode/SiemensProgramPackage.h"
#include "../strategies/StrategyBase.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

bool isSlotMillingStrategy(const QString &strategyId)
{
    return strategyId == QStringLiteral("mill_slot") ||
           strategyId == QStringLiteral("mill_blind_slot") ||
           strategyId == QStringLiteral("mill_tapered_slot");
}

bool isPocketMillingStrategy(const QString &strategyId)
{
    return strategyId == QStringLiteral("mill_pocket_rough") ||
           strategyId == QStringLiteral("mill_pocket_finish") ||
           strategyId == QStringLiteral("mill_pocket_floor_finish");
}

QString expectedHoleToolType(const QString &strategyId)
{
    if (strategyId == QStringLiteral("hole_spot")) return QStringLiteral("spot_drill");
    if (strategyId == QStringLiteral("hole_tapping")) return QStringLiteral("tap");
    if (strategyId == QStringLiteral("hole_reaming")) return QStringLiteral("reamer");
    if (strategyId == QStringLiteral("hole_chamfer")) return QStringLiteral("chamfer_mill");
    if (strategyId == QStringLiteral("hole_peck") ||
        strategyId == QStringLiteral("hole_deephole")) {
        return QStringLiteral("drill");
    }
    if (strategyId == QStringLiteral("hole_circular_mill")) {
        return QStringLiteral("end_mill");
    }
    return QString();
}

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

    if (tool.fluteLen > 0.0 &&
        operation.holeFeature.depth > tool.fluteLen + 0.01) {
        return QStringLiteral("cutting depth %1 mm exceeds tool flute length %2 mm.")
            .arg(operation.holeFeature.depth, 0, 'f', 3)
            .arg(tool.fluteLen, 0, 'f', 3);
    }
    if (tool.totalLen > 0.0 &&
        operation.holeFeature.depth >= tool.totalLen - 0.01) {
        return QStringLiteral("cutting depth %1 mm reaches tool total length %2 mm; safe stick-out is impossible.")
            .arg(operation.holeFeature.depth, 0, 'f', 3)
            .arg(tool.totalLen, 0, 'f', 3);
    }

    if (operation.strategyId == QStringLiteral("hole_peck") ||
        operation.strategyId == QStringLiteral("hole_deephole")) {
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
    return QString();
}

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
    return QString();
}

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

QString holeKey(const HoleFeature &hole)
{
    return QStringLiteral("%1|%2|%3")
        .arg(hole.center.x(), 0, 'f', 3)
        .arg(hole.center.y(), 0, 'f', 3)
        .arg(hole.center.z(), 0, 'f', 3);
}

QString holeBatchKey(const HoleFeature &hole)
{
    const QVector3D axis = hole.axis.normalized();
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11")
        .arg(static_cast<int>(hole.kind))
        .arg(hole.subType)
        .arg(hole.radius, 0, 'f', 4)
        .arg(hole.depth, 0, 'f', 4)
        .arg(hole.secondaryRadius, 0, 'f', 4)
        .arg(hole.pitch, 0, 'f', 4)
        .arg(static_cast<int>(hole.region))
        .arg(std::abs(axis.x()), 0, 'f', 4)
        .arg(std::abs(axis.y()), 0, 'f', 4)
        .arg(std::abs(axis.z()), 0, 'f', 4)
        .arg(hole.center.z(), 0, 'f', 4);
}

bool sharesHoleBatch(const MachiningOperation &first,
                     const MachiningOperation &candidate)
{
    if (first.strategyId == QStringLiteral("hole_spot")) {
        return true;
    }
    return holeBatchKey(first.holeFeature) == holeBatchKey(candidate.holeFeature);
}

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
    lines << gcode.trimmed();
    return lines.join(QLatin1Char('\n'));
}

QString internalHoleCycleError(const QString &gcode)
{
    static const QRegularExpression codePattern(
        QStringLiteral("(?:^|\\s)code=([^\\s]+)"),
        QRegularExpression::CaseInsensitiveOption);
    const QSet<QString> supportedCodes{
        QStringLiteral("G81"),
        QStringLiteral("G82"),
        QStringLiteral("G83"),
        QStringLiteral("G84"),
        QStringLiteral("G85")};

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
    }
    return QString();
}

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

QString sha256(const QString &content)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256).toHex())
        .toUpper();
}

} // namespace

ProgramGenerationService::ProgramGenerationService(StrategyLookup strategyLookup,
                                                   ToolLookup toolLookup)
    : m_strategyLookup(std::move(strategyLookup)),
      m_toolLookup(std::move(toolLookup))
{
}

ProgramGenerationResult ProgramGenerationService::generate(
    const QList<MachiningOperation> &operations,
    const PostProcessorBase &postProcessor,
    const PostProcessorOptions &options,
    const ProgramGenerationSnapshotOptions &snapshotOptions) const
{
    ProgramGenerationResult output;
    if (operations.isEmpty()) {
        output.errors << QStringLiteral("No confirmed operations were provided.");
        return output;
    }

    for (int index = 0; index < operations.size(); ++index) {
        if (operations[index].id.trimmed().isEmpty()) {
            output.errors << QStringLiteral("Operation %1 is not a confirmed operation.")
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
            const QVector3D slotAxis = operations[index].contourFeature.axis.normalized();
            if (slotAxis.lengthSquared() > 1.0e-8f && std::abs(slotAxis.z()) < 0.65f) {
                output.errors << QStringLiteral(
                    "Operation %1: slot axis is not aligned with the front-face Z workflow; a transformed Setup is required before G-code generation.")
                                     .arg(index + 1);
            } else if (operations[index].contourFeature.region == FaceRegion::Side) {
                output.errors << QStringLiteral(
                    "Operation %1: Side-face slot requires a dedicated Setup before G-code generation.")
                                     .arg(index + 1);
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
                output.errors << QStringLiteral(
                    "Operation %1: Side-face pocket requires a dedicated Setup before G-code generation.")
                                     .arg(index + 1);
            } else if (operations[index].contourFeature.region == FaceRegion::Back) {
                output.errors << QStringLiteral(
                    "Operation %1: Back-face pocket requires a transformed Setup before G-code generation.")
                                     .arg(index + 1);
            }
        }
    }
    if (!output.errors.isEmpty()) {
        return output;
    }

    QStringList blocks;
    QList<ParametricToolpathProgram> parametricPrograms;
    QSet<QString> holesWithDeepCycle;
    QSet<QString> holesWithCircularMill;
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

        const ToolEntry tool = m_toolLookup ? m_toolLookup(operation.toolId) : ToolEntry();
        if (tool.id <= 0 || tool.diameter <= 0.0) {
            output.errors << QStringLiteral("Operation %1: tool T%2 is invalid or missing.")
                                     .arg(index + 1)
                                     .arg(operation.toolId);
            ++index;
            continue;
        }

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
                    blocks << operationBlock(index + 1,
                                             operation,
                                             batchOperationIds,
                                             result.gcode);
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

    QStringList sourceOperationIds;
    for (const MachiningOperation &operation : operations) {
        if (!operation.id.trimmed().isEmpty()) {
            sourceOperationIds << operation.id;
        }
    }

    QString finalGCode = expandedGCode;
    if (postProcessor.id() == QStringLiteral("cq8") && !parametricPrograms.isEmpty()) {
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

    ProgramEntry snapshot;
    snapshot.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    snapshot.createdAt = QDateTime::currentDateTime();
    snapshot.name = snapshotOptions.name;
    snapshot.postProcessorId = postProcessor.id();
    snapshot.sourceOperationIds = sourceOperationIds;
    snapshot.sourceOperationFingerprint =
        ProgramSnapshotFingerprint::calculate(operations, sourceOperationIds);
    snapshot.machineProfileId = snapshotOptions.machineProfile.id;
    snapshot.machineProfileVersion = snapshotOptions.machineProfile.version;
    snapshot.safeStartBlocks = resolvedSafeStartBlocks(options);
    snapshot.sourceSummary = snapshotOptions.sourceSummary;
    snapshot.gcodeText = finalGCode;
    snapshot.expandedGcodeText = finalGCode == expandedGCode ? QString() : expandedGCode;
    if (!cq8MacroProgram.libraryText.isEmpty()) {
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

    output.snapshot = snapshot;
    output.ok = true;
    return output;
}
