#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/postprocessor/FanucPostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/safety/GCodeSafetyValidator.h"
#include "../src/strategies/StrategyFactory.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/core/ProjectManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <limits>

static bool expect(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL " << message << '\n';
    return condition;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const auto strategy = StrategyFactory::instance().strategy(QStringLiteral("hole_g73"));
    if (!expect(bool(strategy), "StrategyFactory must expose hole_g73")) return 1;

    HoleFeature hole;
    // Target hole geometry stores the centre at mid-depth; this yields a top at Z0.
    hole.center = QVector3D(10.0f, 20.0f, -10.0f);
    hole.radius = 5.0;
    hole.depth = 20.0;
    hole.subType = QStringLiteral("blind_hole");
    hole.region = FaceRegion::Front;

    ToolEntry tool;
    tool.id = 7;
    tool.type = QStringLiteral("drill");
    tool.diameter = 4.0;
    tool.fluteLen = 30.0;
    tool.totalLen = 80.0;

    StrategyParams params = strategy->defaultParams();
    params.set(QStringLiteral("depth"), 20.0);
    params.set(QStringLiteral("peckDepth"), 5.0);
    params.set(QStringLiteral("safeHeight"), 25.0);
    params.set(QStringLiteral("feedHeight"), 3.0);
    params.set(QStringLiteral("spindleSpeed"), 600.0);
    params.set(QStringLiteral("feedRate"), 40.0);

    StrategyParams userDepthParams = params;
    userDepthParams.set(QStringLiteral("depth"), 5.0);
    const ToolpathResult direct = strategy->generate(hole, tool, userDepthParams);
    if (!expect(direct.ok, "valid G73 parameters must generate") ||
        !expect(direct.gcode.contains(QStringLiteral("CNEXT_HOLE_CYCLE code=G73")),
                "G73 strategy must use target neutral hole marker") ||
        !expect(direct.gcode.contains(QStringLiteral("q=5.000")),
                "G73 marker must preserve positive Q") ||
        !expect(direct.gcode.contains(QStringLiteral("z=-5.000")),
                "explicit G73 depth must override the model depth")) return 1;

    HoleFeature throughHole = hole;
    throughHole.subType = QStringLiteral("through_hole");
    const ToolpathResult shallowThrough = strategy->generate(throughHole, tool, userDepthParams);
    if (!expect(shallowThrough.ok && shallowThrough.gcode.contains(QStringLiteral("z=-5.000")),
                "explicit shallow G73 depth must not add breakthrough allowance")) return 1;

    StrategyParams nonFiniteDepth = params;
    nonFiniteDepth.set(QStringLiteral("depth"), std::numeric_limits<double>::quiet_NaN());
    if (!expect(!strategy->generate(hole, tool, nonFiniteDepth).ok,
                "non-finite G73 depth must be rejected")) return 1;

    ToolEntry nonFiniteTool = tool;
    nonFiniteTool.diameter = std::numeric_limits<double>::quiet_NaN();
    if (!expect(!strategy->generate(hole, nonFiniteTool, params).ok,
                "non-finite G73 tool diameter must be rejected")) return 1;

    StrategyParams badQ = params;
    badQ.set(QStringLiteral("peckDepth"), 0.0);
    if (!expect(!strategy->generate(hole, tool, badQ).ok, "zero Q must be rejected") ||
        !expect(strategy->generate(hole, tool, badQ).gcode.isEmpty(),
                "rejected G73 must not leave partial code")) return 1;

    StrategyParams nonFiniteDwell = params;
    nonFiniteDwell.set(QStringLiteral("dwellTime"),
                       std::numeric_limits<double>::quiet_NaN());
    if (!expect(!strategy->generate(hole, tool, nonFiniteDwell).ok,
                "non-finite dwell must be rejected")) return 1;

    StrategyParams invalidCoolant = params;
    invalidCoolant.set(QStringLiteral("coolantMode"), 3.0);
    if (!expect(!strategy->generate(hole, tool, invalidCoolant).ok,
                "unsupported coolant mode must be rejected")) return 1;

    ToolEntry wrongType = tool;
    wrongType.type = QStringLiteral("tool_holder");
    if (!expect(!strategy->generate(hole, wrongType, params).ok,
                "non-drill tool must be rejected by G73")) return 1;

    StrategyParams badDepth = params;
    badDepth.set(QStringLiteral("depth"), -1.0);
    if (!expect(!strategy->generate(hole, tool, badDepth).ok,
                "negative requested depth must be rejected by G73")) return 1;

    StrategyParams oversizedQ = params;
    oversizedQ.set(QStringLiteral("peckDepth"), 25.0);
    if (!expect(!strategy->generate(hole, tool, oversizedQ).ok,
                "G73 Q must not exceed the effective drilling depth")) return 1;

    ToolEntry oversizeTool = tool;
    oversizeTool.diameter = 10.2;
    if (!expect(!strategy->generate(hole, oversizeTool, params).ok,
                "oversize drill must be rejected by G73")) return 1;

    ToolEntry shortFlute = tool;
    shortFlute.fluteLen = 19.0;
    if (!expect(!strategy->generate(hole, shortFlute, params).ok,
                "G73 depth beyond flute length must be rejected")) return 1;

    ToolEntry shortTool = tool;
    shortTool.totalLen = 20.0;
    if (!expect(!strategy->generate(hole, shortTool, params).ok,
                "G73 depth reaching total tool length must be rejected")) return 1;

    HoleFeature second = hole;
    second.center = QVector3D(15.0f, 20.0f, -10.0f);
    const auto lookup = [strategy](const QString &id) {
        return id == strategy->id() ? strategy : std::shared_ptr<StrategyBase>();
    };
    const auto toolLookup = [tool](int id) { return id == tool.id ? tool : ToolEntry(); };
    ProgramGenerationService generator(lookup, toolLookup);
    MachiningOperation operation;
    operation.id = QStringLiteral("g73-main");
    operation.opType = OperationType::Hole;
    operation.stage = OperationStage::DeepHole;
    operation.strategyId = strategy->id();
    operation.toolId = tool.id;
    operation.params = params;
    operation.holeFeature = hole;
    operation.holeFeature.depth = 20.0;
    MachiningOperation secondOperation = operation;
    secondOperation.id = QStringLiteral("g73-second");
    secondOperation.holeFeature = second;

    PostProcessorOptions options;
    options.programNumber = QStringLiteral("O7301");
    options.workOffset = QStringLiteral("G54");
    options.safeStartBlocks = QStringList{QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
                                          QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    ProgramGenerationSnapshotOptions snapshotOptions;
    snapshotOptions.name = QStringLiteral("G73 integration");
    snapshotOptions.machineProfile.id = QStringLiteral("fanuc-0i");

    FanucPostProcessor fanuc;
    const ProgramGenerationResult generated = generator.generate(
        {operation, secondOperation}, fanuc, options, snapshotOptions);
    const QString &program = generated.snapshot.gcodeText;
    if (argc > 1) {
        QFile sample(QString::fromLocal8Bit(argv[1]));
        if (sample.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            sample.write(program.toUtf8());
        }
    }
    if (!expect(generated.ok, "valid multi-hole G73 must generate") ||
        !expect(program.contains(QStringLiteral("G17 G40 G49 G80")) &&
                    program.contains(QStringLiteral("G90")) && program.contains(QStringLiteral("G54")) &&
                    program.contains(QStringLiteral("T7 M6")) && program.contains(QStringLiteral("G43 H7")) &&
                    program.contains(QStringLiteral("M8")) &&
                    program.contains(QStringLiteral("G73 Z-20.000 R3.000 Q5.000 F40.000")) &&
                    program.count(QStringLiteral("X10.000 Y20.000")) == 1 &&
                    program.count(QStringLiteral("X15.000 Y20.000")) == 1 &&
                    program.contains(QStringLiteral("G80")) && program.contains(QStringLiteral("M9")) &&
                    program.contains(QStringLiteral("M5")) && program.contains(QStringLiteral("M30")),
                "final Fanuc G73 must contain modal safety, tool and shutdown words")) return 1;

    SimulationController simulation;
    QVector<QVector3D> path;
    QVector<bool> rapid;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&path, &rapid](const QVector<QVector3D> &points,
                                     const QVector<bool> &rapidMoves) {
        path = points;
        rapid = rapidMoves;
    });
    simulation.loadGCode(program);
    bool firstBottom = false;
    bool secondBottom = false;
    for (int i = 0; i < rapid.size(); ++i) {
        if (rapid.at(i)) continue;
        const QVector3D &to = path.at(i + 1);
        if (std::abs(to.x() - 10.0f) < 1.0e-3f && std::abs(to.y() - 20.0f) < 1.0e-3f &&
            std::abs(to.z() + 20.0f) < 1.0e-3f) firstBottom = true;
        if (std::abs(to.x() - 15.0f) < 1.0e-3f && std::abs(to.y() - 20.0f) < 1.0e-3f &&
            std::abs(to.z() + 20.0f) < 1.0e-3f) secondBottom = true;
    }
    if (!expect(firstBottom && secondBottom,
                "simulation must expand both final G73 holes to their bottoms")) {
        std::cerr << program.toStdString() << '\n';
        for (int i = 0; i < rapid.size(); ++i) {
            const QVector3D &to = path.at(i + 1);
            std::cerr << (rapid.at(i) ? "R " : "F ")
                      << to.x() << ',' << to.y() << ',' << to.z() << '\n';
        }
        return 1;
    }
    if (argc > 2) {
        QFile evidence(QString::fromLocal8Bit(argv[2]));
        if (evidence.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            evidence.write("segment,mode,from_x,from_y,from_z,to_x,to_y,to_z\n");
            for (int i = 0; i < rapid.size(); ++i) {
                const QVector3D &from = path.at(i);
                const QVector3D &to = path.at(i + 1);
                evidence.write(QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8\n")
                    .arg(i + 1)
                    .arg(rapid.at(i) ? QStringLiteral("rapid") : QStringLiteral("feed"))
                    .arg(from.x(), 0, 'f', 3).arg(from.y(), 0, 'f', 3).arg(from.z(), 0, 'f', 3)
                    .arg(to.x(), 0, 'f', 3).arg(to.y(), 0, 'f', 3).arg(to.z(), 0, 'f', 3)
                    .toUtf8());
            }
        }
    }

    Cq8PostProcessor cq8;
    const ProgramGenerationResult cq8Result = generator.generate(
        {operation}, cq8, options, snapshotOptions);
    if (!expect(cq8Result.ok && cq8Result.snapshot.gcodeText.contains(QStringLiteral("G73 ")),
                "CQ8 must preserve real G73 output through target wrapper")) return 1;

    SiemensPostProcessor siemens;
    const ProgramGenerationResult blockedSiemens = generator.generate(
        {operation}, siemens, options, snapshotOptions);
    if (!expect(!blockedSiemens.ok && blockedSiemens.snapshot.gcodeText.isEmpty() &&
                    blockedSiemens.errors.join('\n').contains(QStringLiteral("cycle contract")),
                "Siemens G73 must block without an explicitly confirmed cycle contract")) return 1;

    ProgramGenerationSnapshotOptions siemensSnapshot = snapshotOptions;
    siemensSnapshot.machineProfile.id = QStringLiteral("siemens-840d-pgz-1102");
    siemensSnapshot.machineProfile.version = QStringLiteral("sw6");
    siemensSnapshot.machineProfile.controllerId = QStringLiteral("siemens-840d");
    StrategyParams siemensParams = params;
    siemensParams.set(QStringLiteral("dwellTime"), 0.3);
    MachiningOperation siemensOperation = operation;
    siemensOperation.params = siemensParams;
    const ProgramGenerationResult siemensResult = generator.generate(
        {siemensOperation}, siemens, options, siemensSnapshot);
    if (!expect(siemensResult.ok &&
                    siemensResult.snapshot.gcodeText.contains(QStringLiteral("MCALL CYCLE83(")) &&
                    !siemensResult.snapshot.gcodeText.contains(QStringLiteral("CNEXT_HOLE_CYCLE")),
                "Siemens must map G73 to executable chip-breaking CYCLE83")) return 1;
    const int siemensOpen = siemensResult.snapshot.gcodeText.indexOf(QStringLiteral("CYCLE83("));
    const int siemensClose = siemensResult.snapshot.gcodeText.indexOf(QLatin1Char(')'), siemensOpen);
    const QStringList siemensArgs = siemensResult.snapshot.gcodeText
        .mid(siemensOpen + QStringLiteral("CYCLE83(").size(), siemensClose - siemensOpen - QStringLiteral("CYCLE83(").size())
        .split(QLatin1Char(','), Qt::KeepEmptyParts);
    if (!expect(siemensArgs.size() >= 17 && siemensArgs.at(6) == QStringLiteral("5.000") &&
                    siemensArgs.at(7) == QStringLiteral("0.000") &&
                    siemensArgs.at(8) == QStringLiteral("0.300") &&
                    siemensArgs.at(10) == QStringLiteral("1.000") &&
                    siemensArgs.at(11) == QStringLiteral("0") &&
                    siemensArgs.at(14) == QStringLiteral("0.000"),
                "Siemens G73 must populate CYCLE83 FDPR/DAM/DTB/FRF/VARI/VRT slots")) return 1;

    const QString invalidFinal = QStringLiteral(
        "G17 G40 G49 G80\nG21\nG90\nG54\nT7 M6\nS600 M3\nG0 Z25\n"
        "G98 G73 Z-20 R3 Q0 F40 X10 Y20\nG80\nM5\nM9\nM30\n");
    if (!expect(!GCodeSafetyValidator::validate(invalidFinal).ok,
                "final safety validation must reject non-positive G73 Q")) return 1;

    ProjectManager project;
    project.setOperations({operation});
    project.setPrograms({generated.snapshot});
    QTemporaryDir temp;
    if (!expect(temp.isValid(), "temporary project directory must be available")) return 1;
    const QString projectPath = QDir(temp.path()).filePath(QStringLiteral("g73-project.json"));
    if (!expect(project.saveToFile(projectPath), "G73 project must save")) return 1;
    ProjectManager reopened;
    if (!expect(reopened.loadFromFile(projectPath), "G73 project must reopen") ||
        !expect(reopened.operations().size() == 1 &&
                    reopened.operations().first().strategyId == QStringLiteral("hole_g73") &&
                    std::abs(reopened.operations().first().params.get("peckDepth") - 5.0) < 1.0e-6,
                "G73 strategy and Q must survive save/reopen")) return 1;

    MachiningOperation invalid = operation;
    invalid.id.clear();
    const ProgramGenerationResult failed = generator.generate(
        {invalid}, fanuc, options, snapshotOptions);
    if (!expect(!failed.ok && failed.snapshot.gcodeText.isEmpty(),
                "invalid operation must fail atomically without program snapshot")) return 1;

    std::cout << "PASS G73 strategy, target postprocessors, simulation, save/reopen, and atomic failure" << std::endl;
    return 0;
}
