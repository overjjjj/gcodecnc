#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/postprocessor/FanucPostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/strategies/hole/ChamferStrategy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <iostream>

namespace {
bool expect(bool condition, const char *message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

bool hasWords(const QString &text, const QString &words)
{
    return QRegularExpression(QStringLiteral("(?:^|\\s)")+QRegularExpression::escape(words)+QStringLiteral("(?=\\s|$)")).match(text).hasMatch();
}

QString marker(const QString &code, double q = 0.0, double pitch = 0.0,
               double rpm = 0.0)
{
    return QStringLiteral(
        ";CNEXT_HOLE_CYCLE code=%1 rtp=25 rfp=0.562 sdis=3 x=10 y=20 "
        "z=-4.438 q=%2 p=0 f=40 pitch=%3 rpm=%4 vari=1")
        .arg(code)
        .arg(q, 0, 'f', 3)
        .arg(pitch, 0, 'f', 3)
        .arg(rpm, 0, 'f', 3);
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    PostProcessorOptions options;
    options.programNumber = QStringLiteral("O7302");
    options.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"), QStringLiteral("G21"),
        QStringLiteral("G90"), QStringLiteral("G54"), QStringLiteral("G94")};

    const QStringList fixedCycleMarkers{
        marker(QStringLiteral("G81")),
        marker(QStringLiteral("G73"), 2.0),
        marker(QStringLiteral("G83"), 2.0),
        marker(QStringLiteral("G84"), 0.0, 1.0, 600.0),
        marker(QStringLiteral("G85")),
    };
    FanucPostProcessor fanuc;
    Cq8PostProcessor cq8;
    SiemensPostProcessor siemens;
    const QString fanucCode = fanuc.wrapGCode(fixedCycleMarkers, options);
    const QString cq8Code = cq8.wrapGCode(fixedCycleMarkers, options);
    const QString siemensCode = siemens.wrapGCode(fixedCycleMarkers, options);

    if (!expect(hasWords(fanucCode, QStringLiteral("G81 Z-4.438000 R3.562000")) &&
                    hasWords(fanucCode, QStringLiteral("G73 Z-4.438000 R3.562000 Q2.000000")) &&
                    hasWords(fanucCode, QStringLiteral("G83 Z-4.438000 R3.562000 Q2.000000")) &&
                    hasWords(fanucCode, QStringLiteral("G84 Z-4.438000 R3.562000 F600.000")) &&
                    hasWords(fanucCode, QStringLiteral("G85 Z-4.438000 R3.562000 F40.000")),
                "Fanuc postprocessor must translate the supported fixed-cycle matrix") ||
        !expect(cq8Code.contains(QStringLiteral("Post-processor: CQ8 Macro B")) &&
                    hasWords(cq8Code, QStringLiteral("G81 Z-4.438000 R3.562000")) &&
                    hasWords(cq8Code, QStringLiteral("G83 Z-4.438000 R3.562000 Q2.000000")) &&
                    hasWords(cq8Code, QStringLiteral("G84 Z-4.438000 R3.562000 F600.000")) &&
                    hasWords(cq8Code, QStringLiteral("G85 Z-4.438000 R3.562000 F40.000")),
                "CQ8 export must retain its documented ISO/Fanuc-style fixed-cycle syntax") ||
        !expect(siemensCode.contains(QStringLiteral("CYCLE81(")) &&
                    siemensCode.count(QStringLiteral("CYCLE83(")) == 2 &&
                    siemensCode.contains(QStringLiteral("CYCLE84(")) &&
                    siemensCode.contains(QStringLiteral("CYCLE85(")) &&
                    !siemensCode.contains(QStringLiteral("G73")) &&
                    !siemensCode.contains(QStringLiteral("G83")) &&
                    !siemensCode.contains(QStringLiteral("G84")) &&
                    !siemensCode.contains(QStringLiteral("G85")),
                "Siemens postprocessor must translate supported cycles instead of leaking Fanuc codes")) {
        return 1;
    }

    HoleFeature firstHole;
    firstHole.center = QVector3D(10.0f, 20.0f, -10.0f);
    firstHole.radius = 5.0;
    firstHole.depth = 20.0;
    firstHole.region = FaceRegion::Front;
    HoleFeature secondHole = firstHole;
    secondHole.center.setX(30.0f);
    ToolEntry countersink;
    countersink.id = 95;
    countersink.type = QStringLiteral("chamfer_mill");
    countersink.diameter = 6.0;
    countersink.fluteLen = 12.0;
    countersink.totalLen = 42.0;
    countersink.extra.insert(QStringLiteral("angle"), 90.0);
    ChamferStrategy chamfer;
    StrategyParams axial = chamfer.defaultParams();
    axial.set(QStringLiteral("chamferMode"), 1.0);
    axial.set(QStringLiteral("axialDepth"), 2.0);
    axial.set(QStringLiteral("safeHeight"), 25.0);
    axial.set(QStringLiteral("feedHeight"), 3.0);
    axial.set(QStringLiteral("feedRate"), 40.0);
    const ToolpathResult axialPath = chamfer.generate(
        QVector<HoleFeature>{firstHole, secondHole}, countersink, axial);
    const QStringList axialBlocks = axialPath.gcode.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QString fanucAxial = fanuc.wrapGCode(axialBlocks, options);
    const QString cq8Axial = cq8.wrapGCode(axialBlocks, options);
    const QString siemensAxial = siemens.wrapGCode(axialBlocks, options);
    if (!expect(axialPath.ok && axialPath.gcode.count(QStringLiteral("code=G81")) == 2 &&
                    !axialPath.gcode.contains(QStringLiteral("G2 ")),
                "axial chamfer strategy must supply two G81 markers and no circular interpolation") ||
        !expect(hasWords(fanucAxial, QStringLiteral("G98 G81 Z-2.000000 R3.000000")) &&
                    hasWords(fanucAxial, QStringLiteral("X30.000000 Y20.000000")) &&
                    fanucAxial.contains(QStringLiteral("G80\nG0 Z25.000000")),
                "Fanuc must translate the real axial countersink strategy to a safe G81 cycle") ||
        !expect(hasWords(cq8Axial, QStringLiteral("G98 G81 Z-2.000000 R3.000000")) &&
                    cq8Axial.contains(QStringLiteral("G80\nG0 Z25.000000")),
                "CQ8 must retain the confirmed ISO/Fanuc-style axial G81 output") ||
        !expect(siemensAxial.contains(QStringLiteral("CYCLE81(")) &&
                    !hasWords(siemensAxial, QStringLiteral("G98 G81")) &&
                    !siemensAxial.contains(QStringLiteral("CNEXT_HOLE_CYCLE")) &&
                    hasWords(siemensAxial, QStringLiteral("G0 Z25.000000")),
                "Siemens must map the real axial countersink strategy to CYCLE81")) {
        return 1;
    }
    if (argc > 1) {
        const QString outputDir = QString::fromLocal8Bit(argv[1]);
        QDir().mkpath(outputDir);
        const QList<QPair<QString, QString>> artifacts{
            {QStringLiteral("fanuc-axial-g81-two-hole.nc"), fanucAxial},
            {QStringLiteral("cq8-axial-g81-two-hole.nc"), cq8Axial},
            {QStringLiteral("siemens-axial-cycle81-two-hole.mpf"), siemensAxial},
        };
        for (const auto &artifact : artifacts) {
            QFile file(QDir(outputDir).filePath(artifact.first));
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text) ||
                file.write(artifact.second.toUtf8()) <= 0) {
                std::cerr << "failed to write postprocessor evidence artifact\n";
                return 1;
            }
        }
    }

    const QStringList millingBlocks{
        QStringLiteral("T7 M6"), QStringLiteral("S600 M3"), QStringLiteral("G0 Z25.000000"),
        QStringLiteral("M8"), QStringLiteral("G1 X12.000000 Y20.000000 Z-1.000000 F40"),
        QStringLiteral("G2 X12.000000 Y20.000000 I-3.000 J0.000 F40"),
        QStringLiteral("G0 Z25.000000")};
    for (const QString &code : {fanuc.wrapGCode(millingBlocks, options),
                                cq8.wrapGCode(millingBlocks, options),
                                siemens.wrapGCode(millingBlocks, options)}) {
        if (!expect(code.contains(QStringLiteral("G2 X12.000000 Y20.000000 I-3.000 J0.000 F40")) &&
                        code.contains(QStringLiteral("M8")) && code.contains(QStringLiteral("M9")) &&
                        code.contains(QStringLiteral("M30")),
                    "all three posts must retain verified ISO circular milling and safe shutdown")) {
            return 1;
        }
    }

    std::cout << "Hole postprocessor capability matrix passed\n";
    return 0;
}
