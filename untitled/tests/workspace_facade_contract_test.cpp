#include "../src/core/WorkspaceFacades.h"

#include <iostream>

int main()
{
    OperationProposal proposal;
    proposal.strategyId = QStringLiteral("contour");
    proposal.toolId = 1;
    if (!ProcessDesignWorkspaceFacade::candidateCanBeConfirmed(proposal)) {
        std::cerr << "valid candidate rejected\n";
        return 1;
    }

    MachiningOperation operation;
    operation.enabled = true;
    operation.toolpathState = ToolpathState::Valid;
    if (!ProgramVerificationWorkspaceFacade::canGenerate(operation)) {
        std::cerr << "valid operation rejected\n";
        return 1;
    }
    operation.toolpathState = ToolpathState::Stale;
    if (ProgramVerificationWorkspaceFacade::canGenerate(operation)) {
        std::cerr << "stale operation accepted\n";
        return 1;
    }

    ProgramEntry program;
    program.gcodeText = QStringLiteral("G90");
    program.sourceOperationIds = QStringList{QStringLiteral("op-1")};
    if (!MachineOperationWorkspaceFacade::canSend(program)) {
        std::cerr << "valid program rejected\n";
        return 1;
    }
    return 0;
}
