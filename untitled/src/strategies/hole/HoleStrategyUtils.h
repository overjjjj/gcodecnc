#pragma once

#include "../../import/StepImporter.h"
#include "../../tool/ToolEntry.h"
#include "../../tool/ChamferToolGeometry.h"
#include "../../core/StrategyParams.h"
#include <QString>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <limits>

struct HoleZRange {
    double entryZ = 0.0;
    double retractZ = 0.0;
    double bottomZ = 0.0;
    double direction = -1.0;
};

inline QString holeSpindleOutputError(const QString &strategyId, const StrategyParams &params)
{
    if (strategyId != "hole_peck" && strategyId != "hole_reaming" &&
        strategyId != "hole_spot" && strategyId != "hole_g81" &&
        strategyId != "hole_deephole" && strategyId != "hole_g73" &&
        strategyId != "hole_g76" && strategyId != "hole_g86" &&
        strategyId != "hole_tapping" && strategyId != "hole_chamfer" &&
        strategyId != "hole_circular_mill") return {};
    // Omitted speeds use each strategy's positive integer default.
    if (!params.values.contains("spindleSpeed")) return {};
    bool numeric = false;
    const double speed = params.values.value("spindleSpeed").toDouble(&numeric);
    if (!numeric || !std::isfinite(speed) || speed < 1.0 || speed > std::numeric_limits<int>::max())
        return QStringLiteral("主轴转速必须能输出为正整数 S 值（至少 1 rpm），不能截断为 S0。");
    return {};
}

inline QString holeAxisOutputError(const HoleFeature &feature, double machiningDepth = 0.0)
{
    if (feature.coordinatePrecisionVersion != 1)
        return QStringLiteral("孔中心缺少当前精度证据，请从重新导入的模型中重新选择孔特征。");
    const double x = feature.axis.x(), y = feature.axis.y(), z = feature.axis.z();
    const double length = std::hypot(std::hypot(x, y), z);
    const double depth = std::max(std::abs(feature.depth), std::abs(machiningDepth));
    // Accept numerical rotation noise only: <=1e-6 rad and <=0.0001 mm drift.
    if (!std::isfinite(length) || length <= 0.0 || !std::isfinite(feature.depth) ||
        !std::isfinite(machiningDepth) || !std::isfinite(depth) ||
        std::hypot(x, y) > length * 1.0e-6 ||
        std::hypot(x, y) / length * depth > 1.0e-4) {
        return QStringLiteral("当前孔工艺只支持与Z轴平行的孔，请先调整整个模型的装夹方向。");
    }
    if (feature.region != FaceRegion::Front) {
        return QStringLiteral("当前孔工艺需要已识别的正面孔，请确认加工侧并调整整个模型的装夹方向。");
    }
    return {};
}

inline HoleZRange holeZRange(const HoleFeature &feature, double cutDepth, double feedHeight)
{
    HoleZRange range;
    const double machiningDepth = std::max(cutDepth, 0.0);
    const double holeDepth = feature.depth > 0.0 ? feature.depth : machiningDepth;
    const double halfHoleDepth = holeDepth * 0.5;

    // The current UI/G-code pipeline is a single Z setup. Back-face holes are
    // recognized for grouping, but fixture rotation is not applied yet, so the
    // generated toolpath must still drill from the current top toward negative Z.
    range.direction = -1.0;
    range.entryZ = feature.center.z() - range.direction * halfHoleDepth;
    range.retractZ = range.entryZ - range.direction * feedHeight;
    range.bottomZ = range.entryZ + range.direction * machiningDepth;
    return range;
}

// Returns the effective drill depth for a feature, adapting for hole subType.
// Through holes drill 1 mm past the bottom face to ensure clean breakthrough.
// Tapped holes and countersunk holes use the feature depth unchanged.
inline double effectiveDrillDepth(const HoleFeature &feature, double paramDepth,
                                  bool explicitDepth);

inline double effectiveDrillDepth(const HoleFeature &feature, double paramDepth)
{
    return effectiveDrillDepth(feature, paramDepth, false);
}

// An explicitly confirmed depth is authoritative.  Breakthrough is only
// inferred when the operation has no explicit depth value.
inline double effectiveDrillDepth(const HoleFeature &feature, double paramDepth,
                                  bool explicitDepth)
{
    const double base = explicitDepth ? paramDepth
                                      : (feature.depth > 0.0 ? feature.depth : paramDepth);
    if (feature.subType == QStringLiteral("through_hole") ||
        feature.subType == QStringLiteral("countersunk_through_hole")) {
        if (explicitDepth) return base;
        return base + 1.0; // breakthrough allowance
    }
    return base;
}

