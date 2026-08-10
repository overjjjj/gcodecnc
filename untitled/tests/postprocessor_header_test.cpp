#include "../src/postprocessor/FanucPostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <iostream>

static int expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << "\n";
        return 1;
    }
    return 0;
}

static QStringList body()
{
    return {
        QStringLiteral("T1 M6"),
        QStringLiteral("S2000 M3"),
        QStringLiteral("G0 Z50.000"),
        QStringLiteral("M5"),
        QStringLiteral("M9")
    };
}

static QStringList twoToolBody()
{
    return {
        QStringLiteral("T1 M6"),
        QStringLiteral("S2000 M3"),
        QStringLiteral("M8"),
        QStringLiteral("G0 Z50.000"),
        QStringLiteral("G1 Z-1.000 F100"),
        QStringLiteral("G0 Z50.000"),
        QStringLiteral("T2 M6"),
        QStringLiteral("S2500 M3"),
        QStringLiteral("G0 Z50.000")
    };
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    PostProcessorOptions options;
    options.addComments = false;
    options.useAbsoluteCoords = false;
    options.workOffset = QStringLiteral("G56");
    options.safeStartBlocks = QStringList{
        QStringLiteral("G17 G40 G49 G80"),
        QStringLiteral("G21"),
        QStringLiteral("G91"),
        QStringLiteral("G54"),
        QStringLiteral("G94")
    };

    const SiemensPostProcessor siemens;
    const QString siemensProgram = siemens.wrapGCode(body(), options);
    if (expect(siemensProgram.contains(QStringLiteral("\nG21\n")),
               "Siemens postprocessor should emit G21 units mode")) {
        return 1;
    }
    if (expect(siemensProgram.contains(QStringLiteral("\nG90\n"))
                   && !siemensProgram.contains(QStringLiteral("\nG91\n")),
               "Siemens postprocessor should enforce absolute coordinates")) {
        return 1;
    }
    if (expect(siemensProgram.contains(QStringLiteral("\nG56\n"))
                   && !siemensProgram.contains(QStringLiteral("\nG54\n")),
               "Siemens postprocessor should apply the selected WCS to the safe-start template")) {
        return 1;
    }
    const QString siemensTwoToolProgram = siemens.wrapGCode(twoToolBody(), options);
    if (expect(siemensTwoToolProgram.contains(
                   QStringLiteral("G0 Z50.000\nM5\nM9\nT2 M6")),
               "Siemens postprocessor should stop spindle and coolant before the second tool change")) {
        return 1;
    }

    const FanucPostProcessor fanuc;
    const QString fanucProgram = fanuc.wrapGCode(body(), options);
    if (expect(fanucProgram.contains(QStringLiteral("\nG21\n")),
               "Fanuc postprocessor should emit G21 units mode")) {
        return 1;
    }
    if (expect(fanucProgram.contains(QStringLiteral("\nG90\n"))
                   && !fanucProgram.contains(QStringLiteral("\nG91\n")),
               "Fanuc postprocessor should enforce absolute coordinates")) {
        return 1;
    }
    if (expect(fanucProgram.contains(QStringLiteral("\nG56\n"))
                   && !fanucProgram.contains(QStringLiteral("\nG54\n")),
               "Fanuc postprocessor should apply the selected WCS to the safe-start template")) {
        return 1;
    }
    const QString fanucTwoToolProgram = fanuc.wrapGCode(twoToolBody(), options);
    if (expect(fanucTwoToolProgram.contains(
                   QStringLiteral("G0 Z50.000\nM5\nM9\nT2 M6")),
               "Fanuc postprocessor should stop spindle and coolant before the second tool change")) {
        return 1;
    }

    return 0;
}
