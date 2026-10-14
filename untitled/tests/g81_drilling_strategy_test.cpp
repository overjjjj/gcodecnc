#include "../src/strategies/StrategyFactory.h"
#include "../src/strategies/hole/SpotDrillingStrategy.h"
#include "../src/postprocessor/FanucPostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/core/ProjectManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>

static bool expect(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL " << message << '\n';
    return condition;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const auto g81 = StrategyFactory::instance().strategy(QStringLiteral("hole_g81"));
    const auto spot = StrategyFactory::instance().strategy(QStringLiteral("hole_spot"));
    if (!expect(bool(g81), "StrategyFactory must expose independent hole_g81") ||
        !expect(bool(spot), "StrategyFactory must keep hole_spot")) return 1;
    if (!expect(g81->id() != spot->id(), "G81 and spot strategies must have distinct ids") ||
        !expect(g81->displayName().contains(QStringLiteral("G81")),
                "G81 strategy display name must identify G81")) return 1;

    HoleFeature hole;
    hole.center = QVector3D(12.0f, 24.0f, -10.0f);
    hole.radius = 5.0;
    hole.depth = 20.0;
    hole.subType = QStringLiteral("blind_hole");
    hole.region = FaceRegion::Front;
    ToolEntry drill;
    drill.id = 81;
    drill.type = QStringLiteral("drill");
    drill.diameter = 4.0;
    drill.fluteLen = 40.0;
    drill.totalLen = 80.0;

    StrategyParams params = g81->defaultParams();
    params.set(QStringLiteral("depth"), 15.0);
    params.set(QStringLiteral("safeHeight"), 30.0);
    params.set(QStringLiteral("feedHeight"), 3.0);
    params.set(QStringLiteral("feedRate"), 50.0);
    const ToolpathResult direct = g81->generate(hole, drill, params);
    if (!expect(direct.ok, "valid G81 parameters must generate") ||
        !expect(direct.gcode.contains(QStringLiteral("CNEXT_HOLE_CYCLE code=G81")),
                "G81 strategy must emit neutral G81 marker") ||
        !expect(direct.gcode.contains(QStringLiteral("z=-15.000")),
                "G81 must use confirmed 15 mm depth from the hole top") ||
        !expect(!g81->generate(hole, ToolEntry(), params).ok,
                "G81 must reject a non-drill tool")) return 1;

    const auto lookup = [g81](const QString &id) {
        return id == g81->id() ? g81 : std::shared_ptr<StrategyBase>();
    };
    const auto toolLookup = [drill](int id) { return id == drill.id ? drill : ToolEntry(); };
    ProgramGenerationService generator(lookup, toolLookup);
    MachiningOperation operation;
    operation.id = QStringLiteral("g81-main");
    operation.opType = OperationType::Hole;
    operation.stage = OperationStage::Setup;
    operation.strategyId = g81->id();
    operation.toolId = drill.id;
    operation.params = params;
    operation.holeFeature = hole;
    PostProcessorOptions options;
    options.programNumber = QStringLiteral("O8101");
    options.safeStartBlocks = QStringList{QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
                                         QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};
    ProgramGenerationSnapshotOptions snapshot;
    snapshot.machineProfile.id = QStringLiteral("fanuc-0i");
    FanucPostProcessor fanuc;
    const ProgramGenerationResult generated = generator.generate({operation}, fanuc, options, snapshot);
    if (!expect(generated.ok, "Fanuc G81 operation must generate") ||
        !expect(generated.snapshot.gcodeText.contains(QStringLiteral("G81 Z-15.000 R3.000 F50.000 X12.000 Y24.000")),
                "Fanuc G81 must preserve confirmed depth and hole position") ||
        !expect(generated.snapshot.gcodeText.contains(QStringLiteral("G80")),
                "Fanuc G81 must cancel the fixed cycle")) return 1;

    SiemensPostProcessor siemens;
    snapshot.machineProfile.id = QStringLiteral("siemens-840d-pgz-1102");
    snapshot.machineProfile.version = QStringLiteral("sw6");
    snapshot.machineProfile.controllerId = QStringLiteral("siemens-840d");
    const ProgramGenerationResult siemensResult = generator.generate({operation}, siemens, options, snapshot);
    if (!expect(siemensResult.ok, "Siemens G81 operation must generate") ||
        !expect(siemensResult.snapshot.gcodeText.contains(QStringLiteral("MCALL CYCLE81(")),
                "Siemens G81 must map to CYCLE81") ||
        !expect(siemensResult.snapshot.gcodeText.contains(QStringLiteral("-15.000")),
                "Siemens G81 must preserve confirmed depth")) return 1;

    ProjectManager project;
    project.setOperations({operation});
    project.setPrograms({generated.snapshot});
    QTemporaryDir temp;
    if (!expect(temp.isValid(), "temporary directory must be available")) return 1;
    const QString path = QDir(temp.path()).filePath(QStringLiteral("g81.json"));
    if (!expect(project.saveToFile(path), "G81 project must save")) return 1;
    ProjectManager reopened;
    if (!expect(reopened.loadFromFile(path), "G81 project must reopen") ||
        !expect(reopened.operations().size() == 1 &&
                    reopened.operations().first().strategyId == QStringLiteral("hole_g81") &&
                    std::abs(reopened.operations().first().params.get(QStringLiteral("depth")) - 15.0) < 1.0e-6,
                "G81 strategy and depth must survive save/reopen")) return 1;

    std::cout << "PASS g81 independent strategy, Fanuc/Siemens mapping, depth, and save/reopen" << std::endl;
    return 0;
}
