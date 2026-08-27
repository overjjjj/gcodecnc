#include "PocketRoughingStrategy.h"

#include "../../core/geometry2d/ToolpathGeometry2D.h"

#include <QObject>
#include <algorithm>
#include <cmath>

namespace {

struct ScanLine {
    double y = 0.0;
    double xMin = 0.0;
    double xMax = 0.0;
};

struct Interval {
    double min = 0.0;
    double max = 0.0;
};

struct ScanChain {
    QVector<ScanLine> rows;
};

namespace geometry = cnext::geometry2d;

geometry::Wire geometryWire(const QVector<QVector3D> &points)
{
    QVector<geometry::Point> converted;
    converted.reserve(points.size());
    for (const QVector3D &point : points) {
        converted.append({point.x(), point.y()});
    }
    return geometry::WireFromPolyline(converted, true);
}

static QString cutMove(double x, double y, double feed)
{
    return QStringLiteral("G1 X%1 Y%2 F%3\n")
        .arg(x, 0, 'f', 3)
        .arg(y, 0, 'f', 3)
        .arg(int(feed));
}

static double distance2D(double x0, double y0, double x1, double y1)
{
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    return std::sqrt(dx * dx + dy * dy);
}

static QVector<Interval> mergedIntervals(QVector<Interval> intervals)
{
    std::sort(intervals.begin(), intervals.end(), [](const Interval &a, const Interval &b) {
        return a.min < b.min;
    });
    QVector<Interval> merged;
    for (const Interval &interval : intervals) {
        if (interval.max <= interval.min + 1.0e-9) {
            continue;
        }
        if (merged.isEmpty() || interval.min > merged.last().max + 1.0e-9) {
            merged.push_back(interval);
        } else {
            merged.last().max = std::max(merged.last().max, interval.max);
        }
    }
    return merged;
}

static QVector<Interval> polygonIntervalsAtY(const QVector<QVector3D> &polygon, double y)
{
    QVector<double> intersections;
    for (int i = 0; i < polygon.size(); ++i) {
        const QVector3D &a = polygon.at(i);
        const QVector3D &b = polygon.at((i + 1) % polygon.size());
        const double ay = a.y();
        const double by = b.y();
        if ((ay <= y && by > y) || (by <= y && ay > y)) {
            const double t = (y - ay) / (by - ay);
            intersections.push_back(a.x() + t * (b.x() - a.x()));
        }
    }
    std::sort(intersections.begin(), intersections.end());
    QVector<Interval> intervals;
    for (int i = 0; i + 1 < intersections.size(); i += 2) {
        intervals.push_back({intersections.at(i), intersections.at(i + 1)});
    }
    return intervals;
}

static QVector<Interval> subtractIntervals(const QVector<Interval> &source,
                                           const QVector<Interval> &removed)
{
    QVector<Interval> result = source;
    for (const Interval &cut : mergedIntervals(removed)) {
        QVector<Interval> next;
        for (const Interval &part : result) {
            if (cut.max <= part.min || cut.min >= part.max) {
                next.push_back(part);
                continue;
            }
            if (cut.min > part.min) {
                next.push_back({part.min, std::min(cut.min, part.max)});
            }
            if (cut.max < part.max) {
                next.push_back({std::max(cut.max, part.min), part.max});
            }
        }
        result = next;
    }
    return result;
}

static void appendCircleInterval(QVector<Interval> &intervals,
                                 const QVector3D &center,
                                 double y,
                                 double radius)
{
    const double dy = y - center.y();
    if (std::abs(dy) >= radius) {
        return;
    }
    const double span = std::sqrt(std::max(0.0, radius * radius - dy * dy));
    intervals.push_back({center.x() - span, center.x() + span});
}

static QVector<Interval> boundaryClearanceIntervals(const QVector<QVector3D> &polygon,
                                                    double y,
                                                    double clearance)
{
    QVector<Interval> forbidden;
    for (int i = 0; i < polygon.size(); ++i) {
        const QVector3D &a = polygon.at(i);
        const QVector3D &b = polygon.at((i + 1) % polygon.size());
        const double dx = b.x() - a.x();
        const double dy = b.y() - a.y();
        const double length = std::sqrt(dx * dx + dy * dy);
        if (length <= 1.0e-9) {
            appendCircleInterval(forbidden, a, y, clearance);
            continue;
        }
        const double nx = -dy / length;
        const double ny = dx / length;
        const QVector<QVector3D> strip = {
            QVector3D(float(a.x() + nx * clearance), float(a.y() + ny * clearance), 0.0f),
            QVector3D(float(b.x() + nx * clearance), float(b.y() + ny * clearance), 0.0f),
            QVector3D(float(b.x() - nx * clearance), float(b.y() - ny * clearance), 0.0f),
            QVector3D(float(a.x() - nx * clearance), float(a.y() - ny * clearance), 0.0f)
        };
        forbidden += polygonIntervalsAtY(strip, y);
        appendCircleInterval(forbidden, a, y, clearance);
        appendCircleInterval(forbidden, b, y, clearance);
    }
    return mergedIntervals(forbidden);
}

static QVector<Interval> safeIntervalsAtY(
    const QVector<QVector3D> &outer,
    const QVector<QVector<QVector3D>> &islands,
    double y,
    double clearance)
{
    QVector<Interval> allowed = polygonIntervalsAtY(outer, y);
    QVector<Interval> removed = boundaryClearanceIntervals(outer, y, clearance);
    for (const QVector<QVector3D> &island : islands) {
        removed += polygonIntervalsAtY(island, y);
        removed += boundaryClearanceIntervals(island, y, clearance);
    }
    return subtractIntervals(allowed, removed);
}

static bool hasTopologyVertexBetween(
    const QVector<QVector3D> &outer,
    const QVector<QVector<QVector3D>> &islands,
    double firstY,
    double secondY)
{
    const double minY = std::min(firstY, secondY) + 1.0e-7;
    const double maxY = std::max(firstY, secondY) - 1.0e-7;
    auto loopHasVertex = [=](const QVector<QVector3D> &loop) {
        for (const QVector3D &point : loop) {
            if (point.y() > minY && point.y() < maxY) {
                return true;
            }
        }
        return false;
    };
    if (loopHasVertex(outer)) {
        return true;
    }
    for (const QVector<QVector3D> &island : islands) {
        if (loopHasVertex(island)) {
            return true;
        }
    }
    return false;
}

static QVector<ScanChain> safeScanChains(
    const QVector<ScanLine> &rows,
    const QVector<QVector3D> &outer,
    const QVector<QVector<QVector3D>> &islands,
    double maximumRowGap)
{
    QVector<ScanChain> chains;
    for (const ScanLine &row : rows) {
        int matchingChain = -1;
        for (int index = 0; index < chains.size(); ++index) {
            const ScanLine &previous = chains.at(index).rows.last();
            const bool consecutive = row.y > previous.y + 1.0e-7
                && row.y - previous.y <= maximumRowGap + 0.001;
            const bool sameRegion = std::abs(row.xMin - previous.xMin) <= 0.001
                && std::abs(row.xMax - previous.xMax) <= 0.001;
            if (consecutive && sameRegion
                && !hasTopologyVertexBetween(outer, islands, previous.y, row.y)) {
                matchingChain = index;
                break;
            }
        }
        if (matchingChain < 0) {
            ScanChain chain;
            chain.rows.push_back(row);
            chains.push_back(chain);
        } else {
            chains[matchingChain].rows.push_back(row);
        }
    }
    return chains;
}

static QString expandParametricProgram(const ParametricToolpathProgram &program)
{
    QStringList lines = program.prefixLines;
    for (const ParametricToolpathCall &call : program.calls) {
        for (QString line : program.bodyTemplateLines) {
            for (auto it = call.arguments.cbegin(); it != call.arguments.cend(); ++it) {
                line.replace(QStringLiteral("${%1}").arg(it.key()), it.value());
            }
            lines.append(line);
        }
    }
    lines.append(program.suffixLines);
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

static ToolpathResult generateIrregularPocket(const ContourFeature &feature,
                                              const ToolEntry &tool,
                                              const StrategyParams &params)
{
    ToolpathResult res;
    if (feature.points.size() < 3) {
        res.errorMsg = QObject::tr("Irregular pocket requires at least three boundary points.");
        return res;
    }
    geometry::Region region;
    region.outer = geometryWire(feature.points);
    for (const QVector<QVector3D> &island : feature.islands) {
        region.islands.append(geometryWire(island));
    }
    const geometry::RegionValidation region_validation =
        geometry::ValidateRegion(region, geometry::GeometryTolerance());
    if (!region_validation.ok) {
        res.errorMsg = QObject::tr(
            "Irregular pocket outer and island boundaries are invalid: %1")
            .arg(region_validation.errors.join(QLatin1Char(' ')));
        return res;
    }
    if (std::abs(params.get(QStringLiteral("entryMode"), -1.0)) > 1.0e-6) {
        res.errorMsg = QObject::tr("Irregular pocket clearing currently supports confirmed vertical entry only.");
        return res;
    }

    const double safe = params.get(QStringLiteral("safeHeight"), 50.0);
    const double feedH = params.get(QStringLiteral("feedHeight"), 3.0);
    const double axial = params.get(QStringLiteral("stepDown"), 2.0);
    const double radial = params.get(QStringLiteral("stepover"), 6.0);
    const double spindle = params.get(QStringLiteral("spindleSpeed"), 1800.0);
    const double feed = params.get(QStringLiteral("feedRate"), 600.0);
    const double plunge = params.get(QStringLiteral("plungeRate"), 200.0);
    const double clearance = tool.diameter * 0.5
        + params.get(QStringLiteral("stockToLeave"), 0.3);
    if (axial <= 0.0 || radial <= 0.0 || feed <= 0.0 || plunge <= 0.0) {
        res.errorMsg = QObject::tr("Stepdown, stepover, feed, and plunge rate must be greater than zero.");
        return res;
    }

    double minY = feature.points.first().y();
    double maxY = minY;
    for (const QVector3D &point : feature.points) {
        minY = std::min(minY, double(point.y()));
        maxY = std::max(maxY, double(point.y()));
    }
    const double boundaryMargin = 1.0e-4;
    const double firstY = minY + clearance + boundaryMargin;
    const double lastY = maxY - clearance - boundaryMargin;
    if (firstY > lastY + 1.0e-6) {
        res.errorMsg = QObject::tr("Pocket boundary is too small for the selected tool and stock allowance.");
        return res;
    }

    QVector<ScanLine> rows;
    auto appendSafeRow = [&](double y) {
        bool appended = false;
        for (const Interval &interval : safeIntervalsAtY(
                 feature.points, feature.islands, y, clearance)) {
            if (interval.max - interval.min > 0.001) {
                rows.push_back({y, interval.min, interval.max});
                appended = true;
            }
        }
        return appended;
    };
    double lastGeneratedY = firstY - radial;
    for (double y = firstY; y <= lastY + 1.0e-9; y += radial) {
        if (appendSafeRow(y)) {
            lastGeneratedY = y;
        }
    }
    if (lastGeneratedY < lastY - 0.001) {
        appendSafeRow(lastY);
    }
    if (rows.isEmpty()) {
        res.errorMsg = QObject::tr("No safe clearing region remains after tool-radius and island clearance.");
        return res;
    }
    const QVector<ScanChain> chains = safeScanChains(
        rows, feature.points, feature.islands, radial);

    const int zLayers = static_cast<int>(std::ceil(feature.depth / axial));
    const double ztop = feature.center.z();
    ParametricToolpathProgram program;
    program.routineName = QStringLiteral("IRREGULAR_POCKET_LAYER");
    program.parameterNames = QStringList{QStringLiteral("DEPTH_Z")};
    program.prefixLines = QStringList{
        QStringLiteral("T%1 M6").arg(tool.id),
        QStringLiteral("S%1 M3").arg(int(spindle)),
        QStringLiteral("G0 Z%1").arg(safe, 0, 'f', 3),
        QStringLiteral("; POCKET REGION: IRREGULAR"),
        QStringLiteral("; POCKET ENTRY: VERTICAL")
    };

    double layerLength = 0.0;
    bool leftToRight = true;
    for (const ScanChain &chain : chains) {
        const ScanLine &firstRow = chain.rows.first();
        const double startX = leftToRight ? firstRow.xMin : firstRow.xMax;
        program.bodyTemplateLines.append(
            QStringLiteral("G0 X%1 Y%2")
                .arg(startX, 0, 'f', 3)
                .arg(firstRow.y, 0, 'f', 3));
        program.bodyTemplateLines.append(
            QStringLiteral("G0 Z%1").arg(ztop + feedH, 0, 'f', 3));
        program.bodyTemplateLines.append(
            QStringLiteral("G1 Z${DEPTH_Z} F%1").arg(int(plunge)));
        if (chain.rows.size() > 1) {
            program.bodyTemplateLines.append(
                QStringLiteral("; POCKET LINK: SAFE SAME-REGION"));
        }
        for (int rowIndex = 0; rowIndex < chain.rows.size(); ++rowIndex) {
            const ScanLine &row = chain.rows.at(rowIndex);
            if (rowIndex > 0) {
                const double linkX = leftToRight ? row.xMin : row.xMax;
                program.bodyTemplateLines.append(
                    QStringLiteral("G1 X%1 Y%2 F%3")
                        .arg(linkX, 0, 'f', 3)
                        .arg(row.y, 0, 'f', 3)
                        .arg(int(feed)));
                layerLength += std::abs(
                    row.y - chain.rows.at(rowIndex - 1).y);
            }
            const double endX = leftToRight ? row.xMax : row.xMin;
            program.bodyTemplateLines.append(
                cutMove(endX, row.y, feed).trimmed());
            layerLength += std::abs(
                endX - (leftToRight ? row.xMin : row.xMax));
            leftToRight = !leftToRight;
        }
        program.bodyTemplateLines.append(
            QStringLiteral("G0 Z%1").arg(safe, 0, 'f', 3));
    }
    for (int layer = 1; layer <= zLayers; ++layer) {
        const double zLayer = ztop - std::min(layer * axial, feature.depth);
        ParametricToolpathCall call;
        call.arguments.insert(
            QStringLiteral("DEPTH_Z"), QString::number(zLayer, 'f', 3));
        program.calls.append(call);
    }

    res.gcode = expandParametricProgram(program);
    res.ok = true;
    res.estimatedTimeS = layerLength * zLayers / feed * 60.0
        + chains.size() * zLayers * feature.depth / plunge * 60.0;
    res.parametricProgram = program;
    return res;
}

static bool isCircularPocket(const ContourFeature &feature)
{
    const QString sub = feature.subType.toLower();
    return sub.contains(QStringLiteral("circle"))
        || sub.contains(QStringLiteral("circular"));
}

static void appendRectangularRows(QVector<ScanLine> &rows,
                                  double cx,
                                  double cy,
                                  double halfLength,
                                  double halfWidth,
                                  double step)
{
    const double yMin = cy - halfWidth;
    const double yMax = cy + halfWidth;
    for (double y = yMin; y <= yMax + 0.001; y += step) {
        rows.push_back({y, cx - halfLength, cx + halfLength});
    }
    if (rows.isEmpty() || rows.last().y < yMax - 0.001) {
        rows.push_back({yMax, cx - halfLength, cx + halfLength});
    }
}

static void appendCircularRows(QVector<ScanLine> &rows,
                               double cx,
                               double cy,
                               double radius,
                               double step)
{
    const double yMin = cy - radius;
    const double yMax = cy + radius;
    for (double y = yMin; y <= yMax + 0.001; y += step) {
        const double dy = y - cy;
        const double xSpan = std::sqrt(std::max(0.0, radius * radius - dy * dy));
        rows.push_back({y, cx - xSpan, cx + xSpan});
    }
    if (rows.isEmpty() || rows.last().y < yMax - 0.001) {
        rows.push_back({yMax, cx, cx});
    }
}

} // namespace

StrategyParams PocketRoughingStrategy::defaultParams() const
{
    StrategyParams p;
    p.set("safeHeight",    50.0);
    p.set("feedHeight",     3.0);
    p.set("stepDown",       2.0);
    p.set("stepover",       6.0);
    p.set("spindleSpeed", 1800.0);
    p.set("feedRate",      600.0);
    p.set("plungeRate",    200.0);
    p.set("stockToLeave",    0.3);
    p.set("helixRadius",     2.0);
    p.set("helixPitch",      0.0);
    return p;
}

ToolpathResult PocketRoughingStrategy::generate(const HoleFeature &,
                                                 const ToolEntry &,
                                                 const StrategyParams &) const
{
    return {QString(), false, QObject::tr("型腔开粗策略需要轮廓或区域特征。"), 0.0};
}

ToolpathResult PocketRoughingStrategy::generate(const ContourFeature &feature,
                                                 const ToolEntry      &tool,
                                                 const StrategyParams &params) const
{
    ToolpathResult res;
    if (tool.diameter <= 0.0) {
        res.errorMsg = QObject::tr("刀具直径无效。");
        return res;
    }
    if (feature.depth <= 0.0) {
        res.errorMsg = QObject::tr("加工深度必须大于零。");
        return res;
    }

    const double cx     = feature.center.x();
    const double cy     = feature.center.y();
    const double ztop   = feature.center.z();
    const double safe   = params.get("safeHeight",    50.0);
    const double feedH  = params.get("feedHeight",     3.0);
    const double axial  = params.get("stepDown",       2.0);
    const double radial = params.get("stepover",       6.0);
    const double S      = params.get("spindleSpeed", 1800.0);
    const double F      = params.get("feedRate",      600.0);
    const double Fp     = params.get("plungeRate",    200.0);
    const double stock  = params.get("stockToLeave",    0.3);
    double helixRadius  = params.get("helixRadius",     2.0);
    double helixPitch   = params.get("helixPitch",      0.0);

    if (!params.values.contains(QStringLiteral("entryMode"))) {
        res.errorMsg = QObject::tr("型腔开粗前必须由操作员选择下刀方式。");
        return res;
    }
    const double entryMode = params.get(QStringLiteral("entryMode"), -1.0);
    const bool verticalEntry = std::abs(entryMode) <= 1.0e-6;
    const bool helicalEntry = std::abs(entryMode - 1.0) <= 1.0e-6;
    if (!verticalEntry && !helicalEntry) {
        res.errorMsg = QObject::tr("型腔下刀方式无效，请重新选择垂直下刀或螺旋下刀。");
        return res;
    }

    if (axial <= 0.0 || radial <= 0.0) {
        res.errorMsg = QObject::tr("每层切深和行距必须大于零。");
        return res;
    }

    if (feature.points.size() >= 3) {
        return generateIrregularPocket(feature, tool, params);
    }

    const bool circularPocket = isCircularPocket(feature);
    const double toolRadius = tool.diameter * 0.5;
    double usableHalfLength = feature.radius - toolRadius - stock;
    double usableHalfWidth = usableHalfLength;
    if (!circularPocket && feature.length > 0.0 && feature.width > 0.0) {
        usableHalfLength = feature.length * 0.5 - toolRadius - stock;
        usableHalfWidth = feature.width * 0.5 - toolRadius - stock;
    }
    if (usableHalfLength <= 0.0 || usableHalfWidth <= 0.0) {
        res.errorMsg = QObject::tr("型腔尺寸小于刀具直径和留量，无法开粗。");
        return res;
    }

    const double limitingHalfSize = std::min(usableHalfLength, usableHalfWidth);
    if (helicalEntry) {
        if (helixRadius <= 0.0 || helixPitch <= 0.0) {
            res.errorMsg = QObject::tr("螺旋下刀半径和节距必须由操作员设置为大于零的数值。");
            return res;
        }
        if (helixRadius > limitingHalfSize + 1.0e-6) {
            res.errorMsg = QObject::tr("螺旋下刀超出扣除刀具半径和余量后的型腔边界。");
            return res;
        }
    } else {
        helixRadius = 0.0;
        helixPitch = 0.0;
    }

    QVector<ScanLine> rows;
    const double step = std::min(radial, usableHalfWidth * 2.0);
    if (circularPocket) {
        appendCircularRows(rows, cx, cy, usableHalfLength, step);
    } else {
        appendRectangularRows(rows, cx, cy, usableHalfLength, usableHalfWidth, step);
    }
    if (rows.isEmpty()) {
        res.errorMsg = QObject::tr("无法生成型腔行切路径。");
        return res;
    }
    const double angleRad = circularPocket
        ? 0.0
        : feature.angle * std::acos(-1.0) / 180.0;
    const double cosAngle = std::cos(angleRad);
    const double sinAngle = std::sin(angleRad);
    auto mapPocketPoint = [=](double x, double y) {
        const double localX = x - cx;
        const double localY = y - cy;
        return QVector3D(float(cx + localX * cosAngle - localY * sinAngle),
                         float(cy + localX * sinAngle + localY * cosAngle),
                         0.0f);
    };

    const int zLayers = static_cast<int>(std::ceil(feature.depth / axial));

    QString gc;
    gc += QStringLiteral("T%1 M6\n").arg(tool.id);
    gc += QStringLiteral("S%1 M3\n").arg(int(S));
    gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);
    gc += helicalEntry
        ? QStringLiteral("; POCKET ENTRY: HELICAL\n")
        : QStringLiteral("; POCKET ENTRY: VERTICAL\n");

