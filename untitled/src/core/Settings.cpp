#include "Settings.h"

// 初始化应用设置存储，并加载已保存的用户偏好。
Settings::Settings()
    // 函数说明：m_qs负责当前模块的核心处理。
    : m_qs("JIANGJUREN", "CNEXT-CAM")
{
    load();
}

// 返回进程内共享的设置实例，首次访问时加载配置。
Settings &Settings::instance()
{
    static Settings s;
    return s;
}

// 返回当前界面语言标识。
QString Settings::language()       const { return m_language; }
// 更新内存中的语言偏好，调用 save 后才持久化。
void    Settings::setLanguage(const QString &v) { m_language = v; }
// 返回当前选择的后处理器标识。
QString Settings::postProcessorId() const { return m_postProcessorId; }
// 更新内存中的后处理器选择，不立即持久化。
void    Settings::setPostProcessorId(const QString &v) { m_postProcessorId = v; }
// 返回是否显示视图区网格的用户偏好。
bool    Settings::showGrid()       const { return m_showGrid; }
// 更新网格显示偏好，不直接修改视图区或保存设置。
void    Settings::setShowGrid(bool v)   { m_showGrid = v; }

// 将语言、后处理器和网格显示偏好写入设置存储。
void Settings::save()
{
    m_qs.setValue("language",        m_language);
    m_qs.setValue("postProcessorId", m_postProcessorId);
    m_qs.setValue("showGrid",        m_showGrid);
}

// 读取用户偏好，缺失时默认使用中文、Siemens 后处理器和显示网格。
void Settings::load()
{
    m_language        = m_qs.value("language",        "zh_CN").toString();
    m_postProcessorId = m_qs.value("postProcessorId", "siemens").toString();
    m_showGrid        = m_qs.value("showGrid",         true).toBool();
}