// G73 treats an explicitly entered depth as the operator's final target.  The
// model depth is only used when the operation has no explicit depth value.
// This keeps the existing peck/deep-hole semantics unchanged while preventing
// a shallow G73 request from being replaced by the recognized model depth.
inline double effectiveG73DrillDepth(const HoleFeature &feature,
                                     double requestedDepth,
                                     bool explicitDepth)
{
    if (!std::isfinite(requestedDepth) || requestedDepth <= 0.0) {
        return requestedDepth;
    }
    if (explicitDepth) {
        return requestedDepth;
    }
    return effectiveDrillDepth(feature, requestedDepth);
}

// Keep tool reach checks consistent with the depth consumed by each hole strategy.
inline double holeCuttingDepth(const QString &strategyId, const HoleFeature &feature,
                              const StrategyParams &params, const ToolEntry &tool = {})
{
    if (strategyId == QStringLiteral("hole_spot") ||
        strategyId == QStringLiteral("hole_g81")) return params.get("depth", 2.0);
    if (strategyId == QStringLiteral("hole_peck"))
        return effectiveDrillDepth(feature, params.get("depth", 20.0),
                                   params.values.contains(QStringLiteral("depth")));
    if (strategyId == QStringLiteral("hole_deephole"))
        return effectiveDrillDepth(feature, params.get("depth", 50.0),
                                   params.values.contains(QStringLiteral("depth")));
    if (strategyId == QStringLiteral("hole_g73"))
        return effectiveG73DrillDepth(
            feature,
            params.get("depth", feature.depth),
            params.values.contains(QStringLiteral("depth")));
    if (strategyId == QStringLiteral("hole_circular_mill"))
        return (params.values.contains(QStringLiteral("depth"))
                    ? params.get("depth", 20.0) : feature.depth)
            + params.get("bottomAllowance", 0.0);
    if (strategyId == QStringLiteral("hole_chamfer")) {
        const double mode = params.get("chamferMode", 0.0);
        if (std::isfinite(mode) && std::abs(mode - 1.0) <= 1.0e-9)
            return params.get("axialDepth", 1.0);
        const double height = chamferConeHeight(tool);
        const double width = params.get("chamferWidth", 1.0);
        const double angle = params.get("chamferAngle", 45.0);
        if (mode != 0.0 || !std::isfinite(height) || !std::isfinite(width) || width <= 0 ||
            !std::isfinite(angle) || std::abs(angle * 2 - tool.extra.value("angle").toDouble()) > 1.0e-9 ||
            width >= (tool.diameter - tool.extra.value("tipDiameter").toDouble()) * 0.5 ||
            !std::isfinite(feature.radius) || tool.diameter * 0.5 >= feature.radius ||
            !std::isfinite(feature.depth) || height >= feature.depth ||
            (feature.subType != "through_hole" && feature.subType != "blind_hole"))
            return std::numeric_limits<double>::quiet_NaN();
        return height;
    }
    if (strategyId == QStringLiteral("hole_tapping"))
        return params.values.contains(QStringLiteral("depth"))
            ? params.get("depth", 15.0) : (feature.depth > 0.0 ? feature.depth : 15.0);
    if (strategyId == QStringLiteral("hole_reaming"))
        return params.values.contains(QStringLiteral("depth"))
            ? params.get("depth", 20.0) : (feature.depth > 0.0 ? feature.depth : 20.0);
    if (strategyId == QStringLiteral("hole_g76") || strategyId == QStringLiteral("hole_g86"))
        return params.values.contains(QStringLiteral("depth"))
            ? params.get("depth", 20.0) : (feature.depth > 0.0 ? feature.depth : 20.0);
    return feature.depth;
}

inline QString holeAxisOutputError(const QVector<HoleFeature> &features,
                                   const QString &strategyId, const StrategyParams &params,
                                   const ToolEntry &tool = {})
{
    for (const auto &feature : features) {
        const double depth = holeCuttingDepth(strategyId, feature, params, tool);
        if (strategyId == "hole_chamfer" && !std::isfinite(depth))
            return QStringLiteral("圆周倒角需已确认的直锥夹角、刀尖直径和足够刃长；角度须匹配，宽度小于锥段径向宽度，刀径小于普通孔径，锥高小于孔深。");
        const QString error = holeAxisOutputError(feature, depth);
        if (!error.isEmpty()) return error;
    }
    return {};
}

// Returns the recommended peck depth for a feature.
// Deep holes (depth/diameter > 3) use smaller pecks for better chip evacuation.
inline double effectivePeckDepth(const HoleFeature &feature, double paramPeck)
{
    if (feature.radius <= 0.0) return paramPeck;
    const double depthDiameterRatio = feature.depth / (feature.radius * 2.0);
    if (depthDiameterRatio > 5.0)
        return std::min(paramPeck, feature.radius * 0.5); // very deep: peck <= 0.5*D
    if (depthDiameterRatio > 3.0)
        return std::min(paramPeck, feature.radius);       // deep: peck <= 1*D
    return paramPeck;
}

