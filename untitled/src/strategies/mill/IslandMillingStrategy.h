#pragma once

#include "AnnularMillingStrategy.h"

class IslandMillingStrategy : public AnnularMillingStrategy
{
public:
    QString id() const override { return QStringLiteral("mill_island"); }
    QString displayName() const override { return QObject::tr("铣孤岛"); }
};
