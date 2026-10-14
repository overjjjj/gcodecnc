#include "PostProcessorRegistry.h"
#include "SiemensPostProcessor.h"
#include "FanucPostProcessor.h"
#include "Cq8PostProcessor.h"

#include <utility>

// 函数说明：instance负责当前模块的核心处理。
PostProcessorRegistry &PostProcessorRegistry::instance()
{
    static PostProcessorRegistry inst;
    return inst;
}

// 函数说明：PostProcessorRegistry负责当前模块的核心处理。
PostProcessorRegistry::PostProcessorRegistry()
{
    registerProcessor(std::make_shared<SiemensPostProcessor>());
    registerProcessor(std::make_shared<FanucPostProcessor>());
    registerProcessor(std::make_shared<Cq8PostProcessor>());
}

// 函数说明：registerProcessor负责当前模块的核心处理。
void PostProcessorRegistry::registerProcessor(std::shared_ptr<PostProcessorBase> pp)
{
    m_map.insert(pp->id(), std::move(pp));
}

// 函数说明：get负责当前模块的核心处理。
PostProcessorBase *PostProcessorRegistry::get(const QString &id) const
{
    auto it = m_map.find(id);
    return it != m_map.end() ? it->get() : nullptr;
}

// 函数说明：availableIds负责当前模块的核心处理。
QStringList PostProcessorRegistry::availableIds() const
{
    return m_map.keys();
}
