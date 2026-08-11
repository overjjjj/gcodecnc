#pragma once

#include "ParametricToolpathProgram.h"

struct Cq8MacroProgram
{
    bool ok = false;
    QString error;
    QString callText;
    QStringList callBlocks;
    QString libraryText;
};

// CQ8 phase-one function grammar: #variables, Oxxxx, M98 Pxxxx and M99.
// The expanded program remains the safety/simulation source of truth.
class Cq8MacroProgramBuilder
{
public:
    static Cq8MacroProgram build(const QList<ParametricToolpathProgram> &programs);
};
