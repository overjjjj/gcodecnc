#include "StrategyFactory.h"
#include "hole/SpotDrillingStrategy.h"
#include "hole/PeckDrillingStrategy.h"
#include "hole/DeepHoleDrillingStrategy.h"
#include "hole/HighSpeedPeckDrillingStrategy.h"
#include "hole/BoringG86Strategy.h"
#include "hole/TappingStrategy.h"
#include "hole/ThreadMillingStrategy.h"
#include "hole/ReamingStrategy.h"
#include "hole/ChamferStrategy.h"
#include "hole/HoleCircularMillingStrategy.h"
#include "mill/CircleMillingStrategy.h"
#include "mill/AnnularMillingStrategy.h"
#include "mill/IslandMillingStrategy.h"
#include "mill/FaceMillingStrategy.h"
#include "mill/PocketRoughingStrategy.h"
#include "mill/PocketFinishStrategy.h"
#include "mill/PocketFloorFinishStrategy.h"
#include "mill/ContourFinishStrategy.h"
#include "mill/SurfaceFinishStrategy.h"
#include "mill/ClosedContourMillingStrategy.h"
#include "mill/OpenContourMillingStrategy.h"
#include "mill/OuterContourChamferStrategy.h"
#include "mill/PlanarSlopeMillingStrategy.h"
#include "mill/InnerCornerCleanupStrategy.h"
#include "mill/SlotMillingStrategy.h"
#include "mill/BlindSlotMillingStrategy.h"
#include "mill/TaperedSlotMillingStrategy.h"

#include <utility>

StrategyFactory::StrategyFactory()
{
    registerDefaults();
}

StrategyFactory &StrategyFactory::instance()
{
    static StrategyFactory s;
    return s;
}

void StrategyFactory::registerStrategy(std::shared_ptr<StrategyBase> strategy)
{
    m_strategies[strategy->id()] = std::move(strategy);
}

std::shared_ptr<StrategyBase> StrategyFactory::strategy(const QString &id) const
{
    return m_strategies.value(id, nullptr);
}

QStringList StrategyFactory::availableIds() const
{
    return m_strategies.keys();
}

void StrategyFactory::registerDefaults()
{
    registerStrategy(std::make_shared<SpotDrillingStrategy>());
    registerStrategy(std::make_shared<PeckDrillingStrategy>());
    registerStrategy(std::make_shared<DeepHoleDrillingStrategy>());
    registerStrategy(std::make_shared<HighSpeedPeckDrillingStrategy>());
    registerStrategy(std::make_shared<BoringG86Strategy>());
    registerStrategy(std::make_shared<TappingStrategy>());
    registerStrategy(std::make_shared<ThreadMillingStrategy>());
    registerStrategy(std::make_shared<ReamingStrategy>());
    registerStrategy(std::make_shared<ChamferStrategy>());
    registerStrategy(std::make_shared<HoleCircularMillingStrategy>());
    registerStrategy(std::make_shared<CircleMillingStrategy>());
    registerStrategy(std::make_shared<AnnularMillingStrategy>());
    registerStrategy(std::make_shared<IslandMillingStrategy>());
    registerStrategy(std::make_shared<FaceMillingStrategy>());
    registerStrategy(std::make_shared<PocketRoughingStrategy>());
    registerStrategy(std::make_shared<PocketFinishStrategy>());
    registerStrategy(std::make_shared<PocketFloorFinishStrategy>());
    registerStrategy(std::make_shared<ContourFinishStrategy>());
    registerStrategy(std::make_shared<SurfaceFinishStrategy>());
    registerStrategy(std::make_shared<ClosedContourMillingStrategy>());
    registerStrategy(std::make_shared<OpenContourMillingStrategy>());
    registerStrategy(std::make_shared<OuterContourChamferStrategy>());
    registerStrategy(std::make_shared<PlanarSlopeMillingStrategy>());
    registerStrategy(std::make_shared<InnerCornerCleanupStrategy>());
    registerStrategy(std::make_shared<SlotMillingStrategy>());
    registerStrategy(std::make_shared<BlindSlotMillingStrategy>());
    registerStrategy(std::make_shared<TaperedSlotMillingStrategy>());
}
