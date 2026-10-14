#pragma once

#include "../../import/StepImporter.h"
#include "../../strategies/StrategyBase.h"
#include "../../tool/ToolEntry.h"

#include <QString>
#include <cmath>
#include <limits>

inline bool slotAxisSupportsZMachining(const QVector3D &axis)
{
    const double x = axis.x(), y = axis.y(), z = axis.z();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || z == 0.0)
        return false;
    // QVector3D stores floats. Allow only float transform roundoff, not a machining tilt.
    // The normal sign does not change the contour plane; Setup/access checks own the machining side.
    const double roundoff = 8.0 * std::numeric_limits<float>::epsilon();
    return std::hypot(x, y) <= std::abs(z) * roundoff;
}

struct SlotMachiningGeometry {
    bool valid = false;
    QString errorMsg;

    double cx = 0.0;
    double cy = 0.0;
    double zTop = 0.0;
    double depth = 0.0;

    double fullLength = 0.0;
    double fullWidth = 0.0;
    double halfLength = 0.0;
    double halfWidth = 0.0;

    double angleRad = 0.0;
    double cosA = 1.0;
    double sinA = 0.0;
    double openSign = -1.0;

    double radialClearance = 0.0;
    double lengthCenterInset = 0.0;
    double sideCenterInset = 0.0;

    double roughMinU = 0.0;
    double roughMaxU = 0.0;
    double roughHalfWidth = 0.0;

    double slopeStartLength = 0.0;
    double slopeEndLength = 0.0;
    double bottomStartU = 0.0;
    double bottomEndU = 0.0;
};

SlotMachiningGeometry buildSlotMachiningGeometry(const ContourFeature &feature,
                                                 const ToolEntry &tool,
                                                 const StrategyParams &params);

bool refineSlotContourFromMeshData(const MachiningFeature &source,
                                   const MeshData &mesh,
                                   ContourFeature &contour);

void slotLocalToWorld(const SlotMachiningGeometry &geometry,
                      double u,
                      double v,
                      double &x,
                      double &y);

QString validateSlotMachiningGeometry(const SlotMachiningGeometry &geometry,
                                      const ToolEntry &tool,
                                      const StrategyParams &params);
