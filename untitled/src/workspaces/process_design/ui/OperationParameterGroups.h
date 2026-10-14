#pragma once

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QScrollArea>
#include <QTableWidget>
#include <QTabWidget>
#include <QSet>
#include <QLabel>
#include <QComboBox>
#include <cmath>

// Shared by single-operation and batch editors; persisted metadata stays read-only.
struct OperationParameterContract {
    bool editable = true;
    QStringList options;
    int optionOffset = 0;
    QString unit;

    bool accepts(double value) const
    {
        if (!std::isfinite(value) || value < -1000000 || value > 1000000) return false;
        return options.isEmpty() || (value == std::floor(value)
            && value >= optionOffset && value < optionOffset + options.size());
    }
};

inline OperationParameterContract operationParameterContract(const QString &strategyId,
                                                             const QString &key)
{
    const QSet<QString> metadataOnly = {
        QStringLiteral("coolantMode"), QStringLiteral("depthMode"), QStringLiteral("threadHandedness"),
        QStringLiteral("tapRetract"), QStringLiteral("chipBreakRetract"), QStringLiteral("toolTipRadius"),
        QStringLiteral("retractDelta"), QStringLiteral("fullRetractEvery"), QStringLiteral("cutDirection"),
        QStringLiteral("linkMode"), QStringLiteral("boundingRectangle"), QStringLiteral("pathPattern"),
        QStringLiteral("keepIslands"), QStringLiteral("overcut"), QStringLiteral("startAtMidpoint")
    };
        QStringList options;
        if (key == QStringLiteral("coolantMode")) options = QStringList{QStringLiteral("无"), QStringLiteral("外冷"), QStringLiteral("内冷"), QStringLiteral("吹气"), QStringLiteral("混合")};
        else if (key == QStringLiteral("chamferMode")) options = QStringList{
            QStringLiteral("圆周铣倒角（G1/G2，倒角刀）"),
            QStringLiteral("轴向 G81 锪倒角（锪钻/倒角刀）")};
        else if (key == QStringLiteral("depthMode")) options = QStringList{QStringLiteral("绝对深度")};
        else if (key == QStringLiteral("cutDirection")) options = QStringList{QStringLiteral("内向外"), QStringLiteral("外向内"), QStringLiteral("混合")};
        else if (key == QStringLiteral("linkMode")) options = QStringList{QStringLiteral("直线"), QStringLiteral("圆弧")};
        else if (key == QStringLiteral("boundingRectangle") || key == QStringLiteral("startAtMidpoint")) options = QStringList{QStringLiteral("关闭"), QStringLiteral("开启")};
        else if (key == QStringLiteral("pathPattern")) options = QStringList{QStringLiteral("双向"), QStringLiteral("单向"), QStringLiteral("环切"), QStringLiteral("螺旋")};
        else if (key == QStringLiteral("compensation")) options = QStringList{QStringLiteral("右补偿 G42"),
            strategyId=="mill_closed_contour" || strategyId=="mill_open_contour"
                ? QStringLiteral("CAM几何偏置 G40") : QStringLiteral("不补偿"), QStringLiteral("左补偿 G41")};
        else if (key == "camOffsetSide") options = QStringList{QStringLiteral("右侧"), QStringLiteral("无效"), QStringLiteral("左侧")};

    OperationParameterContract result;
    result.editable = !metadataOnly.contains(key) && key != "entryMode" && key != "workOffset"
        && key != "camOffsetSide"
        && !(strategyId == "hole_peck" && key == "retractHeight")
        && !(strategyId == "hole_tapping" && key == "pitch");
    result.options = options;
    result.optionOffset = key == "compensation" || key == "camOffsetSide" ? -1 : 0;
    if (key == "spindleSpeed") result.unit = "rpm";
    else if (key.endsWith("Rate")) result.unit = "mm/min";
    else if (key == "dwellTime") result.unit = strategyId == "hole_spot"
        || strategyId == "hole_tapping" || strategyId == "hole_reaming" ? "ms" : "s";
    else if (key == "axialDepth") result.unit = "mm";
    return result;
}

