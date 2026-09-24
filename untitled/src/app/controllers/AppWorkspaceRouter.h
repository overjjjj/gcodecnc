#pragma once

#include <QObject>
#include "../../workspaces/shared/WorkspaceModule.h"

class QListWidget;
class QStackedWidget;
class QLabel;

// 应用壳层路由：只同步导航、页面与标题，不访问项目或加工业务。
// 三个控件须比路由器的使用期更长；页面按设计、验证、运行顺序注册。
class AppWorkspaceRouter : public QObject
{
public:
    AppWorkspaceRouter(QListWidget &navigation, QStackedWidget &pages,
                       QLabel &title, QObject *parent = nullptr);
    bool showWorkspace(WorkspaceArchitecture::WorkspaceId workspace);
    void refreshTitle();

private:
    QListWidget &m_navigation;
    QStackedWidget &m_pages;
    QLabel &m_title;
};
