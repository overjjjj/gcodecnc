#include "../src/import/StepImporter.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/hole/PeckDrillingStrategy.h"
#include "../src/strategies/mill/PocketRoughingStrategy.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>

#include <cmath>
#include <iostream>
#include <memory>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

bool hasFeedMoveTo(const QVector<QVector3D> &path,
                   const QVector<bool> &rapidSegments,
                   const QVector3D &target)
{
    for (int index = 0; index < rapidSegments.size(); ++index) {
        if (!rapidSegments[index] && (path[index + 1] - target).length() < 1.0e-3f) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString fixturePath = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral(
                                        "../测试模型/验收/WH250852-模板紅色面加工（零件3）.STEP"));
    QFile fixture(fixturePath);
    if (!expect(fixture.open(QIODevice::ReadOnly),
                "the committed acceptance STEP model should be readable")) {
        return 1;
    }
    const QString fingerprint = QString::fromLatin1(
        QCryptographicHash::hash(fixture.readAll(), QCryptographicHash::Sha256)
            .toHex().toUpper());
    if (!expect(fingerprint == QStringLiteral(
                    "7728E246C323C2B4BD9D6BDE7066BC90DF2E94CED7AD2001D896012C86A1FA50"),
                "the acceptance STEP fingerprint should remain unchanged")) {
        return 1;
    }

    StepImporter importer;
    if (!expect(importer.load(fixturePath),
                importer.lastError().toUtf8().constData())) {
        return 1;
    }
    const double modelZSpan = importer.mesh().bbMax.z() - importer.mesh().bbMin.z();
    if (!expect(modelZSpan > 49.9 && modelZSpan < 50.1,
                "the acceptance model Z thickness should remain stable")) {
        return 1;
    }

    int sideAxisSlots = 0;
    int frontHoleCount = 0;
    int automaticThreadCount = 0;
    int invalidCountersinkCount = 0;
    HoleFeature selectedThroughHole;
    HoleFeature pairedThroughHole;
    bool hasSelectedThroughHole = false;
    bool hasPairedThroughHole = false;
    MachiningFeature selectedComplexPocket;
    bool hasSelectedComplexPocket = false;
    int pocketIndex = 0;
    int sevenIslandPocketCount = 0;
    for (const MachiningFeature &feature : importer.features()) {
        if (feature.kind == FeatureKind::Pocket) {
            ++pocketIndex;
            if (feature.islandBoundaries.size() == 7) {
                ++sevenIslandPocketCount;
                if (!hasSelectedComplexPocket) {
                    selectedComplexPocket = feature;
                    hasSelectedComplexPocket = true;
                }
            }
        }
        if ((feature.kind == FeatureKind::Hole || feature.kind == FeatureKind::Thread) &&
            feature.region == FaceRegion::Front) {
            ++frontHoleCount;
        }
        if (feature.kind == FeatureKind::Thread) {
            ++automaticThreadCount;
        }
        if (feature.subType.startsWith(QStringLiteral("countersunk_")) &&
            feature.secondaryRadius <= feature.radius + 0.05) {
            ++invalidCountersinkCount;
        }
        if (feature.kind == FeatureKind::Hole &&
            feature.subType == QStringLiteral("through_hole") &&
            std::abs(feature.center.x() + 60.0f) < 0.02f &&
            std::abs(feature.center.y() - 305.0f) < 0.02f) {
            selectedThroughHole = feature;
            hasSelectedThroughHole = true;
        }
        if (feature.kind == FeatureKind::Hole &&
            feature.subType == QStringLiteral("through_hole") &&
            std::abs(feature.center.x() + 60.0f) < 0.02f &&
            std::abs(feature.center.y() + 305.0f) < 0.02f) {
            pairedThroughHole = feature;
            hasPairedThroughHole = true;
        }
        if (feature.kind != FeatureKind::Slot) {
            continue;
        }
        if (std::abs(feature.axis.normalized().z()) < 0.65) {
            if (feature.region == FaceRegion::Side) {
                ++sideAxisSlots;
            }
            continue;
        }
        if (feature.region != FaceRegion::Front) {
            continue;
        }
        if (!expect(feature.depth <= modelZSpan + 0.1,
                    "a front slot depth must not exceed the model thickness")) {
            std::cerr << "invalid slot center=(" << feature.center.x() << ','
                      << feature.center.y() << ',' << feature.center.z()
                      << ") depth=" << feature.depth
                      << " modelZSpan=" << modelZSpan << '\n';
            return 1;
        }
    }
    if (!expect(pocketIndex == 13,
                "the acceptance model should retain its thirteen recognized pockets") ||
        !expect(sevenIslandPocketCount == 2 && hasSelectedComplexPocket,
                "the acceptance model should retain two complex pockets with seven islands each")) {
        std::cerr << "recognized pockets=" << pocketIndex
                  << " seven-island pockets=" << sevenIslandPocketCount << '\n';
        return 1;
    }
    if (!expect(sideAxisSlots >= 4,
                "known side-axis slots should remain recognized as side features")) {
        return 1;
    }
    if (!expect(frontHoleCount > 0,
                "the acceptance model should expose front hole features")) {
        return 1;
    }
    if (!expect(automaticThreadCount == 0,
                "smooth STEP cylinders must not be promoted to threads without explicit thread evidence")) {
        std::cerr << "automatically classified threads: " << automaticThreadCount << '\n';
        return 1;
    }
    if (!expect(invalidCountersinkCount == 0,
                "a countersink candidate must visibly enlarge the cylindrical bore at its conical face")) {
        std::cerr << "countersinks without an enlarged cone: " << invalidCountersinkCount << '\n';
        return 1;
    }
    if (!expect(hasSelectedThroughHole &&
                    std::abs(selectedThroughHole.radius - 10.5) < 0.02 &&
                    std::abs(selectedThroughHole.depth - 50.0) < 0.02 &&
                    hasPairedThroughHole &&
                    std::abs(pairedThroughHole.radius - selectedThroughHole.radius) < 0.02 &&
                    std::abs(pairedThroughHole.depth - selectedThroughHole.depth) < 0.02,
                "the acceptance model should retain the paired D21 through-hole group")) {
        return 1;
    }

    const auto peckStrategy = std::make_shared<PeckDrillingStrategy>();
    ProgramGenerationService generationService(
        [peckStrategy](const QString &strategyId) -> std::shared_ptr<StrategyBase> {
            return strategyId == peckStrategy->id()
                ? peckStrategy
                : std::shared_ptr<StrategyBase>();
        },
        [](int toolId) {
            ToolEntry tool;
            if (toolId == 21) {
                tool.id = 21;
                tool.type = QStringLiteral("drill");
                tool.diameter = 21.0;
            }
            return tool;
        });

    MachiningOperation operation;
    operation.id = QStringLiteral("acceptance-hole-d21");
    operation.opType = OperationType::Hole;
    operation.stage = OperationStage::RoughCut;
    operation.featureRef = QStringLiteral("WH250852 D21 through hole");
    operation.strategyId = peckStrategy->id();
    operation.toolId = 21;
    operation.params = peckStrategy->defaultParams();
    operation.holeFeature = selectedThroughHole;
    operation.markToolpathValid();

    MachiningOperation pairedOperation = operation;
    pairedOperation.id = QStringLiteral("acceptance-hole-d21-paired");
    pairedOperation.featureRef = QStringLiteral("WH250852 paired D21 through hole");
    pairedOperation.holeFeature = pairedThroughHole;

    PostProcessorOptions postOptions;
    postOptions.programNumber = QStringLiteral("O250852");
    postOptions.workOffset = QStringLiteral("G54");
    postOptions.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"),
        QStringLiteral("G21"),
        QStringLiteral("G90"),
        QStringLiteral("G54"),
        QStringLiteral("G94")};
    ProgramGenerationSnapshotOptions snapshotOptions;
    snapshotOptions.name = QStringLiteral("WH250852 acceptance hole");
    snapshotOptions.sourceSummary = QStringLiteral("acceptance STEP / confirmed D21 hole");
    snapshotOptions.mainProgramName = QStringLiteral("WH250852_D21");
    snapshotOptions.machineProfile.id = QStringLiteral("cq8-interface");
    snapshotOptions.machineProfile.version = QStringLiteral("reserved-1");
    snapshotOptions.machineProfile.controllerId = QStringLiteral("cq8");
    snapshotOptions.machineProfile.safeStartBlocks = postOptions.safeStartBlocks;

    Cq8PostProcessor postProcessor;
    const ProgramGenerationResult generated = generationService.generate(
        {operation, pairedOperation}, postProcessor, postOptions, snapshotOptions);
    if (!expect(generated.ok, "the confirmed acceptance hole should generate final G-code") ||
        !expect(generated.snapshot.gcodeText.count(
                    QStringLiteral("G98 G83 Z-51.000 R3.000 Q3.000 F60.000 X-60.000 Y305.000")) == 1,
                "the final CQ8-interface program should define one shared G83 cycle") ||
        !expect(generated.snapshot.gcodeText.contains(
                    QStringLiteral("X-60.000 Y-305.000")),
                "the shared G83 cycle should call the paired D21 coordinate") ||
        !expect(generated.snapshot.gcodeText.contains(QStringLiteral("G80")) &&
                    generated.snapshot.gcodeText.contains(QStringLiteral("M5")) &&
                    generated.snapshot.gcodeText.contains(QStringLiteral("M9")) &&
                    generated.snapshot.gcodeText.contains(QStringLiteral("M30")),
                "the final program should cancel cycles and shut down safely") ||
        !expect(generated.snapshot.postProcessorId == QStringLiteral("cq8") &&
                    generated.snapshot.sourceOperationIds ==
                        QStringList{QStringLiteral("acceptance-hole-d21"),
                                    QStringLiteral("acceptance-hole-d21-paired")},
                "the final snapshot should retain CQ8-interface and both operation IDs")) {
        if (!generated.errors.isEmpty()) {
            std::cerr << generated.errors.join(QLatin1Char('\n')).toStdString() << '\n';
        }
        return 1;
    }

    SimulationController simulation;
    QVector<QVector3D> path;
    QVector<bool> rapidSegments;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&path, &rapidSegments](const QVector<QVector3D> &newPath,
                                             const QVector<bool> &newRapidSegments) {
        path = newPath;
        rapidSegments = newRapidSegments;
    });
    simulation.loadGCode(generated.snapshot.gcodeText);
    if (!expect(hasFeedMoveTo(path, rapidSegments, QVector3D(-60.0f, 305.0f, -51.0f)) &&
                    hasFeedMoveTo(path, rapidSegments, QVector3D(-60.0f, -305.0f, -51.0f)),
                "the final acceptance G83 program should simulate both D21 hole-bottom feed moves")) {
        return 1;
    }

    ContourFeature complexPocket;
    complexPocket.subType = QStringLiteral("irregular_pocket");
    complexPocket.center = selectedComplexPocket.center;
    complexPocket.depth = selectedComplexPocket.depth;
    complexPocket.width = selectedComplexPocket.width;
    complexPocket.length = selectedComplexPocket.length;
    complexPocket.angle = selectedComplexPocket.angle;
    complexPocket.axis = selectedComplexPocket.axis;
    complexPocket.region = selectedComplexPocket.region;
    complexPocket.points = selectedComplexPocket.boundaryPoints;
    complexPocket.islands = selectedComplexPocket.islandBoundaries;

    const auto pocketStrategy = std::make_shared<PocketRoughingStrategy>();
    ProgramGenerationService pocketGenerationService(
        [pocketStrategy](const QString &strategyId) -> std::shared_ptr<StrategyBase> {
            return strategyId == pocketStrategy->id()
                ? pocketStrategy
                : std::shared_ptr<StrategyBase>();
        },
        [](int toolId) {
            ToolEntry tool;
            if (toolId == 10) {
                tool.id = 10;
                tool.type = QStringLiteral("end_mill");
                tool.diameter = 10.0;
                tool.fluteLen = 80.0;
                tool.totalLen = 100.0;
            }
            return tool;
        });

    MachiningOperation pocketOperation;
    pocketOperation.id = QStringLiteral("acceptance-pocket-seven-islands");
    pocketOperation.opType = OperationType::Roughing;
    pocketOperation.stage = OperationStage::RoughCut;
    pocketOperation.featureRef = QStringLiteral("WH250852 seven-island pocket");
    pocketOperation.strategyId = pocketStrategy->id();
    pocketOperation.toolId = 10;
    pocketOperation.params = pocketStrategy->defaultParams();
    pocketOperation.params.set(QStringLiteral("entryMode"), 0.0);
    pocketOperation.params.set(QStringLiteral("safeHeight"), 70.0);
    pocketOperation.params.set(QStringLiteral("feedHeight"), 5.0);
    pocketOperation.params.set(QStringLiteral("stepDown"), 10.0);
    pocketOperation.params.set(QStringLiteral("stepover"), 20.0);
    pocketOperation.params.set(QStringLiteral("stockToLeave"), 0.5);
    pocketOperation.contourFeature = complexPocket;
    pocketOperation.markToolpathValid();

    ProgramGenerationSnapshotOptions pocketSnapshotOptions = snapshotOptions;
    pocketSnapshotOptions.name = QStringLiteral("WH250852 acceptance complex pocket");
    pocketSnapshotOptions.sourceSummary =
        QStringLiteral("acceptance STEP / confirmed seven-island pocket roughing");
    pocketSnapshotOptions.mainProgramName = QStringLiteral("WH250852_POCKET");
    const ProgramGenerationResult generatedPocket = pocketGenerationService.generate(
        {pocketOperation}, postProcessor, postOptions, pocketSnapshotOptions);
    if (!expect(generatedPocket.ok,
                "the confirmed acceptance complex pocket should generate final CQ8 G-code") ||
        !expect(!generatedPocket.snapshot.expandedGcodeText.isEmpty() &&
                    generatedPocket.snapshot.lineCount < generatedPocket.snapshot.expandedLineCount,
                "the complex pocket should retain a shorter CQ8 macro program and expanded safety program") ||
        !expect(generatedPocket.snapshot.gcodeText.contains(QStringLiteral("M98 P")) &&
                    generatedPocket.snapshot.macroText.contains(QStringLiteral("M99")),
                "the complex pocket should expose a callable CQ8 routine and terminating macro library") ||
        !expect(generatedPocket.snapshot.sourceOperationIds ==
                    QStringList{QStringLiteral("acceptance-pocket-seven-islands")},
                "the complex-pocket snapshot should retain its confirmed operation ID")) {
        if (!generatedPocket.errors.isEmpty()) {
            std::cerr << generatedPocket.errors.join(QLatin1Char('\n')).toStdString() << '\n';
        }
        return 1;
    }

    QVector<QVector3D> pocketPath;
    QVector<bool> pocketRapidSegments;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&pocketPath, &pocketRapidSegments](
                         const QVector<QVector3D> &newPath,
                         const QVector<bool> &newRapidSegments) {
        pocketPath = newPath;
        pocketRapidSegments = newRapidSegments;
    });
    simulation.loadGCode(generatedPocket.snapshot.expandedGcodeText);
    int pocketFeedSegments = 0;
    for (bool rapid : pocketRapidSegments) {
        if (!rapid) {
            ++pocketFeedSegments;
        }
    }
    if (!expect(pocketPath.size() > 2 && pocketFeedSegments > 0,
                "the expanded complex-pocket program should produce visible feed toolpath segments")) {
        return 1;
    }
    return 0;
}
