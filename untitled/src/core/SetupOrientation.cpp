#include "SetupOrientation.h"

#include "../import/StepImporter.h"

bool SetupOrientation::canConfirmFaceSelection(int pendingFaceIndex,
                                               const QVector3D &pendingNormal,
                                               int selectedFaceIndex,
                                               const QVector3D &selectedNormal)
{
    if (pendingFaceIndex <= 0 || selectedFaceIndex <= 0 ||
        pendingNormal.lengthSquared() <= 1.0e-8f ||
        selectedNormal.lengthSquared() <= 1.0e-8f) {
        return false;
    }
    if (pendingFaceIndex == selectedFaceIndex) {
        return true;
    }
    return QVector3D::dotProduct(pendingNormal.normalized(), selectedNormal.normalized()) >=
           0.999f;
}

bool SetupOrientation::requiresActiveRegionConfirmation(FaceRegion activeRegion,
                                                        FaceRegion featureRegion)
{
    return activeRegion == FaceRegion::Unknown ||
           featureRegion == FaceRegion::Unknown ||
           activeRegion != featureRegion;
}

QQuaternion SetupOrientation::combinedRotationToFront(
    const QQuaternion &currentRotation,
    const QVector3D &selectedNormal)
{
    const QQuaternion normalizedCurrent = currentRotation.isNull()
                                              ? QQuaternion()
                                              : currentRotation.normalized();
    if (selectedNormal.lengthSquared() <= 1.0e-8f) {
        return normalizedCurrent;
    }
    const QQuaternion delta = QQuaternion::rotationTo(
        selectedNormal.normalized(), QVector3D(0.0f, 0.0f, 1.0f));
    return (delta * normalizedCurrent).normalized();
}
