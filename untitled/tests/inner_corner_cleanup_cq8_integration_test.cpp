#include "../src/gcode/GCodeSafetyValidator.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/postprocessor/FanucPostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/mill/InnerCornerCleanupStrategy.h"

#include <QCoreApplication>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

class PreviousPocketStrategy : public StrategyBase
{
public:
    QString id() const override { return QStringLiteral("mill_pocket_rough"); }
    QString displayName() const override { return QStringLiteral("Previous pocket"); }
    StrategyParams defaultParams() const override { return {}; }
    ToolpathResult generate(const HoleFeature &, const ToolEntry &,
                            const StrategyParams &) const override
    {
        return {QString(), false, QStringLiteral("hole unsupported"), 0.0};
    }
    ToolpathResult generate(const ContourFeature &, const ToolEntry &,
                            const StrategyParams &) const override
    {
        return {QStringLiteral("T10 M6\nS1800 M3\nG0 Z12.000\nG0 X15.000 Y10.000\n"
                               "G1 Z-6.000 F150.000\nG0 Z12.000"),
                true, QString(), 1.0};
    }
};

ContourFeature pocket()
{
    ContourFeature feature;
    feature.subType = QStringLiteral("rectangular_pocket");
    feature.depth = 6.0;
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.region = FaceRegion::Front;
    feature.points = {
        QVector3D(0.0f, 0.0f, 0.0f), QVector3D(30.0f, 0.0f, 0.0f),
        QVector3D(30.0f, 20.0f, 0.0f), QVector3D(0.0f, 20.0f, 0.0f)};
    return feature;
}

ToolEntry tool(int id)
{
    ToolEntry value;
    value.id = id;
    value.type = QStringLiteral("end_mill");
    value.diameter = id == 10 ? 10.0 : 4.0;
    value.fluteLen = 20.0;
    value.totalLen = 60.0;
    return value;
}

MachiningOperation previousOperation()
{
    MachiningOperation operation;
    operation.id = QStringLiteral("pocket-op");
    operation.opType = OperationType::Roughing;
    operation.stage = OperationStage::RoughCut;
    operation.strategyId = QStringLiteral("mill_pocket_rough");
    operation.toolId = 10;
    operation.geometryRefs = QStringList{QStringLiteral("feature:pocket-1")};
    operation.contourFeature = pocket();
    operation.params.set(QStringLiteral("workOffset"), 54.0);
    operation.markToolpathValid();
    return operation;
}

MachiningOperation cleanupOperation(const InnerCornerCleanupStrategy &strategy)
{
    MachiningOperation operation;
    operation.id = QStringLiteral("cleanup-op");
    operation.opType = OperationType::Contour;
    operation.stage = OperationStage::Cleanup;
    operation.strategyId = strategy.id();
    operation.toolId = 4;
    operation.geometryRefs = QStringList{QStringLiteral("feature:pocket-1")};
    operation.dependencyOperationIds = QStringList{QStringLiteral("pocket-op")};
    operation.contourFeature = pocket();
    operation.params = strategy.defaultParams();
    operation.params.set(QStringLiteral("safeHeight"), 12.0);
    operation.params.set(QStringLiteral("plungeHeight"), 3.0);
    operation.params.set(QStringLiteral("referenceHeight"), 0.0);
    operation.params.set(QStringLiteral("depth"), 6.0);
    operation.params.set(QStringLiteral("stepDown"), 2.0);
    operation.params.set(QStringLiteral("stepOver"), 1.0);
    operation.params.set(QStringLiteral("previousToolDiameter"), 10.0);
    operation.params.set(QStringLiteral("cornerTolerance"), 0.05);
    operation.params.set(QStringLiteral("safetyDistance"), 0.5);
    operation.params.set(QStringLiteral("workOffset"), 54.0);
    operation.markToolpathValid();
    return operation;
}

