#include "../src/core/SetupOrientation.h"
#include "../src/import/StepImporter.h"

#include <QCoreApplication>
#include <QVector3D>
#include <iostream>

static int expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << "\n";
        return 1;
    }
    return 0;
}

static bool nearlySameDirection(const QVector3D &left, const QVector3D &right)
{
    return QVector3D::dotProduct(left.normalized(), right.normalized()) > 0.9999f;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    if (expect(SetupOrientation::canConfirmFaceSelection(
                   12, QVector3D(0, 0, 1), 12, QVector3D(0, 0, 1)),
               "the same face should confirm front-face selection")) {
        return 1;
    }
    if (expect(SetupOrientation::canConfirmFaceSelection(
                   12, QVector3D(0, 0, 1), 27, QVector3D(0, 0, 1)),
               "split coplanar faces with the same normal should confirm selection")) {
        return 1;
    }
    if (expect(!SetupOrientation::canConfirmFaceSelection(
                   12, QVector3D(0, 0, 1), 27, QVector3D(1, 0, 0)),
               "a face with a different normal should replace the pending selection")) {
        return 1;
    }

    if (expect(SetupOrientation::requiresActiveRegionConfirmation(
                   FaceRegion::Unknown, FaceRegion::Front),
               "an unspecified active region should require Setup confirmation")) {
        return 1;
    }
    if (expect(SetupOrientation::requiresActiveRegionConfirmation(
                   FaceRegion::Front, FaceRegion::Unknown),
               "an unclassified feature region should require an explicit override")) {
        return 1;
    }
    if (expect(SetupOrientation::requiresActiveRegionConfirmation(
                   FaceRegion::Unknown, FaceRegion::Unknown),
               "unknown Setup and feature regions must not fail open")) {
        return 1;
    }
    if (expect(!SetupOrientation::requiresActiveRegionConfirmation(
                   FaceRegion::Front, FaceRegion::Front),
               "a feature in the active region should not require an override")) {
        return 1;
    }
    if (expect(SetupOrientation::requiresActiveRegionConfirmation(
                   FaceRegion::Front, FaceRegion::Side),
               "a side feature in the front setup should require an override")) {
        return 1;
    }
    if (expect(SetupOrientation::requiresActiveRegionConfirmation(
                   FaceRegion::Front, FaceRegion::Back),
               "a back feature in the front setup should require an override")) {
        return 1;
    }
    if (expect(SetupOrientation::requiresActiveRegionConfirmation(
                   FaceRegion::Side, FaceRegion::Front),
               "a front feature in a side setup should require an override")) {
        return 1;
    }

    const QQuaternion currentRotation =
        QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), 90.0f);
    const QVector3D originalNormal(1, 0, 0);
    const QVector3D currentNormal = currentRotation.rotatedVector(originalNormal);
    const QQuaternion combined = SetupOrientation::combinedRotationToFront(
        currentRotation, currentNormal);
    if (expect(nearlySameDirection(combined.rotatedVector(originalNormal),
                                   QVector3D(0, 0, 1)),
               "new front-face rotation should compose with the existing Setup rotation")) {
        return 1;
    }

    return 0;
}
