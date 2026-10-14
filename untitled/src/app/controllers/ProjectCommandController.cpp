#include "ProjectCommandController.h"

ProjectCommandController::ProjectCommandController(SaveHandler saveHandler,
                                                   LoadHandler loadHandler,
                                                   QObject *parent)
    : QObject(parent), m_saveHandler(std::move(saveHandler)), m_loadHandler(std::move(loadHandler))
{
}

// 路径为空时不触发服务，避免取消文件对话框造成副作用。
bool ProjectCommandController::saveProject(const QString &path) const
{
    return !path.isEmpty() && m_saveHandler && m_saveHandler(path);
}

// 重连源文件由调用方显式传入；控制器不猜测替代模型路径。
bool ProjectCommandController::openProject(const QString &path,
                                           const QString &replacementSource) const
{
    return !path.isEmpty() && m_loadHandler && m_loadHandler(path, replacementSource);
}
