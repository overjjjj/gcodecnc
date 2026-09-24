#include "../src/core/ProjectManager.h"
#include "../src/gcode/ProgramSnapshotStatus.h"
#include "../src/tool/ToolEntry.h"
#include <QCoreApplication>
#include <QHash>
#include <QTemporaryDir>
#include <iostream>

static bool expect(bool value, const char *message)
{
    if (!value) std::cerr << message << '\n';
    return value;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ToolEntry tool;
    tool.id = 1;
    tool.type = QStringLiteral("end_mill");
    tool.diameter = 6;
    QHash<int, ToolEntry> tools{{1, tool}};
    const auto lookup = [&tools](int id) { return tools.value(id); };
    MachiningOperation operation;
    operation.id = QStringLiteral("op-1");
    operation.toolId = 1;
    operation.markToolpathValid();
    const QList<MachiningOperation> operations{operation};
    const QStringList ids{operation.id};
    ProgramEntry program;
    program.id = QStringLiteral("program-1");
    program.sourceOperationIds = ids;
    program.sourceOperationFingerprint = ProgramSnapshotFingerprint::calculate(operations, ids);
    program.toolFingerprint = ProgramSnapshotFingerprint::calculateTools(operations, ids, lookup);
    const auto status = [&]() {
        return ProgramSnapshotStatus::evaluate(operations, ids, program.sourceOperationFingerprint,
            {}, {}, ProgramSnapshotFingerprint::calculateTools(operations, ids, lookup),
            program.toolFingerprint);
    };
    if (!expect(!program.toolFingerprint.isEmpty() && status().okForOutput(),
                "matching tool definitions must allow current snapshot")) return 1;
    tools[2] = tool;
    tools[2].id = 2;
    tools[2].diameter = 30;
    if (!expect(status().okForOutput(), "unused tool changes must not stale this program")) return 1;
    tools[1].diameter = 10;
    if (!expect(!status().okForOutput(), "same ID with changed diameter must block old program")) return 1;
    tools[1] = tool;
    tools[1].type = QStringLiteral("drill");
    if (!expect(!status().okForOutput(), "same ID with changed type must block old program")) return 1;
    tools.remove(1);
    if (!expect(!status().okForOutput(), "deleted tool must block old program")) return 1;
    tools[1] = tool;
    tools[1].name = QStringLiteral("renamed");
    if (!expect(status().okForOutput(), "renaming alone must not change machining evidence")) return 1;

    ProjectManager project;
    project.setOperations(operations);
    project.setPrograms({program});
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("tools.cnext"));
    ProjectManager loaded;
    if (!expect(dir.isValid() && project.saveToFile(path) && loaded.loadFromFile(path),
                "tool snapshot project should round-trip")) return 1;
    program = loaded.programById(program.id);
    if (!expect(status().okForOutput(), "loaded snapshot must preserve tool evidence")) return 1;
    tools[1].diameter = 10;
    if (!expect(!status().okForOutput(), "reopened snapshot must detect changed tool")) return 1;
    program.toolFingerprint = ProgramSnapshotFingerprint::calculateTools(operations, ids, lookup);
    if (!expect(status().okForOutput(), "new generation evidence must restore output")) return 1;
    program.toolFingerprint.clear();
    if (!expect(!status().okForOutput(), "legacy snapshot without tool evidence must be rejected")) return 1;

    MachiningOperation other = operation;
    other.id = QStringLiteral("op-2");
    other.toolId = 2;
    project.setOperations({operation, other});
    int changes = 0;
    QObject::connect(&project, &ProjectManager::operationsChanged, [&]() { ++changes; });
    project.invalidateToolOperations({1});
    if (!expect(project.operations()[0].toolpathState == ToolpathState::Stale &&
                project.operations()[1].toolpathState == ToolpathState::Valid && changes == 1,
                "tool change must invalidate only dependent operations in one update")) return 1;
    project.invalidateToolOperations({99});
    if (!expect(changes == 1, "unreferenced tool must not update operations")) return 1;

    // A saved legacy program can retain Valid sources and matching evidence after a strategy is prohibited.
    auto legacy = operation;
    legacy.id = "legacy-linear-slot"; legacy.opType = OperationType::Contour;
    legacy.strategyId = "mill_tapered_slot";
    legacy.contourFeature.length = 40; legacy.contourFeature.width = 12; legacy.contourFeature.depth = 4;
    legacy.params.set("depth", 4); legacy.markToolpathValid();
    int rejected = 0;
    for (double direction : {-1., 1.}) for (double angle : {0., 45., 90.}) {
        legacy.params.set("slopeDirection", direction); legacy.contourFeature.angle = angle;
        ProjectManager oldProject, reopened;
        oldProject.setOperations({legacy});
        ProgramEntry oldProgram;
        oldProgram.id = "legacy-program"; oldProgram.sourceOperationIds = QStringList{legacy.id};
        oldProgram.sourceOperationFingerprint = ProgramSnapshotFingerprint::calculate({legacy}, oldProgram.sourceOperationIds);
        oldProgram.toolFingerprint = ProgramSnapshotFingerprint::calculateTools({legacy}, oldProgram.sourceOperationIds, lookup);
        oldProgram.setupFingerprint = oldProject.setupFingerprint();
        oldProgram.gcodeText = "G17 G40 G49 G80\nG21 G90 G54 G94\nG0 Z50\nG1 X0 Y0 Z-2 F800\nG0 Z50\nM30\n";
        oldProject.setPrograms({oldProgram});
        const QString legacyPath = dir.filePath("legacy-linear.cnext");
        if (!expect(oldProject.saveToFile(legacyPath) && reopened.loadFromFile(legacyPath), "legacy snapshot must round-trip")) return 1;
        const auto savedProgram = reopened.programById(oldProgram.id);
        const auto savedOperations = reopened.operations();
        const auto fingerprint = ProgramSnapshotFingerprint::calculate(savedOperations, savedProgram.sourceOperationIds);
        if (!expect(savedOperations[0].toolpathState == ToolpathState::Valid &&
                    fingerprint == savedProgram.sourceOperationFingerprint && savedProgram.gcodeText == oldProgram.gcodeText,
                    "legacy fixture must retain Valid, identical fingerprints and original code")) return 1;
        const auto blocked = ProgramSnapshotStatus::evaluate(savedOperations, savedProgram.sourceOperationIds,
            savedProgram.sourceOperationFingerprint, reopened.setupFingerprint(), savedProgram.setupFingerprint,
            ProgramSnapshotFingerprint::calculateTools(savedOperations, savedProgram.sourceOperationIds, lookup), savedProgram.toolFingerprint);
        if (blocked.state == ProgramSnapshotState::Changed && !blocked.okForOutput() &&
            blocked.message.contains("cutter", Qt::CaseInsensitive)) ++rejected;
        auto supported = legacy; supported.contourFeature.slopeMinWidth = .4;
        const auto supportedFingerprint = ProgramSnapshotFingerprint::calculate({supported}, savedProgram.sourceOperationIds);
        const auto widthStatus = ProgramSnapshotStatus::evaluate({supported}, savedProgram.sourceOperationIds,
            supportedFingerprint, {}, {}, savedProgram.toolFingerprint, savedProgram.toolFingerprint);
        if (!expect(widthStatus.okForOutput(), "linear floor block must not prohibit the separate width-profile mode")) return 1;
        oldProject.saveToFile("../outputs/slot-batch2-r1/legacy-linear-slot.cnext");
    }
    if (!expect(rejected == 6, "saved Valid linear-floor snapshots with matching fingerprints must block export/send")) return 1;
    std::cout << "Legacy output gate: 6/6 saved Valid snapshots blocked; code retained; width-profile compatible\n";
    return 0;
}
