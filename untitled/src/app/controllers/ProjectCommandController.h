#pragma once

#include <QObject>
#include <QString>
#include <functional>

// 项目命令端口：只编排 UI 命令与项目服务，不拥有项目数据或文件格式。
class ProjectCommandController : public QObject
{
public:
    using SaveHandler = std::function<bool(const QString &)>;
    using LoadHandler = std::function<bool(const QString &, const QString &)>;

    ProjectCommandController(SaveHandler saveHandler, LoadHandler loadHandler,
                             QObject *parent = nullptr);
    bool saveProject(const QString &path) const;
    bool openProject(const QString &path, const QString &replacementSource = QString()) const;

private:
    SaveHandler m_saveHandler;
    LoadHandler m_loadHandler;
};
