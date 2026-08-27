#include "ContourMillingContract.h"

#include <QObject>

#include <cmath>

namespace {

ProcessParameterDefinition numberDefinition(const QString &id,
                                            ProcessParameterUnit unit,
                                            double defaultValue)
{
    ProcessParameterDefinition definition;
    definition.id = id;
    definition.type = ProcessParameterType::Number;
    definition.unit = unit;
    definition.defaultValue = defaultValue;
    definition.visible = true;
    return definition;
}

bool hasZeroLengthEdge(const QVector<QVector3D> &points, bool closedContour)
{
    for (int index = 1; index < points.size(); ++index) {
        if ((points.at(index) - points.at(index - 1)).lengthSquared() <= 1.0e-10f) {
            return true;
        }
    }
    return closedContour && points.size() > 1 &&
           (points.first() - points.last()).lengthSquared() <= 1.0e-10f;
}

} // namespace

ProcessParameterSchema contourMillingParameterSchema(bool closedContour)
{
    ProcessParameterSchema schema = ProcessParameterSchema::CommonOperation();
    schema.addDefinition(numberDefinition(
        QStringLiteral("stockToLeave"), ProcessParameterUnit::Millimeter, 0.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("depthStockToLeave"), ProcessParameterUnit::Millimeter, 0.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("compensation"), ProcessParameterUnit::None, 1.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("leadLength"), ProcessParameterUnit::Millimeter, 5.0));
    if (closedContour) {
        schema.addDefinition(numberDefinition(
            QStringLiteral("overcut"), ProcessParameterUnit::Millimeter, 0.0));
    }
    return schema;
}

StrategyParams contourMillingDefaultParams(bool closedContour)
{
    StrategyParams params = contourMillingParameterSchema(closedContour).defaultParams();
    params.set(QStringLiteral("safeHeight"), 50.0);
    params.set(QStringLiteral("plungeHeight"), 3.0);
    params.set(QStringLiteral("referenceHeight"), 0.0);
    params.set(QStringLiteral("depth"), 1.0);
    params.set(QStringLiteral("stepOver"), 1.0);
    params.set(QStringLiteral("stepDown"), 1.0);
    params.set(QStringLiteral("feedRate"), 1000.0);
    params.set(QStringLiteral("plungeRate"), 200.0);
    params.set(QStringLiteral("spindleSpeed"), 3000.0);
    params.set(QStringLiteral("feedHeight"), 3.0);
    return params;
}

double contourMillingEffectiveDepth(const ContourFeature &feature,
                                    const StrategyParams &params)
{
    return feature.depth - params.get(QStringLiteral("depthStockToLeave"), 0.0);
}

QString validateContourMillingContract(const ContourFeature &feature,
                                       const ToolEntry &tool,
                                       const StrategyParams &params,
                                       bool closedContour)
{
    const QStringList parameterErrors =
        contourMillingParameterSchema(closedContour).validate(params);
    if (!parameterErrors.isEmpty()) {
        return parameterErrors.join(QLatin1Char('\n'));
    }
    if (tool.id <= 0 || tool.diameter <= 0.0) {
        return QObject::tr("轮廓刀具编号或直径无效。");
    }
    const int minimumPointCount = closedContour ? 3 : 2;
    if (feature.points.size() < minimumPointCount) {
        return closedContour ? QObject::tr("封闭轮廓至少需要三个点。")
                             : QObject::tr("开放轮廓至少需要两个点。");
    }
    if (hasZeroLengthEdge(feature.points, closedContour)) {
        return QObject::tr("轮廓包含零长度边，无法确定刀补方向。");
    }
    const double feedHeight = params.get(QStringLiteral("feedHeight"), 3.0);
    const double safeHeight = params.get(QStringLiteral("safeHeight"), 50.0);
    const double feedPlane = feature.center.z() + feedHeight;
    if (feedHeight < 0.0 || safeHeight <= feedPlane) {
        return QObject::tr("安全高度必须高于轮廓进刀平面。");
    }
    const double planarStock = params.get(QStringLiteral("stockToLeave"), 0.0);
    const double depthStock = params.get(QStringLiteral("depthStockToLeave"), 0.0);
    const double compensation = params.get(QStringLiteral("compensation"), 1.0);
    if (planarStock < 0.0 || depthStock < 0.0 ||
        contourMillingEffectiveDepth(feature, params) <= 0.0) {
        return QObject::tr("轮廓平面余量或深度余量无效。");
    }
    if (std::abs(compensation) > 1.0e-9 && planarStock > 1.0e-9) {
        return QObject::tr("机床刀补模式尚不能验证平面余量，请改用 G40 CAM 刀心偏置。");
    }
    if (std::abs(compensation) > 1.0e-9 &&
        std::abs(std::abs(compensation) - 1.0) > 1.0e-9) {
        return QObject::tr("刀补参数仅支持 G40、G41 或 G42。");
    }
    if (params.get(QStringLiteral("leadLength"), 5.0) <= 0.0) {
        return QObject::tr("轮廓进退刀长度必须大于零。");
    }
    if (closedContour) {
        const double overcut = params.get(QStringLiteral("overcut"), 0.0);
        const double firstEdgeLength =
            double((feature.points.at(1) - feature.points.first()).length());
        if (overcut < 0.0 || overcut >= firstEdgeLength) {
            return QObject::tr("闭合过切必须小于起始边长度。");
        }
    }
    return QString();
}
