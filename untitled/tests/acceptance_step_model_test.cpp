#include "../src/import/StepImporter.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/services/ProgramGenerationService.h"
#include "../src/simulation/SimulationController.h"
#include "../src/strategies/hole/PeckDrillingStrategy.h"

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
    for (const MachiningFeature &feature : importer.features()) {
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
    return 0;
}