// The existing table remains the parameter store used by proposal validation.
inline void rebuildOperationParameterGroups(QTabWidget *tabs, QTableWidget *table,
                                           const QString &strategyId)
{
    const int selected = tabs->currentIndex();
    while (tabs->count()) {
        QWidget *page = tabs->widget(0);
        tabs->removeTab(0);
        delete page;
    }
    const bool hole = strategyId.startsWith(QStringLiteral("hole_"));
    const QString process = hole ? QStringLiteral("循环参数")
        : strategyId == QStringLiteral("mill_face") ? QStringLiteral("平面加工")
        : QStringLiteral("槽加工");
    QList<QFormLayout *> forms;
    for (const QString &title : {QStringLiteral("刀具与用量"), process,
                                QStringLiteral("进退刀"), QStringLiteral("高度")}) {
        auto *scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *page = new QWidget(scroll);
        auto *form = new QFormLayout(page);
        form->setContentsMargins(12, 16, 12, 16);
        form->setVerticalSpacing(12);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        forms.append(form);
        scroll->setWidget(page);
        tabs->addTab(scroll, title);
    }
    bool hasMetadata = false;
    for (int row = 0; row < table->rowCount(); ++row) {
        const auto *name = table->item(row, 0);
        auto *value = table->item(row, 1);
        if (!name || !value) continue;
        const QString key = name->data(Qt::UserRole).toString();
        if (key == QStringLiteral("workOffset")) continue;
        int group = 1;
        if (key == QStringLiteral("spindleSpeed") || key == QStringLiteral("feedRate")) group = 0;
        else if (key.contains(QStringLiteral("Height")) || key == QStringLiteral("depth")
                 || key == QStringLiteral("axialDepth")) group = 3;
        else if (key.contains(QStringLiteral("lead"), Qt::CaseInsensitive)
                 || key.contains(QStringLiteral("ramp"), Qt::CaseInsensitive)
                 || key == QStringLiteral("plungeRate") || key == QStringLiteral("retractDelta")) group = 2;
        const auto contract = operationParameterContract(strategyId, key);
        const auto &options = contract.options;
        if (!options.isEmpty()) {
            auto *combo = new QComboBox(tabs);
            combo->setObjectName(QStringLiteral("operationParameter_") + key);
            const int offset = contract.optionOffset;
            for (int index = 0; index < options.size(); ++index) combo->addItem(options[index], index + offset);
            const int index = combo->findData(value->text().toInt());
            combo->setCurrentIndex(index);
            if (!contract.editable) {
                hasMetadata = hasMetadata || key != "camOffsetSide";
                combo->setEnabled(false);
                combo->setToolTip(key == "camOffsetSide"
                    ? QStringLiteral("由已确认加工侧与点序决定；更改需重新选择并确认轮廓。")
                    : QStringLiteral("尚未接入刀路或输出"));
            }
            if (key == QStringLiteral("chamferMode")) {
                combo->setToolTip(QStringLiteral("方式 0 沿孔口圆周铣削；方式 1 沿孔轴执行 G81。两种方式共用当前侧栏选孔集合。"));
            }
            QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), table, [table, row, combo](int) {
                table->item(row, 1)->setText(QString::number(combo->currentData().toInt()));
            });
            forms.at(group)->addRow(name->text().section(QStringLiteral("（"), 0, 0), combo);
            continue;
        }
        auto *spin = new QDoubleSpinBox(tabs);
        spin->setObjectName(QStringLiteral("operationParameter_") + key);
        spin->setDecimals(key == "chamferAngle" ? 6 : 3);
        spin->setRange(-1000000, 1000000);
        spin->setValue(value->text().toDouble());
        spin->setMinimumHeight(30);
        if (!contract.editable) {
            hasMetadata = true;
            spin->setEnabled(false);
            spin->setToolTip(key == QStringLiteral("pitch")
                ? QStringLiteral("攻丝螺距使用所选刀具的螺距，此参数不覆盖刀具。")
                : QStringLiteral("此设置尚未接入刀路或输出，仅保留已有值。"));
        }
        if (!contract.unit.isEmpty()) spin->setSuffix(" " + contract.unit);
        if (key == QStringLiteral("axialDepth")) {
            spin->setToolTip(QStringLiteral("轴向 G81 方式按用户确认的轴向锪入深度加工；当前不根据倒角宽度自动补偿刀尖。"));
        }
        else if (key == QStringLiteral("dwellTime")) {
            if (hole && strategyId != QStringLiteral("hole_spot"))
                spin->setToolTip(QStringLiteral("此停留参数仅 Siemens 循环输出支持；CQ8/Fanuc 当前不输出底部停留。"));
        }
        QString label = name->text();
        if (key == "chamferAngle") {
            label = QStringLiteral("圆周倒角半角（与刀轴）");
            spin->setToolTip(QStringLiteral("等于已确认倒角刀夹角的一半；轴向G81方式不使用。"));
        } else if (key == "safeHeight") {
            label = QStringLiteral("安全高度（工作坐标绝对Z）");
            spin->setToolTip(QStringLiteral("相对于已确认工件原点的绝对Z值；更改原点后请重新核对净空。"));
        } else if (key == "feedHeight") {
            label = QStringLiteral("进给净空（距加工顶面）");
        }
        if (key == QStringLiteral("peckDepth")) label = strategyId == "hole_peck"
            ? QStringLiteral("单次深度上限 Q (mm)") : QStringLiteral("单次深度 Q (mm)");
        QObject::connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), table,
                         [table, row](double number) {
            table->item(row, 1)->setText(QString::number(number, 'g', 12));
        });
        forms.at(group)->addRow(label, spin);
    }
    if (hasMetadata) {
        auto *note = new QLabel(QStringLiteral("灰色字段尚未接入刀路或输出。"), tabs);
        note->setWordWrap(true);
        forms.at(1)->addRow(note);
    }
    if (strategyId == QStringLiteral("hole_chamfer")) {
        auto *note = new QLabel(QStringLiteral("圆周铣倒角使用 G1/G2；轴向 G81 锪倒角使用锪钻/倒角刀，并严格按用户确认的轴向深度加工。"), tabs);
        note->setWordWrap(true);
        forms.at(1)->addRow(note);
    }
    tabs->setCurrentIndex(selected < 0 ? 1 : qMin(selected, tabs->count() - 1));
}

