#include "../src/strategies/hole/PeckDrillingStrategy.h"
#include "../src/strategies/hole/HoleCircularMillingStrategy.h"
#include "../src/strategies/hole/ChamferStrategy.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include <QFile>
#include <cmath>
#include "../src/workspaces/process_design/ui/OperationParameterGroups.h"
#include "../src/workspaces/process_design/ui/OperationEditDialog.h"
#include "../src/tool/ToolLibrary.h"
#include <QApplication>
#include <iostream>
#include <limits>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    bool ok = true;
    const auto check = [&ok](bool passed, const char *message) {
        if (!passed) { std::cerr << message << '\n'; ok = false; }
    };
    HoleFeature hole;
    hole.radius = 5;
    hole.depth = 10;
    hole.center = QVector3D(0, 0, -5);
    hole.subType = QStringLiteral("through_hole");
    ToolEntry tool;
    tool.id = 901;
    tool.type = QStringLiteral("drill");
    tool.diameter = 6;
    tool.fluteLen = 10.5;
    tool.totalLen = 50;
    auto drill = std::make_shared<PeckDrillingStrategy>();
    auto mill = std::make_shared<HoleCircularMillingStrategy>();
    auto chamfer = std::make_shared<ChamferStrategy>();
    ProgramGenerationService service(
        [=](const QString &id) -> std::shared_ptr<StrategyBase> {
            if (id == drill->id()) return drill;
            if (id == mill->id()) return mill;
            if (id == chamfer->id()) return chamfer;
            return {};
        }, [&tool](int) { return tool; });
    MachiningOperation op;
    op.id = "depth-contract";
    op.opType = OperationType::Hole;
    op.strategyId = drill->id();
    op.toolId = tool.id;
    op.holeFeature = hole;
    op.params = drill->defaultParams();
    Cq8PostProcessor post;
    PostProcessorOptions options;
    options.workOffset = "G54";
    const auto through = service.generate({op}, post, options, {});
    check(!through.ok && through.errors.join('\n').contains("flute length")
          && through.snapshot.gcodeText.isEmpty(), "through-hole breakthrough must participate in flute validation");

    tool.fluteLen = 30;
    tool.totalLen = 10.5;
    const auto total = service.generate({op}, post, options, {});
    check(!total.ok && total.errors.join('\n').contains("total length"),
          "through-hole breakthrough must participate in total-length validation");
    tool.totalLen = 50;
    tool.type = "end_mill";
    tool.fluteLen = 11;
    op.strategyId = mill->id();
    op.params = mill->defaultParams();
    op.params.set("bottomAllowance", 3);
    const auto allowance = service.generate({op}, post, options, {});
    check(!allowance.ok && allowance.errors.join('\n').contains("flute length"),
          "circular milling bottom allowance must participate in flute validation");
    ToolLibrary::instance().addTool(tool);
    OperationEditDialog edit(op);
    edit.accept();
    check(edit.result() != QDialog::Accepted, "editing must reject the same excessive effective depth");

    tool.type = "chamfer_mill";
    tool.fluteLen = 2;
    op.strategyId = chamfer->id();
    op.params = chamfer->defaultParams();
    const auto shallow = service.generate({op}, post, options, {});
    check(shallow.ok, "shallow hole-mouth chamfer must use chamfer depth rather than full hole depth");

    tool.type = "end_mill";
    tool.fluteLen = 30;
    auto params = mill->defaultParams();
    params.set("feedHeight", 4);
    params.set("plungeRate", 123);
    const auto path = mill->generate(hole, tool, params);
    check(path.ok && path.gcode.contains("G0 Z4.000\nG1 Z0.000 F123"),
          "circular milling must feed from the specified clearance to the first layer");
    params.set("feedHeight", 6);
    const auto changed = mill->generate(hole, tool, params);
    check(changed.ok && changed.gcode != path.gcode, "feed height must change real circular milling motion");
    auto explicitMillDepth = mill->defaultParams();
    explicitMillDepth.set("depth", 5.0);
    explicitMillDepth.set("stepDown", 5.0);
    const auto explicitMillPath = mill->generate(hole, tool, explicitMillDepth);
    check(explicitMillPath.ok && explicitMillPath.gcode.contains("Z-5.000") &&
              !explicitMillPath.gcode.contains("Z-10.000"),
          "explicit circular-milling depth 5 must override the recognized model depth 10");
    ToolEntry shortForModel = tool;
    shortForModel.fluteLen = 6.0;
    ProgramGenerationService explicitMillService(
        [mill](const QString &id) -> std::shared_ptr<StrategyBase> {
            return id == mill->id() ? mill : std::shared_ptr<StrategyBase>();
        },
        [shortForModel](int) { return shortForModel; });
    MachiningOperation explicitMillOperation = op;
    explicitMillOperation.strategyId = mill->id();
    explicitMillOperation.params = explicitMillDepth;
    const auto explicitMillProgram = explicitMillService.generate(
        {explicitMillOperation}, post, options, {});
    check(explicitMillProgram.ok,
          "circular-milling reach validation must use explicit depth 5 instead of model depth 10");
    params.set("bottomAllowance", -1);
    check(!mill->generate(hole, tool, params).ok, "negative bottom allowance must not be silently clamped");
    params.set("bottomAllowance", 0);
    params.set("springPasses", 1.5);
    check(!mill->generate(hole, tool, params).ok, "fractional spring passes must be rejected");
    for (const auto &entry : QList<QPair<QString, QVariant>>{{"feedHeight", -1.0}, {"stockToLeave", -1.0},
                                       {"spindleSpeed", 0.0}, {"safeHeight", 1.0},
                                       {"feedHeight", std::numeric_limits<double>::quiet_NaN()}}) {
        auto invalid = mill->defaultParams();
        invalid.values[entry.first] = entry.second;
        const auto rejected = mill->generate(hole, tool, invalid);
        if (rejected.ok) std::cerr << "accepted " << entry.first.toStdString() << '=' << entry.second.toString().toStdString() << '\n';
        check(!rejected.ok && rejected.gcode.isEmpty(),
              "circular milling must reject invalid clearance, stock or spindle parameters before motion");
    }
    for (const QString &key : mill->defaultParams().values.keys()) {
        for (double value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            auto invalid = mill->defaultParams();
            invalid.set(key, value);
            const auto rejected = mill->generate(hole, tool, invalid);
            if (rejected.ok) std::cerr << "accepted nonfinite " << key.toStdString() << '\n';
            check(!rejected.ok && rejected.gcode.isEmpty(), "nonfinite cutting parameters must not produce motion");
        }
    }
    auto raised = hole;
    raised.center.setZ(45);
    check(!mill->generate(raised, tool, mill->defaultParams()).ok,
          "safe absolute Z must clear the actual hole mouth plus feed height");
    auto atClearance = mill->defaultParams();
    atClearance.set("safeHeight", 53);
    check(mill->generate(raised, tool, atClearance).ok, "safe Z equal to clearance must be accepted");
    for (double depth : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()}) {
        auto invalidHole = hole;
        invalidHole.depth = depth;
        check(!mill->generate(invalidHole, tool, mill->defaultParams()).ok, "invalid cutting depth must be rejected");
    }
    auto tooManyLayers = mill->defaultParams();
    tooManyLayers.set("stepDown", 1e-20);
    check(!mill->generate(hole, tool, tooManyLayers).ok, "unrepresentable layer count must be rejected before integer conversion");
    auto invalidTool = tool;
    invalidTool.diameter = std::numeric_limits<double>::quiet_NaN();
    check(!mill->generate(hole, invalidTool, mill->defaultParams()).ok, "nonfinite tool diameter must be rejected");
    auto invalidCenter = hole;
    invalidCenter.center.setX(std::numeric_limits<float>::quiet_NaN());
    check(!mill->generate(QVector<HoleFeature>{hole, invalidCenter}, tool, mill->defaultParams()).ok,
          "all hole geometry must be validated before batch motion");
    for (const QString &key : {QString("feedRate"), QString("plungeRate"), QString("spindleSpeed")}) {
        for (double value : {0.5, double(std::numeric_limits<int>::max()) + 1.0}) {
            auto invalid = mill->defaultParams();
            invalid.set(key, value);
            check(!mill->generate(hole, tool, invalid).ok,
                  "integer speed output must neither truncate to zero nor overflow");
        }
    }
    for (double radius : {std::numeric_limits<double>::infinity(), 1e20}) {
        auto invalidHole = hole;
        invalidHole.radius = radius;
        check(!mill->generate(invalidHole, tool, mill->defaultParams()).ok,
              "nonfinite radius or unrepresentable segment count must be rejected");
    }
    op.strategyId = mill->id();
    op.params = mill->defaultParams();
    op.params.set("stockToLeave", 0.5);
    op.params.set("bottomAllowance", 2);
    op.params.set("feedHeight", 4);
    op.params.set("stepDown", 2);
    options.safeStartBlocks = QStringList{"G17 G40 G49 G80", "G21", "G90", "G54", "G94"};
    const auto finalProgram = service.generate({op}, post, options, {});
    check(finalProgram.ok, "valid circular milling must pass final program generation and safety validation");
    const QString finalCode = finalProgram.snapshot.gcodeText;
    check(finalCode.contains("Z4.000\nG1 Z0.000 F150") && finalCode.contains("M30"),
          "final controller code must retain the clearance feed entry and program end");
    if (argc > 1) {
        QFile output(QString::fromLocal8Bit(argv[1]));
        check(output.open(QIODevice::WriteOnly), "final code evidence file must open");
        output.write(finalCode.toUtf8());
    }
    SimulationController simulation;
    QVector<QVector3D> simulated;
    QVector<bool> rapid;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&](const QVector<QVector3D> &p, const QVector<bool> &r) { simulated = p; rapid = r; });
    simulation.loadGCode(finalCode);
    bool reachedBottom = false, fedFromClearance = false;
    for (int i = 0; i < rapid.size(); ++i) {
        const auto &from = simulated[i];
        const auto &to = simulated[i + 1];
        if (!rapid[i]) {
            check(std::hypot(to.x(), to.y()) <= 1.501 && to.z() >= -12.001,
                  "final-code cutting motion must respect tool-center radius and effective depth");
            reachedBottom |= std::abs(to.z() + 12.0) < 0.001;
            fedFromClearance |= std::abs(from.z() - 4.0) < 0.001 && std::abs(to.z()) < 0.001;
        } else if (std::hypot(to.x() - from.x(), to.y() - from.y()) > 0.001) {
            check(from.z() >= 49.999 && to.z() >= 49.999,
                  "final-code rapid XY motion must stay at safe height");
        }
    }
    check(reachedBottom && fedFromClearance, "final-code simulation must consume allowance and clearance entry");

    for (const QString &id : {QString("hole_peck"), QString("hole_tapping"), QString("hole_spot"), QString("hole_deephole")}) {
        QTableWidget table(3, 2);
        QTabWidget tabs;
        const QStringList keys = {"dwellTime", "pitch", "retractHeight"};
        for (int row = 0; row < keys.size(); ++row) {
            auto *name = new QTableWidgetItem(keys[row]);
            name->setData(Qt::UserRole, keys[row]);
            table.setItem(row, 0, name);
            table.setItem(row, 1, new QTableWidgetItem("1"));
        }
        rebuildOperationParameterGroups(&tabs, &table, id);
        const auto *dwell = tabs.findChild<QDoubleSpinBox *>("operationParameter_dwellTime");
        check(dwell && dwell->suffix() == ((id == "hole_spot" || id == "hole_tapping") ? " ms" : " s"),
              "dwell editor must display the unit actually consumed by its strategy");
        if (id == "hole_peck") check(!tabs.findChild<QDoubleSpinBox *>("operationParameter_retractHeight")->isEnabled(),
                                    "unused peck retract height must not be editable");
        if (id == "hole_tapping") check(!tabs.findChild<QDoubleSpinBox *>("operationParameter_pitch")->isEnabled(),
                                       "tap pitch comes from the tool and must not be editable as a cutting parameter");
    }
    if (ok) std::cout << "Hole parameter consumption, depth compatibility and units passed\n";
    return ok ? 0 : 1;
}
