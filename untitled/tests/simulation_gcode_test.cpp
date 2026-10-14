#include "../src/workspaces/program_verification/simulation/SimulationController.h"

#include <QCoreApplication>
#include <QString>
#include <QVector>
#include <iostream>

static int expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << "\n";
        return 1;
    }
    return 0;
}

static bool hasFeedMoveTo(const QVector<QVector3D> &path, const QVector<bool> &rapidSegments,
                          const QVector3D &target)
{
    for (int i = 0; i < rapidSegments.size(); ++i) {
        if (!rapidSegments[i] && (path[i + 1] - target).length() < 1.0e-3f) {
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    SimulationController simulation;
    QVector<QVector3D> path;
    QVector<bool> rapidSegments;
    QObject::connect(&simulation, &SimulationController::toolPathReady,
                     [&path, &rapidSegments](const QVector<QVector3D> &newPath,
                                             const QVector<bool> &newRapidSegments) {
        path = newPath;
        rapidSegments = newRapidSegments;
    });

    const QString cq8Program =
        "G17 G40 G49 G80\n"
        "G21\n"
        "G90\n"
        "G54\n"
        "G94\n"
        "T21 M6\n"
        "S800 M3\n"
        "G0 Z50.000\n"
        "G98 G83 Z-51.000 R3.000 Q3.000 F60.000 X-60.000 Y305.000\n"
        "X-60.000 Y-305.000\n"
        "G80\n"
        "M5\n"
        "M9\n"
        "M30\n";

    simulation.loadGCode(cq8Program);

    if (expect(hasFeedMoveTo(path, rapidSegments, QVector3D(-60.0f, 305.0f, -51.0f)),
               "CQ8 G83 first hole must expand to a feed move at its final depth")) {
        return 1;
    }
    if (expect(hasFeedMoveTo(path, rapidSegments, QVector3D(-60.0f, -305.0f, -51.0f)),
               "CQ8 modal G83 second hole must expand to a feed move at its final depth")) {
        return 1;
    }

    const QString g73Program = QString(cq8Program).replace(QStringLiteral("G83"),
                                                           QStringLiteral("G73"));
    simulation.loadGCode(g73Program);
    if (expect(hasFeedMoveTo(path, rapidSegments, QVector3D(-60.0f, 305.0f, -51.0f)),
               "CQ8 G73 first hole must expand to its final depth") ||
        expect(hasFeedMoveTo(path, rapidSegments, QVector3D(-60.0f, -305.0f, -51.0f)),
               "CQ8 modal G73 second hole must expand to its final depth") ||
        expect(simulation.hasUnverifiedRetract(),
               "Fanuc G73 simulation must expose unverified short-retract status")) {
        return 1;
    }

    const QString siemensG73 =
        "MCALL CYCLE83(25.000,0.000,3.000,-20.000,,,5.000,0,0.300,,1,0,,,0,,)\n"
        "X10.000 Y20.000\n"
        "MCALL\n";
    simulation.loadGCode(siemensG73);
    if (expect(!simulation.hasUnverifiedRetract(),
               "Siemens CYCLE83 VARI=0 with explicit default VRT must be verified") ||
        expect(hasFeedMoveTo(path, rapidSegments, QVector3D(10.0f, 20.0f, -20.0f)),
               "Siemens CYCLE83 must reach its final depth")) {
        return 1;
    }
    bool sawShortFeedRetract = false;
    for (int i = 0; i < rapidSegments.size(); ++i) {
        if (!rapidSegments.at(i) && path.at(i).z() < -1.9f && path.at(i).z() > -2.1f &&
            path.at(i + 1).z() > -1.1f && path.at(i + 1).z() < -0.9f) {
            sawShortFeedRetract = true;
        }
    }
    if (expect(sawShortFeedRetract,
               "Siemens VARI=0 must use a one-millimeter feed retract instead of full rapid return")) {
        return 1;
    }

    const QString boringProgram =
        "G17 G40 G49 G80\nG21\nG90\nG54\nG94\nT17 M6\nS400 M3\nG0 Z20.000\n"
        "G98 G76 Z-5.000 R3.000 Q0.100 F40.000 X12.000 Y24.000\n"
        "X32.000 Y24.000\nG80\n"
        "G98 G86 Z-5.000 R3.000 F40.000 X12.000 Y44.000\n"
        "X32.000 Y44.000\nG80\nM5\nM9\nM30\n";
    simulation.loadGCode(boringProgram);
    if (expect(hasFeedMoveTo(path, rapidSegments, QVector3D(12.0f, 24.0f, -5.0f)) &&
                   hasFeedMoveTo(path, rapidSegments, QVector3D(32.0f, 24.0f, -5.0f)),
               "G76 simulation must show both selected hole positions at confirmed depth") ||
        expect(hasFeedMoveTo(path, rapidSegments, QVector3D(12.0f, 44.0f, -5.0f)) &&
                   hasFeedMoveTo(path, rapidSegments, QVector3D(32.0f, 44.0f, -5.0f)),
               "G86 simulation must show both selected hole positions at confirmed depth") ||
        expect(simulation.hasUnverifiedG76LateralShift(),
               "G76 simulation must disclose that lateral shift direction is unverified") ||
        expect(simulation.hasUnverifiedG86SpindleStopRetract(),
               "G86 simulation must disclose that spindle-stop retract timing is unverified")) {
        return 1;
    }
    simulation.loadGCode(cq8Program);
    if (expect(!simulation.hasUnverifiedG76LateralShift() &&
                   !simulation.hasUnverifiedG86SpindleStopRetract(),
               "boring-cycle limitation flags must reset for the next program")) {
        return 1;
    }
    return 0;
}
