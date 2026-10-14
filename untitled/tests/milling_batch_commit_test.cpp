#include "../src/app/MillingBatchCommit.h"
#include "../src/workspaces/process_design/operations/OperationProposal.h"
#include "../src/strategies/mill/OpenContourMillingStrategy.h"
#include <QCoreApplication>
#include <iostream>

static bool expect(bool value, const char *message)
{
    if (!value) std::cerr << message << '\n';
    return value;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    OpenContourMillingStrategy strategy;
    ToolEntry tool;
    tool.id = 1;
    tool.diameter = 6;
    OperationProposal proposal;
    proposal.kind = OperationProposalKind::Contour;
    proposal.strategyId = strategy.id();
    proposal.toolId = tool.id;
    proposal.params = strategy.defaultParams();
    proposal.contourFeature.depth = 2;
    proposal.contourFeature.radius = 10;
    proposal.contourFeature.points = {QVector3D(0, 0, 0), QVector3D(20, 0, 0)};
    if (!expect(strategy.generate(proposal.contourFeature, tool, proposal.params).ok,
                "original contour must be machinable to reproduce the regression")) return 1;
    const auto first = OperationFactory::confirm(proposal, OperationConfirmationIntent::ExplicitUser);
    proposal.contourFeature.points.resize(1);
    const auto second = OperationFactory::confirm(proposal, OperationConfirmationIntent::ExplicitUser);
    if (!expect(first.ok && second.ok, "factory should preserve final geometry for toolpath validation")) return 1;
    QList<MachiningOperation> committed{first.operation};
    int commits = 0;
    const auto commit = [&](const QList<MachiningOperation> &operations) {
        ++commits;
        committed.append(operations);
        QStringList ids;
        for (const auto &operation : operations) ids.append(operation.id);
        return ids;
    };
    const auto failed = validateAndCommitMillingBatch(
        {first.operation, second.operation}, strategy, tool, commit);
    if (!expect(!failed.ok && !failed.error.isEmpty() && failed.operationIds.isEmpty() &&
                commits == 0 && committed.size() == 1 &&
                committed.first().toolpathState == ToolpathState::Empty,
                "invalid final geometry must not mark or commit any part of the batch")) return 1;
    MachiningOperation changedParams = first.operation;
    changedParams.params.set("stepDown", 0);
    if (!expect(!validateAndCommitMillingBatch({changedParams}, strategy, tool, commit).ok && commits == 0,
                "final operation parameters must be used for validation")) return 1;
    MachiningOperation changedTool = first.operation;
    changedTool.toolId = 2;
    if (!expect(!validateAndCommitMillingBatch({changedTool}, strategy, tool, commit).ok && commits == 0,
                "a batch must not validate an operation using a different tool")) return 1;
    MachiningOperation changedStrategy = first.operation;
    changedStrategy.strategyId = QStringLiteral("mill_face");
    if (!expect(!validateAndCommitMillingBatch({changedStrategy}, strategy, tool, commit).ok && commits == 0,
                "a batch must not validate an operation using a different strategy")) return 1;
    MachiningOperation finalGeometry = first.operation;
    finalGeometry.id = QStringLiteral("final-geometry");
    finalGeometry.contourFeature.center.setZ(3);
    finalGeometry.contourFeature.points = {QVector3D(30, 5, 3), QVector3D(10, 5, 3)};
    const auto success = validateAndCommitMillingBatch(
        {first.operation, finalGeometry}, strategy, tool, commit);
    if (!expect(success.ok && commits == 1 && committed.size() == 3 &&
                success.operationIds == QStringList{first.operation.id, finalGeometry.id} &&
                committed[1].toolpathState == ToolpathState::Valid &&
                committed[2].toolpathState == ToolpathState::Valid &&
                committed[2].contourFeature.points == finalGeometry.contourFeature.points,
                "valid final batch must commit once in order and retain selected geometry")) return 1;
    if (!expect(!validateAndCommitMillingBatch({}, strategy, tool, commit).ok && commits == 1,
                "empty batch must not invoke commit")) return 1;
    return 0;
}
