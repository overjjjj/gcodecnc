#include "ProgramGenerationService.h"

#include "../gcode/GCodeSafetyValidator.h"
#include "../gcode/ProgramSnapshotFingerprint.h"
#include "../gcode/SiemensProgramPackage.h"
#include "../strategies/StrategyBase.h"

#include <QDateTime>
#include <QSet>
#include <QUuid>

#include <utility>

namespace {

bool isSlotMillingStrategy(const QString &strategyId)
{
    return strategyId == QStringLiteral("mill_slot") ||
           strategyId == QStringLiteral("mill_blind_slot") ||
           strategyId == QStringLiteral("mill_tapered_slot");
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
    }
    if (!output.errors.isEmpty()) {
        return output;
    }

    QStringList blocks;
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

        if (operation.opType == OperationType::Hole) {
            QVector<HoleFeature> batch;
            int next = index;
            while (next < operations.size() &&
                   operations[next].opType == OperationType::Hole &&
                   operations[next].strategyId == operation.strategyId &&
                   operations[next].toolId == operation.toolId &&
                   operations[next].params.values == operation.params.values) {
                batch.append(operations[next].holeFeature);
                ++next;
            }

            const ToolpathResult result = strategy->generate(batch, tool, operation.params);
            if (result.ok && !result.gcode.trimmed().isEmpty()) {
                QStringList batchOperationIds;
                for (int batchIndex = index; batchIndex < next; ++batchIndex) {
                    batchOperationIds << operations[batchIndex].id;
                }
                blocks << operationBlock(index + 1,
                                         operation,
                                         batchOperationIds,
                                         result.gcode);
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

    const QString finalGCode =
        postProcessor.wrapGCode(blocks.join(QLatin1Char('\n')).split(QLatin1Char('\n')),
                                options);
    const GCodeSafetyReport safetyReport = GCodeSafetyValidator::validate(finalGCode);
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
    snapshot.lineCount = finalGCode.count(QLatin1Char('\n')) + 1;

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
