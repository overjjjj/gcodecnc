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
    return 0;
}
