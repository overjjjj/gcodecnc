#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/postprocessor/FanucPostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"

#include <QCoreApplication>

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

QString marker(const QString &code, double dwell = 0.0)
{
    return QStringLiteral(
        ";CNEXT_HOLE_CYCLE code=%1 rtp=8 rfp=0 sdis=2 x=10 y=20 "
        "z=-12 q=2 p=%2 f=120 vari=0\nG0 Z8")
        .arg(code)
        .arg(dwell);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    PostProcessorOptions options;
    options.addComments = false;

    const QString fanucG73 = FanucPostProcessor().wrapGCode(
        {marker(QStringLiteral("G73"))}, options);
    const QString cq8G86 = Cq8PostProcessor().wrapGCode(
        {marker(QStringLiteral("G86"), 500.0)}, options);
    const QString siemensG73 = SiemensPostProcessor().wrapGCode(
        {marker(QStringLiteral("G73"))}, options);
    const QString siemensG86 = SiemensPostProcessor().wrapGCode(
        {marker(QStringLiteral("G86"))}, options);

    if (!expect(fanucG73.contains(QStringLiteral("G73 Z-12.000 R2.000 Q2.000 F120.000")),
                "Fanuc G73 should retain high-speed peck parameters") ||
        !expect(cq8G86.contains(QStringLiteral("G86 Z-12.000 R2.000 P500 F120.000")),
                "CQ8 G86 should retain boring depth, return plane, dwell and feed") ||
        !expect(siemensG73.contains(QStringLiteral("MCALL CYCLE83(")) &&
                    siemensG73.contains(QStringLiteral(",0,,2.000,)")),
                "Siemens G73 should map to chip-break CYCLE83") ||
        !expect(siemensG86.contains(QStringLiteral("MCALL CYCLE86(")),
                "Siemens G86 should map to its boring cycle")) {
        return 1;
    }

    return 0;
}
