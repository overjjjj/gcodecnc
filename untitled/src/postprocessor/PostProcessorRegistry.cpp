#include "PostProcessorRegistry.h"
#include "SiemensPostProcessor.h"
#include "FanucPostProcessor.h"
#include "Cq8PostProcessor.h"

#include <utility>

PostProcessorRegistry &PostProcessorRegistry::instance()
{
    static PostProcessorRegistry inst;
    return inst;
}

PostProcessorRegistry::PostProcessorRegistry()
{
    registerProcessor(std::make_shared<SiemensPostProcessor>());
    registerProcessor(std::make_shared<FanucPostProcessor>());
    registerProcessor(std::make_shared<Cq8PostProcessor>());
}

void PostProcessorRegistry::registerProcessor(std::shared_ptr<PostProcessorBase> pp)
{
    m_map.insert(pp->id(), std::move(pp));
}

PostProcessorBase *PostProcessorRegistry::get(const QString &id) const
{
    auto it = m_map.find(id);
    return it != m_map.end() ? it->get() : nullptr;
}

QStringList PostProcessorRegistry::availableIds() const
{
    return m_map.keys();
}
