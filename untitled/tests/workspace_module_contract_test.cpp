#include "../src/workspaces/shared/WorkspaceModule.h"

#include <iostream>

int main()
{
    using namespace WorkspaceArchitecture;
    if (workspaceFor(WorkspaceCapability::Selection) != WorkspaceId::ProcessDesign ||
        workspaceFor(WorkspaceCapability::ProgramGeneration) != WorkspaceId::ProgramVerification ||
        workspaceFor(WorkspaceCapability::MachineCommunication) != WorkspaceId::MachineOperation) {
        std::cerr << "capability ownership is incorrect\n";
        return 1;
    }

    if (!canDependOn(WorkspaceId::ProcessDesign, WorkspaceId::Shared) ||
        !canDependOn(WorkspaceId::ProgramVerification, WorkspaceId::ProcessDesign) ||
        canDependOn(WorkspaceId::ProcessDesign, WorkspaceId::MachineOperation)) {
        std::cerr << "workspace dependency direction is incorrect\n";
        return 1;
    }
    return 0;
}
