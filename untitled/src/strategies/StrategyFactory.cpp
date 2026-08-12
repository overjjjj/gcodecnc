#include "StrategyFactory.h"
#include "hole/SpotDrillingStrategy.h"
#include "hole/PeckDrillingStrategy.h"
#include "hole/DeepHoleDrillingStrategy.h"
#include "hole/TappingStrategy.h"
#include "hole/ReamingStrategy.h"
#include "hole/ChamferStrategy.h"
#include "hole/HoleCircularMillingStrategy.h"
#include "mill/CircleMillingStrategy.h"
#include "mill/FaceMillingStrategy.h"
#include "mill/PocketRoughingStrategy.h"
#include "mill/PocketFinishStrategy.h"
#include "mill/PocketFloorFinishStrategy.h"
#include "mill/ContourFinishStrategy.h"
#include "mill/SurfaceFinishStrategy.h"
#include "mill/ClosedContourMillingStrategy.h"
#include "mill/OpenContourMillingStrategy.h"
#include "mill/SlotMillingStrategy.h"
#include "mill/BlindSlotMillingStrategy.h"
#include "mill/TaperedSlotMillingStrategy.h"

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
    m_strategies[strategy->id()] = strategy;
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
    registerStrategy(std::make_shared<TappingStrategy>());
    registerStrategy(std::make_shared<ReamingStrategy>());
    registerStrategy(std::make_shared<ChamferStrategy>());
    registerStrategy(std::make_shared<HoleCircularMillingStrategy>());
    registerStrategy(std::make_shared<CircleMillingStrategy>());
    registerStrategy(std::make_shared<FaceMillingStrategy>());
    registerStrategy(std::make_shared<PocketRoughingStrategy>());
    registerStrategy(std::make_shared<PocketFinishStrategy>());
    registerStrategy(std::make_shared<PocketFloorFinishStrategy>());
    registerStrategy(std::make_shared<ContourFinishStrategy>());
    registerStrategy(std::make_shared<SurfaceFinishStrategy>());
    registerStrategy(std::make_shared<ClosedContourMillingStrategy>());
    registerStrategy(std::make_shared<OpenContourMillingStrategy>());
    registerStrategy(std::make_shared<SlotMillingStrategy>());
    registerStrategy(std::make_shared<BlindSlotMillingStrategy>());
    registerStrategy(std::make_shared<TaperedSlotMillingStrategy>());
}
