#include "ToolOperationCompatibility.h"
#include "../../../strategies/hole/HoleStrategyUtils.h"

#include <algorithm>
#include <cmath>

namespace {

// 函数说明：expectedToolType负责当前模块的核心处理。
QString expectedToolType(const QString &strategyId)
{
    if (strategyId == QStringLiteral("hole_spot")) return QStringLiteral("spot_drill");
    if (strategyId == QStringLiteral("hole_tapping")) return QStringLiteral("tap");
    if (strategyId == QStringLiteral("hole_reaming")) return QStringLiteral("reamer");
    if (strategyId == QStringLiteral("hole_chamfer")) return QStringLiteral("chamfer_mill");
    if (strategyId == QStringLiteral("hole_g76") || strategyId == QStringLiteral("hole_g86"))
        return QStringLiteral("boring_bar");
    if (strategyId.startsWith(QStringLiteral("mill_"))
        || strategyId == QStringLiteral("hole_circular_mill")) {
        return QStringLiteral("end_mill");
    }
    return QStringLiteral("drill");
}

// 函数说明：addIssue负责当前模块的核心处理。
void addIssue(ToolCompatibilityReport &report,
              const QString &code,
              ToolCompatibilitySeverity severity,
              const QString &chinese,
              const QString &english,
              bool useChinese)
{
    report.issues.append({code, severity, useChinese ? chinese : english});
}

// 函数说明：effectiveDepth负责当前模块的核心处理。
double effectiveDepth(double featureDepth, double requestedDepth)
{
    return !std::isfinite(requestedDepth) || requestedDepth >= 0.0 ? requestedDepth : featureDepth;
}

// 函数说明：reviewCommon负责当前模块的核心处理。
void reviewCommon(ToolCompatibilityReport &report,
                  const QString &strategyId,
                  const ToolEntry &tool,
                  double depth,
                  bool chinese)
{
    if (tool.id <= 0 || tool.diameter <= 0.0) {
        addIssue(report, QStringLiteral("invalid_tool"), ToolCompatibilitySeverity::Blocking,
                 QStringLiteral("刀具编号或直径无效。"),
                 QStringLiteral("The tool number or diameter is invalid."), chinese);
        return;
    }

    const QString expected = expectedToolType(strategyId);
    if (tool.type != expected) {
        addIssue(report, QStringLiteral("tool_type"), ToolCompatibilitySeverity::Blocking,
                 QStringLiteral("刀具类型不匹配：当前为 %1，工艺要求 %2。").arg(tool.type, expected),
                 QStringLiteral("Tool type mismatch: selected %1, strategy requires %2.").arg(tool.type, expected),
                 chinese);
    }

    if (depth <= 0.0) {
        return;
    }
    if (tool.fluteLen <= 0.0) {
        addIssue(report, QStringLiteral("missing_flute_length"), ToolCompatibilitySeverity::Warning,
                 QStringLiteral("刀具未填写有效刃长，无法自动核对加工深度。"),
                 QStringLiteral("Flute length is missing, so machining depth cannot be verified automatically."),
                 chinese);
    } else if (depth > tool.fluteLen + 0.01) {
        addIssue(report, QStringLiteral("flute_too_short"), ToolCompatibilitySeverity::Blocking,
                 QStringLiteral("加工深度 %1 mm 超过有效刃长 %2 mm。").arg(depth, 0, 'f', 2).arg(tool.fluteLen, 0, 'f', 2),
                 QStringLiteral("Machining depth %1 mm exceeds flute length %2 mm.").arg(depth, 0, 'f', 2).arg(tool.fluteLen, 0, 'f', 2),
                 chinese);
    }
    if (tool.totalLen > 0.0 && depth >= tool.totalLen - 0.01) {
        addIssue(report, QStringLiteral("tool_length"), ToolCompatibilitySeverity::Blocking,
                 QStringLiteral("加工深度达到或超过刀具总长，无法形成安全伸出。"),
                 QStringLiteral("Machining depth reaches or exceeds total tool length; safe stick-out is impossible."),
                 chinese);
    }
}

} // namespace

// 函数说明：hasBlockingIssues负责当前模块的核心处理。
bool ToolCompatibilityReport::hasBlockingIssues() const
{
    // 函数说明：any_of用于完成当前模块的对应处理。
    return std::any_of(issues.cbegin(), issues.cend(), [](const ToolCompatibilityIssue &issue) {
        return issue.severity == ToolCompatibilitySeverity::Blocking;
    });
}

// 函数说明：hasWarnings负责当前模块的核心处理。
bool ToolCompatibilityReport::hasWarnings() const
{
    // 函数说明：any_of用于完成当前模块的对应处理。
    return std::any_of(issues.cbegin(), issues.cend(), [](const ToolCompatibilityIssue &issue) {
        return issue.severity == ToolCompatibilitySeverity::Warning;
    });
}

