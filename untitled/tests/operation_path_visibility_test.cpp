#include "../src/workspaces/program_verification/ui/OperationPathVisibility.h"
#include <QCoreApplication>
#include <iostream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString code = "G0 Z10\n; ---- Operation 1 [op:a] ----\nG1 X1\n; ---- Operation 2 [op:b] [op:c] ----\nG1 X2\nG1 X3\n";
    const QVector<int> lines{0,2,4,5,-1};
    auto mask = operationPathVisibility(code, lines, {"a", "b"});
    if (mask != QVector<bool>({true,false,true,true,true})) return 1;
    mask = operationPathVisibility(code, lines, {"a", "b", "c"});
    if (mask != QVector<bool>({true,false,false,false,true})) return 2;
    if (operationPathVisibility("G0 Z10\nG1 X1", {0,1}, {"a"}) != QVector<bool>({true,true})) return 3;
    std::cout << "PASS final source-line ownership, merged batches, unowned motion and stable segment count\n";
    return 0;
}
