#include "../src/core/WorkspaceContracts.h"

#include <iostream>

int main()
{
    using namespace WorkspaceArchitecture;
    if (ownerOf(WorkspaceAction::SelectGeometry) != WorkspaceId::ProcessDesign ||
        ownerOf(WorkspaceAction::GenerateProgram) != WorkspaceId::ProgramVerification ||
        ownerOf(WorkspaceAction::SendProgram) != WorkspaceId::MachineOperation) {
        std::cerr << "action owner mismatch\n";
        return 1;
    }
    if (!requiresConfirmedOperation(WorkspaceAction::GenerateProgram) ||
        !requiresConfirmedOperation(WorkspaceAction::SendProgram) ||
        requiresConfirmedOperation(WorkspaceAction::SelectGeometry)) {
        std::cerr << "confirmation gate mismatch\n";
        return 1;
    }
    return 0;
}
