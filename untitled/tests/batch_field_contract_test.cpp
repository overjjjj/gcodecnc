#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <iostream>
#include "../src/workspaces/process_design/operations/MachiningOperation.h"
#define private public
#include "../src/workspaces/process_design/ui/OperationBatchEditDialog.h"
#undef private
#include "../src/workspaces/process_design/ui/OperationEditDialog.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/strategies/StrategyFactory.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char *message) {
        std::cout << (ok ? "PASS " : "FAIL ") << message << std::endl;
        failures += !ok;
    };
    auto operation = [](const QString &strategy) {
        MachiningOperation op;
        op.strategyId = strategy;
        op.params = StrategyFactory::instance().strategy(strategy)->defaultParams();
        op.params.set("depth", 3);
        op.params.set("feedRate", 100);
        return op;
    };
    for (const QString &strategy : {QString("mill_pocket_rough"), QString("hole_tapping"), QString("hole_peck")}) {
        auto op = operation(strategy);
        // Old persisted metadata must not become writable through batch editing.
        for (const QString &key : {QString("depthMode"), QString("pathPattern"), QString("keepIslands")}) op.params.set(key, 0);
        OperationBatchEditDialog dialog({op, op});
        for (const QString &key : {QString("depthMode"), QString("pathPattern"), QString("keepIslands")})
            check(!dialog.findChild<QCheckBox *>("batchParameterSelect_" + key), "metadata remains unavailable in batch");
        check(dialog.findChild<QCheckBox *>("batchParameterSelect_feedRate"), "legal feed remains editable");
        check(dialog.findChild<QCheckBox *>("batchParameterSelect_depth"), "legal depth remains editable");
        if (strategy == "hole_tapping") check(!dialog.findChild<QCheckBox *>("batchParameterSelect_pitch"), "tap pitch comes from tool");
        if (strategy == "hole_peck") check(!dialog.findChild<QCheckBox *>("batchParameterSelect_retractHeight"), "unused peck retract remains unavailable");
    }
    auto spot = operation("hole_spot"), deep = operation("hole_deephole");
    OperationBatchEditDialog mixed({spot, deep});
    check(!mixed.findChild<QCheckBox *>("batchParameterSelect_dwellTime"), "mixed ms and s cannot share dwell input");
    OperationBatchEditDialog same({spot, spot});
    auto *dwell = same.findChild<QCheckBox *>("batchParameterSelect_dwellTime");
    check(dwell && dwell->text().contains("ms"), "same-unit dwell displays its unit");
    auto contour = operation("mill_closed_contour");
    OperationBatchEditDialog enums({contour, contour});
    auto *combo = enums.findChild<QComboBox *>("batchParameterValue_compensation");
    check(combo && combo->count() == 3 && combo->itemData(0).toInt() == -1
          && combo->itemData(2).toInt() == 1, "compensation uses discrete -1 0 1 options");
    if (combo) {
        enums.findChild<QCheckBox *>("batchParameterSelect_compensation")->setChecked(true);
        combo->addItem("invalid", 9);
        combo->setCurrentIndex(combo->count() - 1);
        enums.accept();
        check(enums.result() != QDialog::Accepted
              && enums.findChild<QLabel *>("batchEditError")->text().contains(QString::fromUtf8("合法选项")),
              "invalid enum is rejected before any candidate commit");
    }
    auto malformed = contour;
    malformed.params.values.insert("compensation", "not-an-enum");
    check(operationEditError(malformed, malformed.params, malformed.toolId).contains(QString::fromUtf8("刀补方向")),
          "restored nonnumeric enum is rejected by the shared validator");
    ToolEntry drill;drill.id=611;drill.type="drill";drill.diameter=6;drill.fluteLen=20;drill.totalLen=50;
    ToolLibrary::instance().addTool(drill);
    auto readonly = operation("hole_g81");readonly.toolId=611;readonly.holeFeature.radius=3;
    readonly.holeFeature.depth=6;readonly.holeFeature.axis={0,0,1};readonly.holeFeature.region=FaceRegion::Front;
    readonly.params.set("keepIslands",0);
    OperationBatchEditDialog tampered({readonly});
    auto *selected = new QCheckBox(&tampered);selected->setChecked(true);
    auto *value = new QLineEdit("1", &tampered);
    // Exercise the submission boundary even if a future UI accidentally exposes metadata.
    tampered.m_selectedFields.insert("keepIslands",selected);
    tampered.m_values.insert("keepIslands",value);
    tampered.accept();
    check(tampered.result()!=QDialog::Accepted
          &&tampered.findChild<QLabel *>("batchEditError")->text().contains(QString::fromUtf8("不可编辑"))
          &&tampered.operations().first().params.get("keepIslands")==0,
          "batch submission compares read-only fields against the original operation");
    return failures ? 1 : 0;
}
