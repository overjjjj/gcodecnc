#include "../src/workspaces/program_verification/safety/GCodeSafetyValidator.h"
#include <QCoreApplication>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString base = "G17 G40 G49 G80\nG21\nG90\nG54\nT1 M6\nS600 M3\n"
                         "G0 Z50\nG0 X0 Y0\nG1 Z-1 F40\nG0 Z50\nM5\nM9\nM30\n";
    int failures = 0;
    auto check = [&](const QString &text, bool expected, const char *label) {
        const auto report = GCodeSafetyValidator::validate(text);
        if (report.ok != expected) {
            ++failures; std::cerr << label << ": " << report.messages.join(" | ").toStdString() << '\n';
        }
    };
    check(base, true, "valid baseline");
    for (const QString &start : QStringList{"S0 M3", "S0 M4", "S.5 M3", "S-1 M3", "M3", "M4",
                                           "S600 S0 M3", "SNaN M3", "S1e309 M3"}) {
        QString text = base; text.replace("S600 M3", start);
        check(text, false, qPrintable("invalid start " + start));
    }
    for (const QString &start : QStringList{"S600\nM3", "S600\nM4", "M3 S600", "S600 M04", "S600. M03"}) {
        QString text = base; text.replace("S600 M3", start);
        check(text, true, qPrintable("modal start " + start));
    }
    for (const QString &change : QStringList{"S0\n", "M5\n", "M05\n", "M5\nS0\nM3\n"}) {
        QString text = base; text.replace("G1 Z-1", change + "G1 Z-1");
        check(text, false, qPrintable("cutting requires active positive speed " + change));
    }
    QString restarted = base; restarted.replace("G1 Z-1", "M5\nM4\nG1 Z-1");
    check(restarted, true, "M5 preserves modal speed for explicit restart");
    QString stoppedZero = base; stoppedZero.replace("M5\nM9", "M5\nS0\nM9");
    check(stoppedZero, true, "zero reset after stop without cutting");
    QString fixed = base; fixed.replace("G1 Z-1 F40", "G81 X0 Y0 Z-1 R3 F40\nG80");
    check(fixed, true, "valid fixed cycle");
    fixed.replace("G81", "M5\nG81");
    check(fixed, false, "fixed cycle after stop");
    QString modal = base; modal.replace("G1 Z-1 F40", "G81 X0 Y0 Z-1 R3 F40\nM5\nX10 Y0\nG80");
    check(modal, false, "modal fixed cycle after stop");
    for (const QString &change : QStringList{"N10S0\n", "N10M5\n", "N10M05\n"}) {
        QString text = base; text.replace("G1 Z-1", change + "G1 Z-1");
        check(text, false, qPrintable("compact modal change " + change));
    }
    QString compact = base; compact.replace("G1 Z-1 F40", "G1X0Y0Z-1S0F40");
    check(compact, false, "compact cutting with zero speed");
    compact = base; compact.replace("G1 Z-1 F40", "M5\nG1X.5Y.5Z-1F40");
    check(compact, false, "compact cutting after stop");
    compact = base; compact.replace("S600 M3", "N10S600M3");
    check(compact, true, "valid compact speed/start");
    compact = base; compact.replace("G1 Z-1 F40", "G1 Z-1 F40 (S0 M5) ; S0 M5");
    check(compact, true, "ignore spindle words in comments");
    for (const QString &commands : QStringList{"S600 M3 M5", "S600 M4 M3", "S600 M5 M3"}) {
        compact = base; compact.replace("S600 M3", commands);
        check(compact, false, "conflicting spindle commands");
    }
    QString mcall = base; mcall.replace("G1 Z-1 F40", "MCALL CYCLE81(50,0,3,-1)\nM5\nX10 Y0\nMCALL");
    check(mcall, false, "Siemens modal cycle after stop");
    mcall = base; mcall.replace("G1 Z-1 F40", "MCALL CYCLE81(50,0,3,-1)\nX10 Y0\nMCALL");
    check(mcall, true, "Siemens modal cycle with running spindle");
    for (const QString &stop : QStringList{"M 5", "M 05", "M005", "M5.0", "N10M 005.0"}) {
        QString text = base; text.replace("G1 Z-1", stop + "\nG1 Z-1");
        check(text, false, qPrintable("numeric equivalent stop " + stop));
    }
    QString spaced = base; spaced.replace("S600 M3", "N 10 S 600 M 003.0");
    check(spaced, true, "numeric equivalent start");
    spaced = base; spaced.replace("G1 Z-1 F40", "M 5\nG 1 X .5 F 40");
    check(spaced, false, "spaced feed after stop");
    std::cout << "FAILURES=" << failures << std::endl;
    return failures ? 1 : 0;
}