void verifySimulation(const QString &gcode)
{
    SimulationController simulation;
    QVector<QVector3D> path;
    QVector<bool> rapid;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&path, &rapid](const QVector<QVector3D> &points,
                                     const QVector<bool> &rapidSegments) {
                         path = points;
                         rapid = rapidSegments;
                     });
    simulation.loadGCode(gcode);
    bool reachesCorner = false;
    bool reachesFinalDepth = false;
    for (int index = 0; index < rapid.size(); ++index) {
        const QVector3D &from = path.at(index);
        const QVector3D &to = path.at(index + 1);
        if (rapid.at(index) &&
            (std::abs(from.x() - to.x()) > 0.001f ||
             std::abs(from.y() - to.y()) > 0.001f)) {
            require(std::min(from.z(), to.z()) >= 12.0f - 0.001f,
                    "cleanup rapid XY moves must remain at safe height");
        }
        if (!rapid.at(index)) {
            reachesCorner = reachesCorner ||
                (std::abs(to.x() - 2.05f) < 0.02f &&
                 std::abs(to.y() - 2.05f) < 0.02f);
            reachesFinalDepth = reachesFinalDepth || std::abs(to.z() + 6.0f) < 0.02f;
        }
    }
    require(reachesCorner && reachesFinalDepth,
            "final-code simulation must reach the bounded corner at final depth");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const auto previous = std::make_shared<PreviousPocketStrategy>();
    const auto cleanup = std::make_shared<InnerCornerCleanupStrategy>();
    ProgramGenerationService generator(
        [previous, cleanup](const QString &id) {
            if (id == previous->id()) return std::static_pointer_cast<StrategyBase>(previous);
            if (id == cleanup->id()) return std::static_pointer_cast<StrategyBase>(cleanup);
            return std::shared_ptr<StrategyBase>();
        },
        [](int id) { return id == 10 || id == 4 ? tool(id) : ToolEntry(); });

    const QList<MachiningOperation> operations{
        previousOperation(), cleanupOperation(*cleanup)};
    PostProcessorOptions options;
    options.programNumber = QStringLiteral("O2801");
    options.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
        QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    ProgramGenerationSnapshotOptions snapshot;

    const ProgramGenerationResult cq8 = generator.generate(
        operations, Cq8PostProcessor(), options, snapshot);
    if (!cq8.ok) {
        std::cerr << cq8.errors.join(QLatin1Char('\n')).toStdString() << '\n';
    }
    require(cq8.ok && GCodeSafetyValidator::validate(cq8.snapshot.gcodeText).ok,
            "dependency-bound cleanup must generate safe final CQ8");
    require(cq8.snapshot.gcodeText.contains(QStringLiteral("INNER CORNER LAYER 3")),
            "final CQ8 must retain cleanup layer traceability");
    verifySimulation(cq8.snapshot.expandedGcodeText.isEmpty()
                         ? cq8.snapshot.gcodeText
                         : cq8.snapshot.expandedGcodeText);

    const ProgramGenerationResult fanuc = generator.generate(
        operations, FanucPostProcessor(), options, snapshot);
    if (!fanuc.ok || !fanuc.snapshot.gcodeText.contains(QStringLiteral("%\nO2801"))) {
        std::cerr << fanuc.errors.join(QLatin1Char('\n')).toStdString() << '\n'
                  << fanuc.snapshot.gcodeText.left(120).toStdString() << '\n';
    }
    require(fanuc.ok && fanuc.snapshot.gcodeText.contains(QStringLiteral("%\nO2801")) &&
                GCodeSafetyValidator::validate(fanuc.snapshot.gcodeText).ok,
            "Fanuc cleanup output must retain its controller header and pass safety");

    const ProgramGenerationResult siemens = generator.generate(
        operations, SiemensPostProcessor(), options, snapshot);
    if (!siemens.ok || !siemens.snapshot.gcodeText.contains(
            QStringLiteral("Post-processor: Siemens 840D"))) {
        std::cerr << siemens.errors.join(QLatin1Char('\n')).toStdString() << '\n'
                  << siemens.snapshot.gcodeText.left(160).toStdString() << '\n';
    }
    require(siemens.ok && siemens.snapshot.gcodeText.contains(
            QStringLiteral("Post-processor: Siemens 840D")) &&
                GCodeSafetyValidator::validate(siemens.snapshot.gcodeText).ok,
            "Siemens cleanup output must retain its controller header and pass safety");

    MachiningOperation missingDependency = cleanupOperation(*cleanup);
    missingDependency.dependencyOperationIds.clear();
    require(!generator.generate({previousOperation(), missingDependency},
                                Cq8PostProcessor(), options, snapshot).ok,
            "cleanup without an explicit prior operation must block final generation");

    MachiningOperation wrongGeometry = previousOperation();
    wrongGeometry.geometryRefs = QStringList{QStringLiteral("feature:other-pocket")};
    require(!generator.generate({wrongGeometry, cleanupOperation(*cleanup)},
                                Cq8PostProcessor(), options, snapshot).ok,
            "cleanup and prior pocket must reference the same geometry");
    return 0;
}
