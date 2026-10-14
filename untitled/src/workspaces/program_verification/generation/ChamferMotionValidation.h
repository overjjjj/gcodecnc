#pragma once

#include "../simulation/SimulationController.h"
#include <QMap>
#include <cmath>

// Replace only protected chamfer bodies with the independently selected built-in
// strategy. Parsing the whole program preserves the real incoming modal state.
inline QString chamferFinalMotionError(const QString &gcode,
                                      const QMap<QString, QString> &referenceBodies)
{
    if (referenceBodies.isEmpty()) return {};
    QString reference = gcode;
    for (auto it = referenceBodies.cbegin(); it != referenceBodies.cend(); ++it) {
        const QString begin = "; CNEXT_CHAMFER_BEGIN " + it.key();
        const QString end = "; CNEXT_CHAMFER_END " + it.key();
        if (reference.count(begin) != 1 || reference.count(end) != 1)
            return QStringLiteral("倒角最终路径缺少唯一的批次边界，无法验证。");
        const int start = reference.indexOf('\n', reference.indexOf(begin));
        const int finish = reference.indexOf(end);
        if (start < 0 || finish <= start)
            return QStringLiteral("倒角最终路径批次边界顺序无效。");
        reference.replace(start + 1, finish - start - 1,
                          QStringLiteral("G17 G90\n") + it.value() + '\n');
    }
    SimulationController actual, expected;
    actual.loadGCode(gcode, false);
    expected.loadGCode(reference, false);
    if (!actual.motionError().isEmpty() || !expected.motionError().isEmpty())
        return QStringLiteral("倒角最终程序含无法验证的运动：%1 %2")
            .arg(actual.motionError(), expected.motionError());
    const auto &a = actual.moves();
    const auto &e = expected.moves();
    if (a.size() != e.size())
        return QStringLiteral("倒角最终运动与已验证的锥面路径不一致（运动数量）。");
    auto same = [](const Coordinate3D &x, const Coordinate3D &y) {
        // Generated NC uses six decimal places; this is comparison precision,
        // not an allowance to enlarge the machining envelope.
        return std::abs(x.x()-y.x()) <= 1.0e-6 && std::abs(x.y()-y.y()) <= 1.0e-6
            && std::abs(x.z()-y.z()) <= 1.0e-6;
    };
    for (int i = 0; i < a.size(); ++i) {
        if (!same(a[i].from, e[i].from) || !same(a[i].to, e[i].to) ||
            a[i].fromKnownAxes != e[i].fromKnownAxes || a[i].toKnownAxes != e[i].toKnownAxes ||
            a[i].isRapid != e[i].isRapid || a[i].toolId != e[i].toolId ||
            std::abs(a[i].chordDeviation-e[i].chordDeviation) > 1.0e-9)
            return QStringLiteral("倒角最终 NC 第 %1 行偏离已验证的锥面路径。")
                .arg(a[i].sourceLine + 1);
    }
    return {};
}
