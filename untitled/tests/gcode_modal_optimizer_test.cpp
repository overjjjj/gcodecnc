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

    for (const QString &cycle : {QStringLiteral("G73"), QStringLiteral("G76"),
                                 QStringLiteral("G81"), QStringLiteral("G83"),
                                 QStringLiteral("G86")}) {
        const QString qWord = cycle == QStringLiteral("G76")
            ? QStringLiteral(" Q0.100")
            : (cycle == QStringLiteral("G73") || cycle == QStringLiteral("G83"))
                ? QStringLiteral(" Q2.000") : QString();
        const QString cycleSource = QStringLiteral(
            "G0 Z25.000\n"
            "G98 %1 Z-5.000 R3.000%2 F40.000 X10.000 Y20.000\n"
            "X30.000 Y20.000\n"
            "G80\n"
            "G0 Z25.000\n")
                .arg(cycle, qWord);
        const QString cycleOptimized = GCodeModalOptimizer::optimize(cycleSource);
        require(cycleOptimized.contains(QStringLiteral("G80\nG0 Z25.000")),
                "G80 must invalidate the pre-cycle motion mode so retract keeps explicit G0");
    }

    QTextStream(stdout) << "PASS gcode_modal_optimizer_test" << Qt::endl;
    return 0;
}
