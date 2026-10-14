#pragma once

#include "WorkspaceModule.h"

namespace WorkspaceArchitecture {

enum class WorkspaceAction {
    SelectGeometry,
    EditParameters,
    ConfirmOperation,
    GenerateProgram,
    SimulateProgram,
    SendProgram
};

inline WorkspaceId ownerOf(WorkspaceAction action)
{
    switch (action) {
    case WorkspaceAction::SelectGeometry:
    case WorkspaceAction::EditParameters:
    case WorkspaceAction::ConfirmOperation:
        return WorkspaceId::ProcessDesign;
    case WorkspaceAction::GenerateProgram:
    case WorkspaceAction::SimulateProgram:
        return WorkspaceId::ProgramVerification;
    case WorkspaceAction::SendProgram:
        return WorkspaceId::MachineOperation;
    }
    return WorkspaceId::Shared;
}

inline bool requiresConfirmedOperation(WorkspaceAction action)
{
    return action == WorkspaceAction::GenerateProgram ||
           action == WorkspaceAction::SimulateProgram ||
           action == WorkspaceAction::SendProgram;
}

} // namespace WorkspaceArchitecture
