#pragma once

#include "../../strategies/OperationProposal.h"
#include "../../strategies/MachiningOperation.h"
#include "../../core/ProjectManager.h"

class ProcessDesignWorkspaceFacade
{
public:
    static bool candidateCanBeConfirmed(const OperationProposal &proposal)
    {
        return !proposal.strategyId.trimmed().isEmpty() && proposal.toolId >= 0;
    }
};

class ProgramVerificationWorkspaceFacade
{
public:
    static bool canGenerate(const MachiningOperation &operation)
    {
        return operation.enabled && operation.toolpathState == ToolpathState::Valid;
    }
};

class MachineOperationWorkspaceFacade
{
public:
    static bool canSend(const ProgramEntry &program)
    {
        return !program.gcodeText.trimmed().isEmpty() &&
               !program.sourceOperationIds.isEmpty();
    }
};
