#pragma once

#include <QString>

class GCodeModalOptimizer
{
public:
    // Removes only repeated G0/G1/G2/G3, F and S words.  It deliberately
    // leaves modal groups that affect safety or machining semantics intact.
    static QString optimize(const QString &gcode);
};
