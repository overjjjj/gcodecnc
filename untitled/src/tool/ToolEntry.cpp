#include "ToolEntry.h"

QString ToolEntry::toDisplayString() const
{
    return QStringLiteral("T%1 %2 直径%3").arg(id).arg(name).arg(diameter, 0, 'f', 1);
}
