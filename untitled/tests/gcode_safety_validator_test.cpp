#include "../src/gcode/GCodeSafetyValidator.h"

#include <QCoreApplication>
#include <QString>
#include <iostream>

static int expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << "\n";
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const QString validProgram =
        "G17 G40 G49 G80\n"
        "G21\n"
        "G90\n"
        "G54\n"
        "T1 M6\n"
        "S2000 M3\n"
        "G0 Z50.000\n"
        "G0 X0.000 Y0.000\n"
        "G1 Z-1.000 F100\n"
        "G0 Z50.000\n"
        "M5\n"
        "M9\n"
        "M30\n";

    const GCodeSafetyReport validReport = GCodeSafetyValidator::validate(validProgram);
    if (expect(validReport.ok, "valid program should pass")) {
        return 1;
    }

    const GCodeSafetyReport missingEndReport =
        GCodeSafetyValidator::validate(validProgram.section(QStringLiteral("M30"), 0, 0));
    if (expect(!missingEndReport.ok, "program without M30 should fail")) {
        return 1;
    }
    if (expect(missingEndReport.messages.join('\n').contains(QStringLiteral("M30")),
               "missing M30 report should mention M30")) {
        return 1;
    }

    QString missingUnitsProgram = validProgram;
    missingUnitsProgram.remove(QStringLiteral("G21\n"));
    const GCodeSafetyReport missingUnitsReport =
        GCodeSafetyValidator::validate(missingUnitsProgram);
    if (expect(!missingUnitsReport.ok, "program without G21/G20 should fail")) {
        return 1;
    }
    if (expect(missingUnitsReport.messages.join('\n').contains(QStringLiteral("G21/G20")),
               "missing units report should mention G21/G20")) {
        return 1;
    }

    QString incrementalProgram = validProgram;
    incrementalProgram.replace(QStringLiteral("G90\n"), QStringLiteral("G90\nG91\n"));
    const GCodeSafetyReport incrementalReport =
        GCodeSafetyValidator::validate(incrementalProgram);
    if (expect(!incrementalReport.ok, "program containing G91 should fail")) {
        return 1;
    }
    if (expect(incrementalReport.messages.join('\n').contains(QStringLiteral("G91")),
               "incremental-mode report should mention G91")) {
        return 1;
    }

    QString combinedSafeRapid = validProgram;
    combinedSafeRapid.replace(QStringLiteral("G0 Z50.000\nM5"),
                              QStringLiteral("G1 Z-1.000 F100\nG0 X10.000 Y0.000 Z50.000\nM5"));
    const GCodeSafetyReport combinedSafeReport =
        GCodeSafetyValidator::validate(combinedSafeRapid);
    if (expect(combinedSafeReport.ok,
               "combined rapid XY move to a safe destination Z should pass")) {
        return 1;
    }

    QString combinedUnsafeRapid = validProgram;
    combinedUnsafeRapid.replace(QStringLiteral("G0 X0.000 Y0.000\n"),
                                QStringLiteral("G0 X10.000 Y0.000 Z-2.000\n"));
    const GCodeSafetyReport combinedUnsafeReport =
        GCodeSafetyValidator::validate(combinedUnsafeRapid);
    if (expect(!combinedUnsafeReport.ok,
               "combined rapid XY move to a cutting destination Z should fail")) {
        return 1;
    }
    if (expect(combinedUnsafeReport.messages.join('\n').contains(QStringLiteral("G0 X/Y")),
               "combined unsafe rapid report should mention G0 X/Y")) {
        return 1;
    }

    QString modalUnsafeRapidProgram = validProgram;
    modalUnsafeRapidProgram.replace(
        QStringLiteral("G0 X0.000 Y0.000\n"),
        QStringLiteral("G0 Z-2.000\nX10.000 Y0.000\n"));
    const GCodeSafetyReport modalUnsafeRapidReport =
        GCodeSafetyValidator::validate(modalUnsafeRapidProgram);
    if (expect(!modalUnsafeRapidReport.ok,
               "modal G0 XY move at cutting Z should fail")) {
        return 1;
    }
    if (expect(modalUnsafeRapidReport.messages.join('\n').contains(QStringLiteral("G0 X/Y")),
               "modal unsafe rapid report should mention G0 X/Y")) {
        return 1;
    }

    QString unknownZRapidProgram = validProgram;
    unknownZRapidProgram.replace(
        QStringLiteral("G0 Z50.000\nG0 X0.000 Y0.000\n"),
        QStringLiteral("G0 X0.000 Y0.000\nG0 Z50.000\n"));
    const GCodeSafetyReport unknownZRapidReport =
        GCodeSafetyValidator::validate(unknownZRapidProgram);
    if (expect(!unknownZRapidReport.ok,
               "rapid XY before an explicit safe Z should fail")) {
        return 1;
    }
    if (expect(unknownZRapidReport.messages.join('\n').contains(QStringLiteral("safe Z")),
               "unknown-Z rapid report should mention safe Z")) {
        return 1;
    }

    QString wrongShutdownOrder = validProgram;
    wrongShutdownOrder.replace(QStringLiteral("M5\nM9\nM30\n"),
                               QStringLiteral("M30\nM5\nM9\n"));
    const GCodeSafetyReport wrongShutdownOrderReport =
        GCodeSafetyValidator::validate(wrongShutdownOrder);
    if (expect(!wrongShutdownOrderReport.ok,
               "M30 before M5/M9 should fail")) {
        return 1;
    }
    if (expect(wrongShutdownOrderReport.messages.join('\n').contains(QStringLiteral("M30")),
               "wrong shutdown order report should mention M30")) {
        return 1;
    }

    QString activeCutterCompProgram = validProgram;
    activeCutterCompProgram.replace(
        QStringLiteral("M5\n"),
        QStringLiteral("G1 G41 X1.000 Y0.000 F100\nG1 X2.000 Y0.000\nM5\n"));
    const GCodeSafetyReport activeCutterCompReport =
        GCodeSafetyValidator::validate(activeCutterCompProgram);
    if (expect(!activeCutterCompReport.ok,
               "active cutter compensation at program end should fail")) {
        return 1;
    }
    if (expect(activeCutterCompReport.messages.join('\n').contains(QStringLiteral("G40")),
               "active cutter compensation report should mention G40")) {
        return 1;
    }

    QString activeCutterCompToolChangeProgram = validProgram;
    activeCutterCompToolChangeProgram.replace(
        QStringLiteral("M5\n"),
        QStringLiteral("G1 G41 X1.000 Y0.000 F100\nT2 M6\nG1 G40 X2.000 Y0.000\nM5\n"));
    const GCodeSafetyReport activeCutterCompToolChangeReport =
        GCodeSafetyValidator::validate(activeCutterCompToolChangeProgram);
    if (expect(!activeCutterCompToolChangeReport.ok,
               "tool change with active cutter compensation should fail")) {
        return 1;
    }
    if (expect(activeCutterCompToolChangeReport.messages.join('\n')
                   .contains(QStringLiteral("before tool change")),
               "active cutter compensation tool-change report should explain G40 ordering")) {
        return 1;
    }

    QString validCutterCompProgram = validProgram;
    validCutterCompProgram.replace(
        QStringLiteral("M5\n"),
        QStringLiteral("G1 G41 X1.000 Y0.000 F100\n"
                       "G1 X2.000 Y0.000\n"
                       "G1 G40 X3.000 Y0.000\n"
                       "M5\n"));
    const GCodeSafetyReport validCutterCompReport =
        GCodeSafetyValidator::validate(validCutterCompProgram);
    if (expect(validCutterCompReport.ok,
               "cutter compensation with linear lead-in and G40 should pass")) {
        return 1;
    }

    QString invalidCutterCompLeadInProgram = validProgram;
    invalidCutterCompLeadInProgram.replace(
        QStringLiteral("M5\n"),
        QStringLiteral("G41\nG1 X2.000 Y0.000\nG1 G40 X3.000 Y0.000\nM5\n"));
    const GCodeSafetyReport invalidCutterCompLeadInReport =
        GCodeSafetyValidator::validate(invalidCutterCompLeadInProgram);
    if (expect(!invalidCutterCompLeadInReport.ok,
               "cutter compensation without a linear XY lead-in should fail")) {
        return 1;
    }
    if (expect(invalidCutterCompLeadInReport.messages.join('\n')
                   .contains(QStringLiteral("linear X/Y lead-in")),
               "invalid cutter compensation lead-in report should explain the requirement")) {
        return 1;
    }

    QString invalidCutterCompLeadOutProgram = validProgram;
    invalidCutterCompLeadOutProgram.replace(
        QStringLiteral("M5\n"),
        QStringLiteral("G1 G41 X1.000 Y0.000 F100\n"
                       "G1 X2.000 Y0.000\n"
                       "G40\n"
                       "M5\n"));
    const GCodeSafetyReport invalidCutterCompLeadOutReport =
        GCodeSafetyValidator::validate(invalidCutterCompLeadOutProgram);
    if (expect(!invalidCutterCompLeadOutReport.ok,
               "cutter compensation cancellation without a linear XY lead-out should fail")) {
        return 1;
    }
    if (expect(invalidCutterCompLeadOutReport.messages.join('\n')
                   .contains(QStringLiteral("linear X/Y lead-out")),
               "invalid cutter compensation lead-out report should explain the requirement")) {
        return 1;
    }

    QString modalCutterCompLeadOutProgram = validProgram;
    modalCutterCompLeadOutProgram.replace(
        QStringLiteral("M5\n"),
        QStringLiteral("G1 G41 X1.000 Y0.000 F100\n"
                       "G1 X2.000 Y0.000\n"
                       "G40 X3.000 Y0.000\n"
                       "M5\n"));
    const GCodeSafetyReport modalCutterCompLeadOutReport =
        GCodeSafetyValidator::validate(modalCutterCompLeadOutProgram);
    if (expect(modalCutterCompLeadOutReport.ok,
               "G40 should accept a modal G1 XY lead-out")) {
        return 1;
    }

    const QString validTwoToolProgram =
        "G17 G40 G49 G80\n"
        "G21\n"
        "G90\n"
        "G54\n"
        "T1 M6\n"
        "S2000 M3\n"
        "M8\n"
        "G0 Z50.000\n"
        "G0 X0.000 Y0.000\n"
        "G1 Z-1.000 F100\n"
        "G0 Z50.000\n"
        "M5\n"
        "M9\n"
        "T2 M6\n"
        "S2500 M3\n"
        "G0 X10.000 Y0.000\n"
        "G1 Z-1.000 F100\n"
        "G0 Z50.000\n"
        "M5\n"
        "M9\n"
        "M30\n";
    const GCodeSafetyReport validTwoToolReport =
        GCodeSafetyValidator::validate(validTwoToolProgram);
    if (expect(validTwoToolReport.ok, "safe two-tool program should pass")) {
        return 1;
    }

    QString spindleRunningToolChangeProgram = validTwoToolProgram;
    spindleRunningToolChangeProgram.replace(
        QStringLiteral("G0 Z50.000\nM5\nM9\nT2 M6\n"),
        QStringLiteral("G0 Z50.000\nM9\nT2 M6\n"));
    const GCodeSafetyReport spindleRunningToolChangeReport =
        GCodeSafetyValidator::validate(spindleRunningToolChangeProgram);
    if (expect(!spindleRunningToolChangeReport.ok,
               "second tool change with spindle running should fail")) {
        return 1;
    }
    if (expect(spindleRunningToolChangeReport.messages.join('\n').contains(QStringLiteral("spindle"), Qt::CaseInsensitive),
               "running-spindle tool-change report should mention spindle")) {
        return 1;
    }

    QString coolantOnToolChangeProgram = validTwoToolProgram;
    coolantOnToolChangeProgram.replace(
        QStringLiteral("G0 Z50.000\nM5\nM9\nT2 M6\n"),
        QStringLiteral("G0 Z50.000\nM5\nT2 M6\n"));
    const GCodeSafetyReport coolantOnToolChangeReport =
        GCodeSafetyValidator::validate(coolantOnToolChangeProgram);
    if (expect(!coolantOnToolChangeReport.ok,
               "second tool change with coolant on should fail")) {
        return 1;
    }
    if (expect(coolantOnToolChangeReport.messages.join('\n').contains(QStringLiteral("coolant"), Qt::CaseInsensitive),
               "coolant-on tool-change report should mention coolant")) {
        return 1;
    }

    QString lowZToolChangeProgram = validTwoToolProgram;
    lowZToolChangeProgram.replace(
        QStringLiteral("G1 Z-1.000 F100\nG0 Z50.000\nM5\nM9\nT2 M6\n"),
        QStringLiteral("G1 Z-1.000 F100\nM5\nM9\nT2 M6\n"));
    const GCodeSafetyReport lowZToolChangeReport =
        GCodeSafetyValidator::validate(lowZToolChangeProgram);
    if (expect(!lowZToolChangeReport.ok,
               "second tool change at cutting Z should fail")) {
        return 1;
    }
    if (expect(lowZToolChangeReport.messages.join('\n').contains(QStringLiteral("safe Z")),
               "low-Z tool-change report should mention safe Z")) {
        return 1;
    }

    QString restartedSpindleProgram = validProgram;
    restartedSpindleProgram.replace(
        QStringLiteral("M5\nM9\nM30\n"),
        QStringLiteral("M5\nS2500 M3\nM9\nM30\n"));
    const GCodeSafetyReport restartedSpindleReport =
        GCodeSafetyValidator::validate(restartedSpindleProgram);
    if (expect(!restartedSpindleReport.ok,
               "program ending with spindle running should fail")) {
        return 1;
    }
    if (expect(restartedSpindleReport.messages.join('\n').contains(QStringLiteral("M5")),
               "running-spindle program-end report should mention M5")) {
        return 1;
    }

    const QString unsafeRapid =
        "G17 G40 G49 G80\n"
        "G21\n"
        "G90\n"
        "G54\n"
        "T1 M6\n"
        "S2000 M3\n"
        "G1 Z-2.000 F100\n"
        "G0 X10.000 Y0.000\n"
        "M5\n"
        "M9\n"
        "M30\n";
    const GCodeSafetyReport unsafeRapidReport = GCodeSafetyValidator::validate(unsafeRapid);
    if (expect(!unsafeRapidReport.ok, "rapid XY at cutting Z should fail")) {
        return 1;
    }
    if (expect(unsafeRapidReport.messages.join('\n').contains(QStringLiteral("G0 X/Y")),
               "unsafe rapid report should mention G0 X/Y")) {
        return 1;
    }

    QString uncancelledFixedCycleProgram = validProgram;
    uncancelledFixedCycleProgram.replace(
        QStringLiteral("G1 Z-1.000 F100\nG0 Z50.000\n"),
        QStringLiteral("G81 X0.000 Y0.000 Z-1.000 R2.000 F100\nG0 Z50.000\n"));
    const GCodeSafetyReport uncancelledFixedCycleReport =
        GCodeSafetyValidator::validate(uncancelledFixedCycleProgram);
    if (expect(!uncancelledFixedCycleReport.ok,
               "fixed cycle without G80 cancellation should fail")) {
        return 1;
    }
    if (expect(uncancelledFixedCycleReport.messages.join('\n').contains(QStringLiteral("G80")),
               "uncancelled fixed-cycle report should require G80")) {
        return 1;
    }

    QString cancelledFixedCycleProgram = uncancelledFixedCycleProgram;
    cancelledFixedCycleProgram.replace(
        QStringLiteral("G81 X0.000 Y0.000 Z-1.000 R2.000 F100\nG0 Z50.000\n"),
        QStringLiteral("G81 X0.000 Y0.000 Z-1.000 R2.000 F100\nG80\nG0 Z50.000\n"));
    const GCodeSafetyReport cancelledFixedCycleReport =
        GCodeSafetyValidator::validate(cancelledFixedCycleProgram);
    if (expect(cancelledFixedCycleReport.ok,
               "fixed cycle cancelled with G80 should pass")) {
        return 1;
    }

    QString repeatedFixedCycleProgram = cancelledFixedCycleProgram;
    repeatedFixedCycleProgram.replace(
        QStringLiteral("G81 X0.000 Y0.000 Z-1.000 R2.000 F100\nG80\n"),
        QStringLiteral("G83 X0.000 Y0.000 Z-5.000 R2.000 Q1.000 F100\n"
                       "X20.000 Y10.000\n"
                       "G80\n"));
    const GCodeSafetyReport repeatedFixedCycleReport =
        GCodeSafetyValidator::validate(repeatedFixedCycleProgram);
    if (expect(repeatedFixedCycleReport.ok,
               "modal X/Y calls in an active fixed cycle should not be treated as G0 moves")) {
        return 1;
    }

    return 0;
}
