#include "../src/gcode/Cq8MacroProgramBuilder.h"

#include <QCoreApplication>
#include <QTextStream>

#include <cstdlib>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << Qt::endl;
        std::exit(1);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    ParametricToolpathProgram layer;
    layer.routineName = QStringLiteral("IRREGULAR_POCKET_LAYER");
    layer.parameterNames = QStringList{QStringLiteral("DEPTH_Z")};
    layer.bodyTemplateLines = QStringList{
        QStringLiteral("G1 Z${DEPTH_Z} F100.000"),
        QStringLiteral("G1 X20.000 Y20.000 F100.000")};
    ParametricToolpathCall first;
    first.arguments.insert(QStringLiteral("DEPTH_Z"), QStringLiteral("-2.000"));
    ParametricToolpathCall second;
    second.arguments.insert(QStringLiteral("DEPTH_Z"), QStringLiteral("-4.000"));
    layer.calls = {first, second};

    const Cq8MacroProgram macro = Cq8MacroProgramBuilder::build({layer});
    require(macro.ok, "a valid parametric layer should build a CQ8 macro library");
    require(macro.callText == QStringLiteral("#100=-2.000\nM98 P9001\n#100=-4.000\nM98 P9001"),
            "every layer call should assign its parameter before the Fanuc-style call");
    require(macro.libraryText == QStringLiteral(
                "O9001\n"
                "; CNEXT ROUTINE: IRREGULAR_POCKET_LAYER\n"
                "G1 Z#100 F100.000\n"
                "G1 X20.000 Y20.000 F100.000\n"
                "M99\n"),
            "routine body should use the assigned variable and terminate with M99");

    QTextStream(stdout) << "PASS cq8_macro_program_builder_test" << Qt::endl;
    return 0;
}