    double totalLen = 0.0;
    for (int layer = 1; layer <= zLayers; ++layer) {
        const double zLayer = ztop - std::min(layer * axial, feature.depth);
        bool leftToRight = true;
        double currentX = cx;
        double currentY = cy;

        gc += QStringLiteral("G0 Z%1\n").arg(ztop + feedH, 0, 'f', 3);
        if (helicalEntry) {
            currentX = cx + helixRadius;
            currentY = cy;
            gc += QStringLiteral("G0 X%1 Y%2\n")
                      .arg(currentX, 0, 'f', 3)
                      .arg(currentY, 0, 'f', 3);
            const int helixLoops = static_cast<int>(std::ceil(((ztop + feedH) - zLayer) / helixPitch));
            for (int loop = 1; loop <= helixLoops; ++loop) {
                const double zNext = std::max(zLayer, (ztop + feedH) - loop * helixPitch);
                gc += QStringLiteral("G2 X%1 Y%2 I%3 J0.000 Z%4 F%5\n")
                          .arg(currentX, 0, 'f', 3)
                          .arg(currentY, 0, 'f', 3)
                          .arg(-helixRadius, 0, 'f', 3)
                          .arg(zNext, 0, 'f', 3)
                          .arg(int(Fp));
                totalLen += 2.0 * std::acos(-1.0) * helixRadius;
            }
            gc += cutMove(cx, cy, F);
            totalLen += helixRadius;
            currentX = cx;
            currentY = cy;
        } else {
            gc += QStringLiteral("G0 X%1 Y%2\n")
                      .arg(cx, 0, 'f', 3)
                      .arg(cy, 0, 'f', 3);
            gc += QStringLiteral("G1 Z%1 F%2\n").arg(zLayer, 0, 'f', 3).arg(int(Fp));
        }

        for (int i = 0; i < rows.size(); ++i) {
            const ScanLine &row = rows.at(i);
            const QVector3D startPoint = mapPocketPoint(
                leftToRight ? row.xMin : row.xMax, row.y);
            const QVector3D endPoint = mapPocketPoint(
                leftToRight ? row.xMax : row.xMin, row.y);
            const double startX = startPoint.x();
            const double startY = startPoint.y();
            const double endX = endPoint.x();
            const double endY = endPoint.y();

            if (std::abs(currentX - startX) > 0.001 || std::abs(currentY - startY) > 0.001) {
                gc += cutMove(startX, startY, F);
                totalLen += distance2D(currentX, currentY, startX, startY);
            }

            gc += cutMove(endX, endY, F);
            totalLen += distance2D(startX, startY, endX, endY);
            currentX = endX;
            currentY = endY;
            leftToRight = !leftToRight;
        }
    }

