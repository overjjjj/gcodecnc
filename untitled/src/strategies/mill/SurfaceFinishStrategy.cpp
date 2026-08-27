#include "SurfaceFinishStrategy.h"
#include <QObject>
#include <algorithm>
#include <cmath>

namespace {

constexpr double kTolerance = 0.02;

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

bool nearValue(double first, double second)
{
    return std::abs(first - second) <= kTolerance;
}

bool verifiedRectangle(const ContourFeature &feature,
                       double *minimumX,
                       double *maximumX,
                       double *minimumY,
                       double *maximumY)
{
    if (feature.points.size() != 4 || !feature.islands.isEmpty() ||
        std::abs(feature.axis.normalized().z()) < 0.999) {
        return false;
    }
    *minimumX = *maximumX = feature.points.first().x();
    *minimumY = *maximumY = feature.points.first().y();
    for (const QVector3D &point : feature.points) {
        if (!nearValue(point.z(), feature.center.z())) {
            return false;
        }
        *minimumX = std::min(*minimumX, double(point.x()));
        *maximumX = std::max(*maximumX, double(point.x()));
        *minimumY = std::min(*minimumY, double(point.y()));
        *maximumY = std::max(*maximumY, double(point.y()));
    }
    bool corners[2][2] = {{false, false}, {false, false}};
    for (const QVector3D &point : feature.points) {
        const int xIndex = nearValue(point.x(), *minimumX) ? 0
            : (nearValue(point.x(), *maximumX) ? 1 : -1);
        const int yIndex = nearValue(point.y(), *minimumY) ? 0
            : (nearValue(point.y(), *maximumY) ? 1 : -1);
        if (xIndex < 0 || yIndex < 0 || corners[xIndex][yIndex]) {
            return false;
        }
        corners[xIndex][yIndex] = true;
    }
    const double length = *maximumX - *minimumX;
    const double width = *maximumY - *minimumY;
    return corners[0][0] && corners[0][1] && corners[1][0] && corners[1][1] &&
           length > kTolerance && width > kTolerance &&
           (feature.length <= 0.0 || nearValue(feature.length, length)) &&
           (feature.width <= 0.0 || nearValue(feature.width, width));
}

}

ProcessParameterSchema SurfaceFinishStrategy::parameterSchema() const
{
    ProcessParameterSchema schema = ProcessParameterSchema::CommonOperation();
    schema.addDefinition(numberDefinition(
        QStringLiteral("feedHeight"), ProcessParameterUnit::Millimeter, 3.0));
    schema.addDefinition(numberDefinition(
        QStringLiteral("stepover"), ProcessParameterUnit::Millimeter, 0.5));
    schema.addDefinition(numberDefinition(
        QStringLiteral("stockToLeave"), ProcessParameterUnit::Millimeter, 0.0));
    return schema;
}

StrategyParams SurfaceFinishStrategy::defaultParams() const
{
    StrategyParams p = parameterSchema().defaultParams();
    p.set("safeHeight",    50.0);
    p.set("plungeHeight",   3.0);
    p.set("referenceHeight", 0.0);
    p.set("depth",           0.2);
    p.set("stepOver",        0.5);
    p.set("stepDown",        0.2);
    p.set("feedHeight",     3.0);
    p.set("stepover",       0.5);  // very fine lateral step for surface quality
    p.set("spindleSpeed", 4000.0);
    p.set("feedRate",      500.0);
    p.set("plungeRate",    150.0);
    p.set("stockToLeave",    0.0);
    return p;
}

ToolpathResult SurfaceFinishStrategy::generate(const HoleFeature &,
                                                const ToolEntry &,
                                                const StrategyParams &) const
{
    return {QString(), false, QObject::tr("精面策略需要轮廓特征。"), 0.0};
}

