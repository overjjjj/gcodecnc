#include "../src/gcode/GCodeModalOptimizer.h"

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

    const QString source = QStringLiteral(
        "G0 Z50.000\n"
        "G0 X10.000 Y20.000\n"
        "G1 Z-2.000 F100.000\n"
        "G1 X20.000 Y20.000 F100.000\n"
        "G1 X30.000 Y20.000 F100.000\n"
        "G0 Z50.000\n");

    const QString optimized = GCodeModalOptimizer::optimize(source);
    const QString expected = QStringLiteral(
        "G0 Z50.000\n"
        "X10.000 Y20.000\n"
        "G1 Z-2.000 F100.000\n"
        "X20.000 Y20.000\n"
        "X30.000 Y20.000\n"
        "G0 Z50.000\n");
    require(optimized == expected,
            "repeated G0/G1 motion and unchanged feed should be omitted modally");

    const QString arcSource = QStringLiteral(
        "G1 X10.000 F100.000\n"
        "G2 X20.000 Y10.000 I5.000 J0.000 F100.000\n"
        "G2 X30.000 Y10.000 I5.000 J0.000 F120.000\n");
    const QString arcOptimized = GCodeModalOptimizer::optimize(arcSource);
    require(arcOptimized == QStringLiteral(
                "G1 X10.000 F100.000\n"
                "G2 X20.000 Y10.000 I5.000 J0.000\n"
                "X30.000 Y10.000 I5.000 J0.000 F120.000\n"),
            "arc mode should remain explicit once and feed should change only when needed");

    const QString protectedSource = QStringLiteral(
        "G17 G40 G49 G80\n"
        "G1 X10.000 F100.000\n"
        "G40 G1 X20.000 F100.000\n");
    require(GCodeModalOptimizer::optimize(protectedSource) == protectedSource,
            "safety and cutter-compensation lines must remain untouched");

    QTextStream(stdout) << "PASS gcode_modal_optimizer_test" << Qt::endl;
    return 0;
}
