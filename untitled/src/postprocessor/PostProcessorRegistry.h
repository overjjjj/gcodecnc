#pragma once
#include "PostProcessorBase.h"
#include <QMap>
#include <memory>

class PostProcessorRegistry
{
public:
    static PostProcessorRegistry &instance();

    void registerProcessor(std::shared_ptr<PostProcessorBase> pp);
    PostProcessorBase *get(const QString &id) const;
    QStringList availableIds() const;

private:
    PostProcessorRegistry();
    QMap<QString, std::shared_ptr<PostProcessorBase>> m_map;
};
