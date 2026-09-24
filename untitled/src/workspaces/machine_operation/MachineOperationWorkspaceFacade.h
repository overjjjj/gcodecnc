#pragma once

#include "../../core/ProjectManager.h"

// 仅检查来源是否存在，不代表已完成程序验证或控制器接收确认。
class MachineOperationWorkspaceFacade
{
public:
    static bool canSend(const ProgramEntry &program)
    {
        return !program.gcodeText.trimmed().isEmpty() &&
               !program.sourceOperationIds.isEmpty();
    }
};