// Generates a fine-pitch raster finish pass at a single Z depth (surface milling).
// Suitable for flat surfaces after roughing; analogous to face milling but with
// much smaller stepover for better surface finish (Ra).
ToolpathResult SurfaceFinishStrategy::generate(const ContourFeature &feature,
                                                const ToolEntry      &tool,
                                                const StrategyParams &params) const
{
    ToolpathResult res;
    const QStringList parameterErrors = parameterSchema().validate(params);
    if (!parameterErrors.isEmpty()) {
        res.errorMsg = parameterErrors.join(QLatin1Char('\n'));
        return res;
    }
    if (tool.id <= 0 || tool.diameter <= 0.0 ||
        tool.type != QStringLiteral("end_mill")) {
        res.errorMsg = QObject::tr("刀具直径无效。");
        return res;
    }
    if (feature.depth <= 0.0) {
        res.errorMsg = QObject::tr("加工深度必须大于零。");
        return res;
    }

    double minimumX = 0.0;
    double maximumX = 0.0;
    double minimumY = 0.0;
    double maximumY = 0.0;
    if (!verifiedRectangle(feature, &minimumX, &maximumX, &minimumY, &maximumY)) {
        res.errorMsg = QObject::tr(
            "精面策略仅支持前视 Setup 下可验证的平面矩形边界，不能替代三维曲面刀路。");
        return res;
    }
    const double ztop = feature.center.z();
    const double zcut = ztop - feature.depth;
    const double safe = params.get("safeHeight", 50.0);
    const double feedH = params.get("feedHeight", 3.0);
    const double step = params.get("stepover", 0.5);
    const double S = params.get("spindleSpeed", 4000.0);
    const double F = params.get("feedRate", 500.0);
    const double Fp = params.get("plungeRate", 150.0);
    const double stock = params.get("stockToLeave", 0.0);

    const double toolRadius = tool.diameter * 0.5;
    const double lead = toolRadius;
    const double startX = minimumX - lead;
    const double endX = maximumX + lead;
    const double startY = minimumY - lead;
    const double endY = maximumY + lead;
    const double effectiveStep = step > 0.0 ? step : tool.diameter * 0.1;
    if (safe <= ztop + feedH || feedH < 0.0 || effectiveStep <= 0.0 ||
        F <= 0.0 || Fp <= 0.0 || S <= 0.0 || stock < 0.0 ||
        stock >= feature.depth) {
        res.errorMsg = QObject::tr("精面高度、步距、余量或切削参数不安全。");
        return res;
    }
    const int passes = std::max(2, static_cast<int>(std::ceil((endY - startY) / effectiveStep)) + 1);

    QString gc;
    gc += QString("T%1 M6\n").arg(tool.id);
    gc += QString("S%1 M3\n").arg((int)S);
    gc += QString("G0 Z%1\n").arg(safe, 0, 'f', 3);
    gc += QString("G0 X%1 Y%2\n").arg(startX, 0, 'f', 3).arg(startY, 0, 'f', 3);
    gc += QString("G0 Z%1\n").arg(ztop + feedH, 0, 'f', 3);
    gc += QString("G1 Z%1 F%2\n").arg(zcut + stock, 0, 'f', 3).arg((int)Fp);

    double totalLen = 0.0;
    for (int i = 0; i < passes; ++i) {
        const double y = std::min(startY + i * effectiveStep, endY);
        const double x0 = (i % 2 == 0) ? startX : endX;
        const double x1 = (i % 2 == 0) ? endX : startX;
        gc += QString("G1 X%1 Y%2 F%3\n").arg(x0, 0, 'f', 3).arg(y, 0, 'f', 3).arg((int)F);
        gc += QString("G1 X%1\n").arg(x1, 0, 'f', 3);
        totalLen += std::abs(x1 - x0);
    }
    gc += QString("G0 Z%1\n").arg(safe, 0, 'f', 3);

    res.gcode = gc;
    res.ok    = true;
    res.estimatedTimeS = (totalLen / F * 60.0) + (feature.depth / Fp * 60.0);
    return res;
}
