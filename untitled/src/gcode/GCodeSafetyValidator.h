#pragma once

#include <QString>
#include <QStringList>

struct GCodeSafetyReport
{
    bool ok = true;
    QStringList messages;
};

class GCodeSafetyValidator
{
public:
    static GCodeSafetyReport validate(const QString &gcode);
};
