#pragma once

#include <QQuaternion>
#include <QVector3D>

enum class FaceRegion;

class SetupOrientation
{
public:
    static bool canConfirmFaceSelection(int pendingFaceIndex,
                                        const QVector3D &pendingNormal,
                                        int selectedFaceIndex,
                                        const QVector3D &selectedNormal);

    static bool requiresActiveRegionConfirmation(FaceRegion activeRegion,
                                                 FaceRegion featureRegion);

    static QQuaternion combinedRotationToFront(const QQuaternion &currentRotation,
                                               const QVector3D &selectedNormal);
};
