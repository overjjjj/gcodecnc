#include <iostream>
#include <QString>
#include "../src/app/controllers/ProjectCommandController.h"

int main()
{
    int saves = 0;
    QString opened;
    ProjectCommandController controller(
        [&](const QString &path) { ++saves; return path == "ok.cnext"; },
        [&](const QString &path, const QString &) { opened = path; return true; });
    if (controller.saveProject("bad.cnext") || saves != 1) return 1;
    if (!controller.saveProject("ok.cnext") || saves != 2) return 2;
    if (!controller.openProject("demo.cnext") || opened != "demo.cnext") return 3;
    if (controller.openProject(QString())) return 4;
    return 0;
}
