#include "Cq8MacroProgramBuilder.h"

namespace {

QString variableForIndex(int index)
{
    return QStringLiteral("#%1").arg(100 + index);
}

} // namespace

Cq8MacroProgram Cq8MacroProgramBuilder::build(
    const QList<ParametricToolpathProgram> &programs)
{
    Cq8MacroProgram output;
    QStringList calls;
    QStringList library;
    int routineNumber = 9001;

    for (const ParametricToolpathProgram &program : programs) {
        if (program.isEmpty()) {
            continue;
        }
        if (program.parameterNames.size() > 100) {
            output.error = QStringLiteral("CQ8 routine '%1' exceeds the #100-#199 parameter range.")
                               .arg(program.routineName);
            return output;
        }

        QStringList routineCalls;
        for (const ParametricToolpathCall &call : program.calls) {
            for (int parameterIndex = 0; parameterIndex < program.parameterNames.size();
                 ++parameterIndex) {
                const QString &name = program.parameterNames.at(parameterIndex);
                if (!call.arguments.contains(name)) {
                    output.error = QStringLiteral("CQ8 routine '%1' call is missing parameter '%2'.")
                                       .arg(program.routineName, name);
                    return output;
                }
                routineCalls.append(QStringLiteral("%1=%2")
                                 .arg(variableForIndex(parameterIndex),
                                      call.arguments.value(name)));
            }
            routineCalls.append(QStringLiteral("M98 P%1").arg(routineNumber));
        }
        calls.append(routineCalls);
        output.callBlocks.append(routineCalls.join(QLatin1Char('\n')));

        library.append(QStringLiteral("O%1").arg(routineNumber));
        library.append(QStringLiteral("; CNEXT ROUTINE: %1").arg(program.routineName));
        for (QString line : program.bodyTemplateLines) {
            for (int parameterIndex = 0; parameterIndex < program.parameterNames.size();
                 ++parameterIndex) {
                line.replace(QStringLiteral("${%1}").arg(program.parameterNames.at(parameterIndex)),
                             variableForIndex(parameterIndex));
            }
            library.append(line);
        }
        library.append(QStringLiteral("M99"));
        ++routineNumber;
    }

    output.ok = true;
    output.callText = calls.join(QLatin1Char('\n'));
    output.libraryText = library.isEmpty()
        ? QString()
        : library.join(QLatin1Char('\n')) + QLatin1Char('\n');
    return output;
}
