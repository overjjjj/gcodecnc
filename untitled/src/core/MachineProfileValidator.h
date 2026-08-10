#pragma once

#include "MachineProfile.h"

struct MachineProfileValidationResult
{
    bool ok = true;
    QStringList errors;
};

class MachineProfileValidator
{
public:
    static MachineProfileValidationResult validate(const MachineProfile &profile);
};
