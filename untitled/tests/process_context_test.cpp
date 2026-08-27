#include "../src/core/ProcessContext.h"
#include "../src/services/OperationFactory.h"

#include <QCoreApplication>

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

OperationProposal sampleProposal()
{
    OperationProposal proposal;
    proposal.kind = OperationProposalKind::Hole;
    proposal.strategyId = QStringLiteral("hole_spot");
    proposal.holeFeature.radius = 3.0;
    proposal.holeFeature.depth = 8.0;
    return proposal;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    ProcessContext context;
    context.toolId = 7;
    context.materialId = QStringLiteral("steel");
    context.toolTypeId = QStringLiteral("spot_drill");
    context.setup.workOffset = QStringLiteral("G56");
    context.setup.origin.confirmed = true;

    if (!expect(context.validate().isEmpty(),
                "a confirmed setup and selected tool should be valid")) {
        return 1;
    }

    OperationParameterLayers layers;
    const OperationFactoryResult created = OperationFactory::CreateConfirmed(
        sampleProposal(), layers, context);
    if (!expect(created.ok, "valid process context should create an operation") ||
        !expect(created.operation.toolId == 7,
                "the selected context tool should be copied to the operation") ||
        !expect(created.operation.params.get(QStringLiteral("workOffset")) == 56.0,
                "the confirmed setup WCS should be copied to operation parameters") ||
        !expect(created.operation.parameterSources.value(QStringLiteral("workOffset")) ==
                    ProcessParameterSource::ManualOverride,
                "the setup WCS should retain operator provenance")) {
        return 1;
    }

    context.setup.workOffset = QStringLiteral("G60");
    const OperationFactoryResult invalid_wcs = OperationFactory::CreateConfirmed(
        sampleProposal(), layers, context);
    if (!expect(!invalid_wcs.ok, "unsupported work offsets must block creation")) {
        return 1;
    }

    context.setup.workOffset = QStringLiteral("G56");
    context.setup.origin.confirmed = false;
    const OperationFactoryResult unconfirmed = OperationFactory::CreateConfirmed(
        sampleProposal(), layers, context);
    if (!expect(!unconfirmed.ok, "unconfirmed setups must block operation creation")) {
        return 1;
    }

    return 0;
}
