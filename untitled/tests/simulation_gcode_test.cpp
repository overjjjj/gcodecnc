#include "../src/simulation/SimulationController.h"

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
    return 0;
}
