#pragma once

namespace WorkspaceArchitecture {

enum class WorkspaceId {
    Shared,
    ProcessDesign,
    ProgramVerification,
    MachineOperation
};

enum class WorkspaceCapability {
    Selection,
    OperationConfirmation,
    ProgramGeneration,
    ProgramSimulation,
    MachineCommunication
};

inline WorkspaceId workspaceFor(WorkspaceCapability capability)
{
    switch (capability) {
    case WorkspaceCapability::Selection:
    case WorkspaceCapability::OperationConfirmation:
        return WorkspaceId::ProcessDesign;
    case WorkspaceCapability::ProgramGeneration:
    case WorkspaceCapability::ProgramSimulation:
        return WorkspaceId::ProgramVerification;
    case WorkspaceCapability::MachineCommunication:
        return WorkspaceId::MachineOperation;
    }
    return WorkspaceId::Shared;
}

inline bool canDependOn(WorkspaceId consumer, WorkspaceId dependency)
{
    if (dependency == WorkspaceId::Shared || consumer == dependency) {
        return true;
    }
    if (consumer == WorkspaceId::ProgramVerification &&
        dependency == WorkspaceId::ProcessDesign) {
        return true;
    }
    return false;
}

} // namespace WorkspaceArchitecture
