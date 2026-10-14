#include "../src/workspaces/process_design/ui/ProcessParameterTemplateStore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    ProcessParameterTemplateStore store(dir.filePath("templates.json"));
    QString error;
    QVariantMap source{{"feedRate", 320.0}, {"spindleSpeed", 1200.0},
                       {"depth", 42.0}, {"slotWidth", 10.0}, {"workOffset", 55.0}, {"toolId", 9.0},
                       {"safeHeight", 90.0}, {"feedHeight", 80.0}, {"bottomAllowance", 5.0},
                       {"pitch", 2.0}, {"geometryRefs", "other-hole"}};
    assert(store.save("Steel", "hole_peck", source, &error));
    assert(!store.save("Steel", "hole_peck", source, &error));
    ProcessParameterTemplateStore reopened(dir.filePath("templates.json"));
    QVariantMap destination{{"depth", 12.0}, {"workOffset", 54.0}, {"feedRate", 10.0},
                            {"safeHeight", 50.0}, {"feedHeight", 3.0}, {"bottomAllowance", 0.0},
                            {"pitch", 1.0}, {"geometryRefs", "selected-hole"}, {"toolId", 7.0}, {"slotWidth", 6.0}};
    QVariantMap expected = destination;
    expected["feedRate"] = 320.0;
    assert(reopened.apply("Steel", "hole_peck", &destination, &error));
    assert(destination.value("feedRate").toDouble() == 320.0);
    assert(destination.value("depth").toDouble() == 12.0);
    assert(destination.value("workOffset").toDouble() == 54.0);
    assert(destination == expected);
    const QVariantMap before = destination;
    assert(!reopened.apply("Steel", "hole_deephole", &destination, &error));
    assert(destination == before);
    assert(reopened.remove("Steel", "hole_peck", &error));
    assert(reopened.entries(&error).isEmpty());
    QFile file(dir.filePath("templates.json"));
    assert(file.open(QIODevice::WriteOnly));
    file.write("{\"version\":99,\"templates\":[]}");
    file.close();
    assert(!reopened.save("New", "hole_peck", source, &error));
    assert(!error.isEmpty());
    assert(file.open(QIODevice::WriteOnly));
    file.write("{\"version\":1,\"templates\":[{\"name\":\"Unsafe\",\"strategyId\":\"hole_peck\",\"parameters\":{\"depth\":99}}]}");
    file.close();
    assert(!reopened.apply("Unsafe", "hole_peck", &destination, &error));
    assert(destination == before);
    ProcessParameterTemplateStore unwritable(dir.path());
    assert(!unwritable.save("New", "hole_peck", source, &error));
    assert(!error.isEmpty());
    return 0;
}