    gc += QStringLiteral("G0 Z%1\n").arg(safe, 0, 'f', 3);

    res.gcode = gc;
    if (verticalEntry) {
        ParametricToolpathProgram program;
        program.routineName = QStringLiteral("POCKET_LAYER");
        program.parameterNames = QStringList{QStringLiteral("DEPTH_Z")};
        program.prefixLines = QStringList{
            QStringLiteral("T%1 M6").arg(tool.id),
            QStringLiteral("S%1 M3").arg(int(S)),
            QStringLiteral("G0 Z%1").arg(safe, 0, 'f', 3),
            QStringLiteral("; POCKET ENTRY: VERTICAL")};
        program.bodyTemplateLines = QStringList{
            QStringLiteral("G0 Z%1").arg(ztop + feedH, 0, 'f', 3),
            QStringLiteral("G0 X%1 Y%2").arg(cx, 0, 'f', 3).arg(cy, 0, 'f', 3),
            QStringLiteral("G1 Z${DEPTH_Z} F%1").arg(int(Fp))};

        bool leftToRight = true;
        double currentX = cx;
        double currentY = cy;
        for (const ScanLine &row : rows) {
            const QVector3D startPoint = mapPocketPoint(
                leftToRight ? row.xMin : row.xMax, row.y);
            const QVector3D endPoint = mapPocketPoint(
                leftToRight ? row.xMax : row.xMin, row.y);
            const double startX = startPoint.x();
            const double startY = startPoint.y();
            const double endX = endPoint.x();
            const double endY = endPoint.y();
            if (std::abs(currentX - startX) > 0.001 || std::abs(currentY - startY) > 0.001) {
                program.bodyTemplateLines.append(cutMove(startX, startY, F).trimmed());
            }
            program.bodyTemplateLines.append(cutMove(endX, endY, F).trimmed());
            currentX = endX;
            currentY = endY;
            leftToRight = !leftToRight;
        }
        program.suffixLines = QStringList{
            QStringLiteral("G0 Z%1").arg(safe, 0, 'f', 3)};
        for (int layer = 1; layer <= zLayers; ++layer) {
            ParametricToolpathCall call;
            call.arguments.insert(
                QStringLiteral("DEPTH_Z"),
                QString::number(ztop - std::min(layer * axial, feature.depth), 'f', 3));
            program.calls.append(call);
        }
        if (expandParametricProgram(program) == res.gcode) {
            res.parametricProgram = program;
        }
    }
    res.ok = true;
    res.estimatedTimeS = (totalLen / F * 60.0) + (feature.depth / Fp * 60.0);
    return res;
}
