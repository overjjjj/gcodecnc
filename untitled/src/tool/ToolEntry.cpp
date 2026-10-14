#include "ToolEntry.h"

// 生成包含刀号、名称和一位小数刀径的界面显示文本。
// 生成刀具编号、名称和直径的界面显示文本。
QString ToolEntry::toDisplayString() const
{
    return QStringLiteral("T%1 %2 直径%3").arg(id).arg(name).arg(diameter, 0, 'f', 1);
}