// 函数说明：reviewToolCompatibility负责当前模块的核心处理。
ToolCompatibilityReport reviewToolCompatibility(const QString &strategyId,
                                                const ToolEntry &tool,
                                                const HoleFeature &feature,
                                                bool chinese,
                                                double requestedDepth)
{
    ToolCompatibilityReport report;
    if (strategyId == "hole_chamfer" && !std::isfinite(requestedDepth)) {
        addIssue(report, "chamfer_geometry", ToolCompatibilitySeverity::Blocking,
                 QStringLiteral("倒角刀几何未确认或与孔口参数不匹配，请检查夹角、刀尖直径、刃长、宽度及孔深。"),
                 QStringLiteral("Confirm cone angle, tip diameter and length, then check chamfer width, angle and bore depth."), chinese);
        return report;
    }
    if (strategyId.startsWith(QStringLiteral("hole_"))) {
        const QString axisError = holeAxisOutputError(feature, effectiveDepth(feature.depth, requestedDepth));
        if (!axisError.isEmpty()) {
            addIssue(report, QStringLiteral("hole_axis"), ToolCompatibilitySeverity::Blocking,
                     axisError,
                     QStringLiteral("This setup requires a front-side hole parallel to Z. Reorient the entire model first."), chinese);
        }
    }
    const double depth = effectiveDepth(feature.depth, requestedDepth);
    reviewCommon(report, strategyId, tool, depth, chinese);
    if (tool.id <= 0 || tool.diameter <= 0.0 || feature.radius <= 0.0) {
        return report;
    }

    const double targetDiameter = feature.radius * 2.0;
    const double tolerance = std::max(0.05, targetDiameter * 0.01);
    if (strategyId == QStringLiteral("hole_g81")
        || strategyId == QStringLiteral("hole_peck")
        || strategyId == QStringLiteral("hole_deephole")
        || strategyId == QStringLiteral("hole_g73")) {
        if (tool.diameter > targetDiameter + tolerance) {
            addIssue(report, QStringLiteral("oversize_drill"), ToolCompatibilitySeverity::Blocking,
                     QStringLiteral("钻头直径 %1 mm 大于目标孔径 %2 mm。").arg(tool.diameter, 0, 'f', 2).arg(targetDiameter, 0, 'f', 2),
                     QStringLiteral("Drill diameter %1 mm exceeds target hole %2 mm.").arg(tool.diameter, 0, 'f', 2).arg(targetDiameter, 0, 'f', 2),
                     chinese);
        } else if (tool.diameter < targetDiameter - tolerance) {
            addIssue(report, QStringLiteral("undersize_drill"), ToolCompatibilitySeverity::Warning,
                     QStringLiteral("钻头比目标孔小，本工序只会形成底孔或粗孔，需确认后续扩孔/铰孔/铣孔工序。"),
                     QStringLiteral("The drill is smaller than the target; this creates only a pilot/rough hole. Confirm a later boring, reaming, or milling operation."),
                     chinese);
        }
    } else if (strategyId == QStringLiteral("hole_circular_mill")
               && tool.diameter >= targetDiameter - tolerance) {
        addIssue(report, QStringLiteral("circular_mill_diameter"), ToolCompatibilitySeverity::Blocking,
                 QStringLiteral("圆插补刀具必须明显小于目标孔径。"),
                 QStringLiteral("A circular-milling tool must be clearly smaller than the target hole."), chinese);
    } else if (strategyId == QStringLiteral("hole_reaming")
               // 函数说明：abs用于完成当前模块的对应处理。
               && std::abs(tool.diameter - targetDiameter) > tolerance) {
        addIssue(report, QStringLiteral("reamer_diameter"), ToolCompatibilitySeverity::Blocking,
                 QStringLiteral("铰刀直径与目标孔径不匹配。"),
                 QStringLiteral("Reamer diameter does not match the target hole."), chinese);
    }

    if (strategyId == QStringLiteral("hole_tapping")) {
        const QString tapError = validateTapForHole(feature, tool);
        if (!tapError.isEmpty()) {
            addIssue(report, QStringLiteral("tap_geometry"), ToolCompatibilitySeverity::Blocking,
                     tapError,
                     QStringLiteral("The tap diameter and pitch must match the recognized hole; pitch must be positive."),
                     chinese);
        }
    }

    return report;
}

// 函数说明：reviewToolCompatibility负责当前模块的核心处理。
ToolCompatibilityReport reviewToolCompatibility(const QString &strategyId,
                                                const ToolEntry &tool,
                                                const ContourFeature &feature,
                                                bool chinese,
                                                double requestedDepth)
{
    ToolCompatibilityReport report;
    const double depth = effectiveDepth(feature.depth, requestedDepth);
    reviewCommon(report, strategyId, tool, depth, chinese);
    if (tool.id <= 0 || tool.diameter <= 0.0) {
        return report;
    }

    const bool slotStrategy = strategyId == QStringLiteral("mill_slot")
        || strategyId == QStringLiteral("mill_blind_slot")
        || strategyId == QStringLiteral("mill_tapered_slot");
    if (slotStrategy && feature.width > 0.0) {
        if (tool.diameter >= feature.width - 0.01) {
            addIssue(report, QStringLiteral("slot_diameter"), ToolCompatibilitySeverity::Blocking,
                     QStringLiteral("刀具直径必须小于槽宽。"),
                     QStringLiteral("Tool diameter must be smaller than slot width."), chinese);
        } else if (tool.diameter < feature.width * 0.25) {
            addIssue(report, QStringLiteral("slot_efficiency"), ToolCompatibilitySeverity::Warning,
                     QStringLiteral("刀具直径小于槽宽的 25%，走刀次数和加工时间可能明显增加。"),
                     QStringLiteral("Tool diameter is below 25% of slot width; pass count and machining time may increase significantly."),
                     chinese);
        }
    }

    if (strategyId == QStringLiteral("mill_pocket_rough") ||
        strategyId == QStringLiteral("mill_pocket_finish") ||
        strategyId == QStringLiteral("mill_pocket_floor_finish")) {
        const double limitingSize = feature.width > 0.0 && feature.length > 0.0
            ? std::min(feature.width, feature.length)
            : feature.radius * 2.0;
        if (limitingSize > 0.0 && tool.diameter >= limitingSize - 0.01) {
            addIssue(report, QStringLiteral("pocket_diameter"), ToolCompatibilitySeverity::Blocking,
                     QStringLiteral("刀具直径必须小于型腔最小尺寸。"),
                     QStringLiteral("Tool diameter must be smaller than the pocket's limiting dimension."), chinese);
        }
    }
    return report;
}