inline double peckDepthForParameters(const HoleFeature &feature, const StrategyParams &params)
{
    auto cuttingFeature = feature;
    cuttingFeature.depth = holeCuttingDepth("hole_peck", feature, params);
    return effectivePeckDepth(cuttingFeature, params.get("peckDepth", 3.0));
}

inline QString peckDepthSummary(const HoleFeature &feature, const StrategyParams &params)
{
    return QStringLiteral("Q上限 %1 mm → 有效Q %2 mm（按加工深度 %3 mm，保留深孔保护）")
        .arg(params.get("peckDepth", 3.0), 0, 'f', 3)
        .arg(peckDepthForParameters(feature, params), 0, 'f', 3)
        .arg(holeCuttingDepth("hole_peck", feature, params), 0, 'f', 3);
}

inline double holeDiameter(const HoleFeature &feature)
{
    return feature.radius > 0.0 ? feature.radius * 2.0 : 0.0;
}

inline QString drillUndersizeComment(const HoleFeature &feature, const ToolEntry &tool)
{
    const double targetDia = holeDiameter(feature);
    if (targetDia <= 0.0 || tool.diameter <= 0.0) {
        return QString();
    }
    const double diff = targetDia - tool.diameter;
    if (diff > 0.5) {
        return QStringLiteral("; NOTE: Tool D%1 is smaller than target hole D%2. This operation drills a pilot/rough hole only.\n")
            .arg(tool.diameter, 0, 'f', 3)
            .arg(targetDia, 0, 'f', 3);
    }
    if (diff < -0.01) {
        return QStringLiteral("; WARNING: Tool D%1 is larger than target hole D%2.\n")
            .arg(tool.diameter, 0, 'f', 3)
            .arg(targetDia, 0, 'f', 3);
    }
    return QString();
}

inline QString validateReamerForHole(const HoleFeature &feature, const ToolEntry &tool)
{
    const double targetDia = holeDiameter(feature);
    if (targetDia <= 0.0 || tool.diameter <= 0.0) {
        return QStringLiteral("铰孔缺少有效孔径或刀具直径。");
    }
    const double diff = std::abs(targetDia - tool.diameter);
    if (diff > 0.05) {
        return QStringLiteral("铰孔刀具直径 D%1 与目标孔径 D%2 不匹配。铰孔应使用接近成品孔径的铰刀。")
            .arg(tool.diameter, 0, 'f', 3)
            .arg(targetDia, 0, 'f', 3);
    }
    return QString();
}

inline QString validateTapForHole(const HoleFeature &feature, const ToolEntry &tool)
{
    if (tool.type != QStringLiteral("tap")) {
        return QStringLiteral("攻丝工艺必须使用丝锥。");
    }
    const double targetDia = holeDiameter(feature);
    if (!std::isfinite(tool.pitch) || tool.pitch <= 0.0) {
        return QStringLiteral("攻丝刀具缺少螺距信息。");
    }
    if (!std::isfinite(targetDia) || targetDia <= 0.0 ||
        !std::isfinite(tool.diameter) || tool.diameter <= 0.0 ||
        std::abs(targetDia - tool.diameter) > 0.2) {
        return QStringLiteral("攻丝刀具 D%1 与识别螺纹孔 D%2 不匹配。")
            .arg(tool.diameter, 0, 'f', 3)
            .arg(targetDia, 0, 'f', 3);
    }
    if (feature.pitch > 0.0) {
        const double pitchTolerance = std::max(0.01, feature.pitch * 0.02);
        if (!std::isfinite(feature.pitch) || std::abs(tool.pitch - feature.pitch) > pitchTolerance) {
            return QStringLiteral("丝锥螺距 %1 与目标螺距 %2 不匹配。")
                .arg(tool.pitch, 0, 'f', 3).arg(feature.pitch, 0, 'f', 3);
        }
    }
    return QString();
}

// Reorder holes using a greedy nearest-neighbor heuristic starting from the
// first hole in the input list. Minimises total XY rapid travel distance.
inline QVector<HoleFeature> sortHolesByNearestNeighbor(QVector<HoleFeature> holes)
{
    if (holes.size() <= 1)
        return holes;

    QVector<HoleFeature> sorted;
    sorted.reserve(holes.size());
    sorted.append(holes.takeFirst());

    while (!holes.isEmpty()) {
        const Coordinate3D &last = sorted.last().center;
        double bestDist = std::numeric_limits<double>::max();
        int   bestIdx  = 0;

        for (int i = 0; i < holes.size(); ++i) {
            const Coordinate3D d = holes[i].center - last;
            const float dist  = d.x() * d.x() + d.y() * d.y(); // XY only
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx  = i;
            }
        }
        sorted.append(holes.takeAt(bestIdx));
    }
    return sorted;
}
