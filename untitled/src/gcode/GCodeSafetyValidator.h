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
    // 中文说明：对最终 G 代码执行离线安全门检查，检查模态、快速移动、循环收尾
    // 以及主轴/冷却关闭等约束；该函数不修复代码，只负责给出阻断原因。
    static GCodeSafetyReport validate(const QString &gcode);
};
