#include "SetupOrientation.h"

#include "../import/StepImporter.h"

// 检查两次选面能否确认：索引和法向有效，且为同一面或法向近似同向。
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

// 判断当前区域与特征区域是否未知或不一致，供界面提示重新确认。
bool SetupOrientation::requiresActiveRegionConfirmation(FaceRegion activeRegion,
                                                        FaceRegion featureRegion)
{
    return activeRegion == FaceRegion::Unknown ||
           featureRegion == FaceRegion::Unknown ||
           activeRegion != featureRegion;
}

// 将选中法向对齐到正 Z 并合成当前旋转；无效法向仅返回规范化当前旋转。
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
