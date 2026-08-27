#include "../src/tool/ToolLibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    ToolLibrary &library = ToolLibrary::instance();
    require(!library.toolsByType(QStringLiteral("ball_end_mill")).isEmpty(),
            "default tool library must expose a ball end mill for verified slope milling");

    ToolEntry metadataTool;
    metadataTool.name = QStringLiteral("metadata chamfer tool");
    metadataTool.type = QStringLiteral("chamfer_mill");
    metadataTool.diameter = 10.0;
    metadataTool.extra.insert(QStringLiteral("includedAngle"), 90.0);
    metadataTool.extra.insert(QStringLiteral("tipRadius"), 0.2);
    library.addTool(metadataTool);

    const QString path = QDir::temp().filePath(
        QStringLiteral("cnext_tool_library_metadata_test.json"));
    QFile::remove(path);
    require(library.saveToFile(path), "tool library metadata fixture should save");
    require(library.loadFromFile(path), "tool library metadata fixture should reload");

    bool restored = false;
    for (const ToolEntry &tool : library.allTools()) {
        if (tool.name == metadataTool.name) {
            restored = tool.extra.value(QStringLiteral("includedAngle")).toDouble() == 90.0 &&
                       tool.extra.value(QStringLiteral("tipRadius")).toDouble() == 0.2;
        }
    }
    QFile::remove(path);
    require(restored, "tool geometry metadata must survive save and reload");
    return 0;
}
