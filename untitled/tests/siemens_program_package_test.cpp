#include "../src/gcode/SiemensProgramPackage.h"

#include <QCoreApplication>
#include <iostream>

static int expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << "\n";
        return 1;
    }
    return 0;
}

static SiemensProgramSection repeatedHole(double x, double y)
{
    SiemensProgramSection section;
    section.repeatKey = QStringLiteral("cycle81-hole");
    section.preferredSubprogramName = QStringLiteral("hole cycle 81");
    section.parameterDeclarations = QStringList{
        QStringLiteral("REAL PX"),
        QStringLiteral("REAL PY"),
        QStringLiteral("REAL PZ")
    };
    section.subprogramBodyLines = QStringList{
        QStringLiteral("G0 X=PX Y=PY"),
        QStringLiteral("G1 Z=PZ F200"),
        QStringLiteral("G0 Z50")
    };
    section.callArguments = QStringList{
        QString::number(x, 'f', 3),
        QString::number(y, 'f', 3),
        QStringLiteral("-10.000")
    };
    section.inlineLines = QStringList{
        QStringLiteral("G0 X%1 Y%2").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3),
        QStringLiteral("G1 Z-10.000 F200"),
        QStringLiteral("G0 Z50")
    };
    return section;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    SiemensProgramPackageRequest request;
    request.mainProgramName = QStringLiteral("WH250852 main");
    request.mainPreambleLines = QStringList{
        QStringLiteral("G17 G40 G49 G80"),
        QStringLiteral("G21"),
        QStringLiteral("G90"),
        QStringLiteral("G54"),
        QStringLiteral("G94"),
        QStringLiteral("T1 M6"),
        QStringLiteral("S2000 M3"),
        QStringLiteral("M8"),
        QStringLiteral("G0 Z50")
    };
    request.sections.append(repeatedHole(10.0, 20.0));
    request.sections.append(repeatedHole(30.0, 40.0));

    SiemensProgramSection uniqueSlot;
    uniqueSlot.repeatKey = QStringLiteral("unique-slot");
    uniqueSlot.preferredSubprogramName = QStringLiteral("slot finish");
    uniqueSlot.parameterDeclarations = QStringList{QStringLiteral("REAL PX")};
    uniqueSlot.subprogramBodyLines = QStringList{QStringLiteral("G1 X=PX F300")};
    uniqueSlot.callArguments = QStringList{QStringLiteral("75.000")};
    uniqueSlot.inlineLines = QStringList{QStringLiteral("G1 X75.000 F300")};
    request.sections.append(uniqueSlot);
    request.mainPostambleLines = QStringList{
        QStringLiteral("M5"),
        QStringLiteral("M9"),
        QStringLiteral("M30")
    };

    const SiemensProgramPackage package = SiemensProgramPackageBuilder::build(request);
    if (expect(package.ok, "program package should build")) {
        return 1;
    }
    if (expect(package.mainProgram.fileName == QStringLiteral("WH250852_MAIN.MPF"),
               "main program should have a deterministic Siemens-safe MPF name")) {
        return 1;
    }
    if (expect(package.subprograms.size() == 1,
               "only logic repeated at least twice should become an SPF")) {
        return 1;
    }
    const SiemensProgramFile &spf = package.subprograms.first();
    if (expect(spf.fileName == QStringLiteral("SP_HOLE_CYCLE_81.SPF"),
               "SPF name should be deterministic and Siemens-safe")) {
        return 1;
    }
    if (expect(spf.content.startsWith(
                   QStringLiteral("PROC SP_HOLE_CYCLE_81(REAL PX, REAL PY, REAL PZ)\n")),
               "SPF should declare its PROC parameters")) {
        return 1;
    }
    if (expect(spf.content.endsWith(QStringLiteral("RET\n")),
               "SPF should end with RET")) {
        return 1;
    }
    if (expect(package.mainProgram.content.startsWith(
                   QStringLiteral("PROC WH250852_MAIN\n")),
               "MPF should start with a named PROC")) {
        return 1;
    }
    if (expect(package.mainProgram.content.contains(
                   QStringLiteral("SP_HOLE_CYCLE_81(10.000,20.000,-10.000)\n")) &&
                   package.mainProgram.content.contains(
                       QStringLiteral("SP_HOLE_CYCLE_81(30.000,40.000,-10.000)\n")),
               "MPF should call the repeated SPF with deterministic arguments")) {
        return 1;
    }
    if (expect(package.mainProgram.content.contains(QStringLiteral("G1 X75.000 F300\n")) &&
                   !package.mainProgram.content.contains(QStringLiteral("SP_SLOT_FINISH(")),
               "single-use logic should remain inline")) {
        return 1;
    }
    if (expect(package.mainProgram.sha256.size() == 64 && spf.sha256.size() == 64,
               "each package file should have a SHA-256 fingerprint")) {
        return 1;
    }

    const SiemensProgramPackage rebuilt = SiemensProgramPackageBuilder::build(request);
    if (expect(rebuilt.ok &&
                   rebuilt.mainProgram.content == package.mainProgram.content &&
                   rebuilt.mainProgram.sha256 == package.mainProgram.sha256 &&
                   rebuilt.subprograms.first().content == spf.content &&
                   rebuilt.subprograms.first().sha256 == spf.sha256,
               "rebuilding the same request should be byte-for-byte deterministic")) {
        return 1;
    }

    SiemensProgramPackageRequest unsafeRequest = request;
    unsafeRequest.sections[0].subprogramBodyLines.prepend(QStringLiteral("M8"));
    unsafeRequest.sections[1].subprogramBodyLines.prepend(QStringLiteral("M8"));
    const SiemensProgramPackage unsafePackage =
        SiemensProgramPackageBuilder::build(unsafeRequest);
    if (expect(!unsafePackage.ok &&
                   unsafePackage.error.contains(QStringLiteral("SPF")),
               "SPF definitions containing coolant or machine state should be rejected")) {
        return 1;
    }

    return 0;
}
