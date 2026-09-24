#include "AppWorkspaceRouter.h"

#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>

// 建立导航到页面的单向同步；上下文销毁时 Qt 自动断开连接。
AppWorkspaceRouter::AppWorkspaceRouter(QListWidget &navigation, QStackedWidget &pages,
                                     QLabel &title, QObject *parent)
    : QObject(parent), m_navigation(navigation), m_pages(pages), m_title(title)
{
    connect(&m_navigation, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= m_navigation.count() || row >= m_pages.count()) return;
        m_pages.setCurrentIndex(row);
        refreshTitle();
    });
}

// 将工作区身份映射到页面；共享层和非法身份不是可导航工作区。
bool AppWorkspaceRouter::showWorkspace(WorkspaceArchitecture::WorkspaceId workspace)
{
    using WorkspaceArchitecture::WorkspaceId;
    int row = -1;
    switch (workspace) {
    case WorkspaceId::ProcessDesign: row = 0; break;
    case WorkspaceId::ProgramVerification: row = 1; break;
    case WorkspaceId::MachineOperation: row = 2; break;
    default: return false;
    }
    if (row >= m_navigation.count() || row >= m_pages.count()) return false;
    m_navigation.setCurrentRow(row);
    m_pages.setCurrentIndex(row);
    refreshTitle();
    return true;
}

// 翻译更新后从导航项读取标题，不维护另一份工作区文案。
void AppWorkspaceRouter::refreshTitle()
{
    if (m_navigation.currentRow() >= 0 && m_navigation.currentItem())
        m_title.setText(m_navigation.currentItem()->text());
}
