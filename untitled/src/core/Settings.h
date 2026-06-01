#pragma once
#include <QSettings>
#include <QString>

class Settings
{
public:
    static Settings &instance();

    QString language() const;
    void    setLanguage(const QString &lang);

    QString postProcessorId() const;
    void    setPostProcessorId(const QString &id);

    bool    showGrid() const;
    void    setShowGrid(bool v);

    void    save();
    void    load();

private:
    Settings();
    QSettings m_qs;
    QString   m_language;
    QString   m_postProcessorId;
    bool      m_showGrid = true;
};
