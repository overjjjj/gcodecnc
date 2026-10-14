#pragma once

#include "../process_design/operations/MachiningOperation.h"

// 界面预检查；完整生成校验仍由 ProgramGenerationService 完成。
class ProgramVerificationWorkspaceFacade
{
public:
    static bool canGenerate(const MachiningOperation &operation)
    {
        return operation.enabled && operation.toolpathState == ToolpathState::Valid;
    }
};
