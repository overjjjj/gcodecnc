#include "../src/strategies/hole/SpotDrillingStrategy.h"

#include <QCoreApplication>

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    SpotDrillingStrategy strategy;
    const ProcessParameterSchema schema = strategy.parameterSchema();
    if (!expect(schema.definition(QStringLiteral("feedRate")) != nullptr,
                "strategies should expose a parameter schema")) {
        return 1;
    }

    StrategyParams params = schema.defaultParams();
    const StrategyParams strategy_defaults = strategy.defaultParams();
    for (auto it = strategy_defaults.values.cbegin();
         it != strategy_defaults.values.cend(); ++it) {
        params.values.insert(it.key(), it.value());
    }
    if (!expect(strategy.validate(params).isEmpty(),
                "strategy defaults should satisfy the shared contract")) {
        return 1;
    }

    params.set(QStringLiteral("feedRate"), 0.0);
    if (!expect(!strategy.validate(params).isEmpty(),
                "strategy validation should reject unsafe common parameters")) {
        return 1;
    }

    return 0;
}
