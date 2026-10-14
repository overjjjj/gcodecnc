#pragma once
#include "../../core/StrategyParams.h"
#include <cmath>

struct PocketAllowances {
    double side;
    double floor;

    bool valid(double depth) const {
        return std::isfinite(side) && side >= 0 && std::isfinite(floor) && floor >= 0
            && std::isfinite(depth) && depth > floor;
    }
};

// Legacy roughing stock only affected XY. Never turn it into a floor allowance.
inline PocketAllowances pocketRoughingAllowances(const StrategyParams &params, double legacySide = .3)
{
    return {params.get("sideStockToLeave", params.get("stockToLeave", legacySide)),
            params.get("floorStockToLeave", 0)};
}
