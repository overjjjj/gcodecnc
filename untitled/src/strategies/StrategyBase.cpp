#include "StrategyBase.h"

ProcessParameterSchema StrategyBase::parameterSchema() const
{
    return ProcessParameterSchema::CommonOperation();
}

QStringList StrategyBase::validate(const StrategyParams &params) const
{
    return parameterSchema().validate(params);
}
