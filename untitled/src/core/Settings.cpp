#include "Settings.h"

Settings::Settings()
    : m_qs("JIANGJUREN", "CNEXT-CAM")
{
    load();
}

Settings &Settings::instance()
{
    static Settings s;
    return s;
}

QString Settings::language()       const { return m_language; }
void    Settings::setLanguage(const QString &v) { m_language = v; }
QString Settings::postProcessorId() const { return m_postProcessorId; }
void    Settings::setPostProcessorId(const QString &v) { m_postProcessorId = v; }
bool    Settings::showGrid()       const { return m_showGrid; }
void    Settings::setShowGrid(bool v)   { m_showGrid = v; }

void Settings::save()
{
    m_qs.setValue("language",        m_language);
    m_qs.setValue("postProcessorId", m_postProcessorId);
    m_qs.setValue("showGrid",        m_showGrid);
}

void Settings::load()
{
    m_language        = m_qs.value("language",        "zh_CN").toString();
    m_postProcessorId = m_qs.value("postProcessorId", "siemens").toString();
    m_showGrid        = m_qs.value("showGrid",         true).toBool();
}
