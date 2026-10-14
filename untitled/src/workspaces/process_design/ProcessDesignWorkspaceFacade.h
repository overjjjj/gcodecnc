#pragma once

#include "operations/OperationProposal.h"

// 界面预检查；正式确认仍由 OperationFactory 完成。
class ProcessDesignWorkspaceFacade
{
public:
    static bool candidateCanBeConfirmed(const OperationProposal &proposal)
    {
        return !proposal.strategyId.trimmed().isEmpty() && proposal.toolId >= 0;
    }
};
