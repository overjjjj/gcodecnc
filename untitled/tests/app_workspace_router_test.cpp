#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <iostream>

#if __has_include("../src/app/controllers/AppWorkspaceRouter.h")
#include "../src/app/controllers/AppWorkspaceRouter.h"
#endif

// 验证真实控件的工作区切换、标题刷新及非法目标阻断，不加载加工业务。
int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
#if !__has_include("../src/app/controllers/AppWorkspaceRouter.h")
    std::cerr << "FAIL: application workspace router has not been extracted\n";
    return 1;
#else
    using WorkspaceArchitecture::WorkspaceId;
    QListWidget navigation;
    navigation.addItems({"Design", "Verification", "Machine"});
    QStackedWidget pages;
    for (int i = 0; i < 3; ++i) pages.addWidget(new QWidget);
    QLabel title;
    AppWorkspaceRouter router(navigation, pages, title);
    int expectedPage = 0;
    for (const auto id : {WorkspaceId::ProcessDesign, WorkspaceId::ProgramVerification,
                          WorkspaceId::MachineOperation}) {
        if (!router.showWorkspace(id) || pages.currentIndex() != expectedPage++ ||
            pages.currentIndex() != navigation.currentRow() ||
            title.text() != navigation.currentItem()->text()) return 2;
    }
    navigation.setCurrentRow(0);
    if (pages.currentIndex() != 0 || title.text() != "Design") return 3;
    navigation.item(0)->setText(QString::fromUtf8("工艺设计"));
    router.refreshTitle();
    if (title.text() != QString::fromUtf8("工艺设计")) return 4;
    if (router.showWorkspace(WorkspaceId::Shared) || pages.currentIndex() != 0) return 5;
    if (router.showWorkspace(static_cast<WorkspaceId>(999))) return 6;
    navigation.setCurrentRow(-1);
    if (pages.currentIndex() != 0 || title.text() != QString::fromUtf8("工艺设计")) return 7;
    return 0;
#endif
}
