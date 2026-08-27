#include "../src/strategies/hole/ThreadMillingStrategy.h"

#include <QCoreApplication>

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    ThreadMillingStrategy strategy;
    const ProcessParameterSchema schema = strategy.parameterSchema();
    for (const QString &id : {QStringLiteral("threadPitch"),
                              QStringLiteral("threadSide"),
                              QStringLiteral("threadHand"),
                              QStringLiteral("cutDirection"),
                              QStringLiteral("toothCount"),
                              QStringLiteral("diameterCorrection"),
                              QStringLiteral("taperAngle"),
                              QStringLiteral("roughPasses"),
                              QStringLiteral("finishAllowance"),
                              QStringLiteral("finishFeedRate"),
                              QStringLiteral("finishSpindleSpeed")}) {
        if (!expect(schema.definition(id) != nullptr,
                    "thread milling schema must declare every DTM identity field")) {
            return 1;
        }
    }

    HoleFeature thread;
    thread.center = QVector3D(10.0f, 20.0f, 0.0f);
    thread.radius = 5.0;
    thread.depth = 9.0;
    thread.pitch = 1.5;
    ToolEntry tool;
    tool.id = 31;
    tool.type = QStringLiteral("thread_mill");
    tool.diameter = 4.0;
    tool.fluteLen = 15.0;
    tool.totalLen = 50.0;
    tool.pitch = 1.5;

    StrategyParams params = strategy.defaultParams();
    params.set(QStringLiteral("depth"), 9.0);
    params.set(QStringLiteral("threadPitch"), 1.5);
    const ToolpathResult valid = strategy.generate(thread, tool, params);
    if (!expect(valid.ok && valid.gcode.contains(QStringLiteral("CNEXT_THREAD_MILL")) &&
                    valid.gcode.contains(QStringLiteral("G1 X")) &&
                    valid.gcode.contains(QStringLiteral("Z-")),
                "a confirmed internal single-tooth thread must create a traceable helix")) {
        return 1;
    }

    StrategyParams layered = params;
    layered.set(QStringLiteral("roughPasses"), 2.0);
    layered.set(QStringLiteral("finishAllowance"), 0.2);
    layered.set(QStringLiteral("taperAngle"), 2.0);
    layered.set(QStringLiteral("finishFeedRate"), 180.0);
    layered.set(QStringLiteral("finishSpindleSpeed"), 2200.0);
    const ToolpathResult layeredResult = strategy.generate(thread, tool, layered);
    if (!expect(layeredResult.ok &&
                    layeredResult.gcode.contains(QStringLiteral("pass=rough-1")) &&
                    layeredResult.gcode.contains(QStringLiteral("pass=rough-2")) &&
                    layeredResult.gcode.contains(QStringLiteral("pass=finish")) &&
                    layeredResult.gcode.contains(QStringLiteral("S2200 M3")),
                "thread milling must emit traceable tapered rough and finish passes")) {
        return 1;
    }

    StrategyParams impossibleTaper = params;
    impossibleTaper.set(QStringLiteral("taperAngle"), 45.0);
    if (!expect(!strategy.generate(thread, tool, impossibleTaper).ok,
                "a taper that collapses the internal tool-center radius must be blocked")) {
        return 1;
    }

    StrategyParams external = params;
    external.set(QStringLiteral("threadSide"), 1.0);
    if (!expect(!strategy.generate(thread, tool, external).ok,
                "external thread milling must stay blocked without a stock boundary")) {
        return 1;
    }
    StrategyParams multiTooth = params;
    multiTooth.set(QStringLiteral("toothCount"), 2.0);
    if (!expect(!strategy.generate(thread, tool, multiTooth).ok,
                "multi-tooth layer transfer must stay blocked until lift geometry is explicit")) {
        return 1;
    }
    tool.diameter = 10.0;
    return expect(!strategy.generate(thread, tool, params).ok,
                  "thread mill diameter must be smaller than the selected inner cylinder")
        ? 0
        : 1;
}
