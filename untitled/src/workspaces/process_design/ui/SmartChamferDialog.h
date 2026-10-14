#pragma once

#include "AutoHoleDialog.h"
#include "ToolOperationCompatibility.h"
#include "../../../strategies/hole/ChamferStrategy.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>

class SmartChamferDialog : public QDialog {
public:
    SmartChamferDialog(const QVector<MachiningFeature> &features,
                       const QVector<ToolEntry> &tools, QWidget *parent = nullptr)
        : QDialog(parent), m_groups(autoHoleGroups(features)), m_tools(tools)
    {
        setObjectName("smartChamferDialog");
        setWindowTitle(QStringLiteral("智能倒角 · 孔口建议"));
        resize(860, 540);
        auto *layout = new QVBoxLayout(this);
        auto *intro = new QLabel(QStringLiteral("先选普通孔组，再确认宽度并选择倒角刀。进入参数页后确认才建工序。外形边面与特殊沉头孔暂不支持。"), this);
        intro->setWordWrap(true);layout->addWidget(intro);
        m_table = new QTableWidget(m_groups.size(), 5, this);
        m_table->setObjectName("smartChamferGroups");
        m_table->setHorizontalHeaderLabels({QStringLiteral("孔型"), QStringLiteral("孔径 (mm)"),
            QStringLiteral("深度 (mm)"), QStringLiteral("数量"), QStringLiteral("建议 / 原因")});
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->verticalHeader()->hide();
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
        for (int row = 0; row < m_groups.size(); ++row) {
            const auto &group = m_groups[row];const auto &hole = group.holes.first();
            const QString type = hole.subType == "through_hole" ? QStringLiteral("通孔")
                : hole.subType == "blind_hole" ? QStringLiteral("盲孔") : hole.subType;
            QString reason = group.reason;
            reason.replace(QStringLiteral("基础钻孔"), QStringLiteral("普通孔口倒角"));
            const QStringList values = {type, QString::number(2 * hole.radius, 'g', 17),
                QString::number(hole.depth, 'g', 17), QString::number(group.holes.size()),
                reason.isEmpty() ? QStringLiteral("圆周孔口倒角 · 待选刀与确认宽度") : reason};
            for (int col = 0; col < values.size(); ++col) {
                auto *item = new QTableWidgetItem(values[col]);item->setToolTip(values[col]);
                m_table->setItem(row, col, item);
            }
        }
        layout->addWidget(m_table, 1);
        auto *form = new QFormLayout;
        m_width = new QDoubleSpinBox(this);
        m_width->setObjectName("smartChamferWidth");m_width->setDecimals(3);
        m_width->setRange(.001, 1000);m_width->setValue(1);m_width->setSuffix(" mm");
        form->addRow(QStringLiteral("孔口水平倒角宽度"), m_width);
        m_tool = new QComboBox(this);m_tool->setObjectName("smartChamferTool");
        form->addRow(QStringLiteral("已确认几何的倒角刀"), m_tool);
        layout->addLayout(form);
        m_note = new QLabel(this);m_note->setWordWrap(true);layout->addWidget(m_note);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        m_next = buttons->addButton(QStringLiteral("进入此组参数"), QDialogButtonBox::AcceptRole);
        m_next->setObjectName("smartChamferContinue");m_next->setEnabled(false);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] { refreshTools(false); });
        connect(m_width, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { refreshTools(true); });
        connect(m_tool, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateChoice(); });
        connect(m_next, &QPushButton::clicked, this, [this] {
            const int index = selectedToolIndex();
            const int row = m_table->currentRow();
            if (!m_next->isEnabled() || index < 0 || index >= m_tools.size() || row < 0) return;
            m_selected = m_groups[row].holes;m_selectedTool = m_tools[index];
            m_selectedParams = paramsFor(m_selectedTool);accept();
        });
        layout->addWidget(buttons);refreshTools(false);
    }
    QVector<HoleFeature> selectedHoles() const { return m_selected; }
    ToolEntry selectedTool() const { return m_selectedTool; }
    StrategyParams selectedParams() const { return m_selectedParams; }
private:
    int selectedToolIndex() const {
        const int id = m_tool->currentData().toInt();
        for (int i = 0; i < m_tools.size(); ++i) if (m_tools[i].id == id) return i;
        return -1;
    }
    StrategyParams paramsFor(const ToolEntry &tool) const {
        auto params = ChamferStrategy().defaultParams();
        params.set("chamferWidth", m_width->value());
        params.set("chamferAngle", tool.extra.value("angle").toDouble() * .5);
        return params;
    }
    void refreshTools(bool retainChoice) {
        const int previous = retainChoice ? m_tool->currentData().toInt() : -1;
        m_tool->blockSignals(true);m_tool->clear();
        m_tool->addItem(QStringLiteral("请选择倒角刀"), -1);
        const int row = m_table->currentRow();
        if (row >= 0 && row < m_groups.size() && m_groups[row].reason.isEmpty()) {
            for (int i = 0; i < m_tools.size(); ++i) {
                const auto &tool = m_tools[i];const auto params = paramsFor(tool);
                bool fits = tool.id > 0 && params.get("chamferAngle") < 89.9;
                for (const auto &hole : m_groups[row].holes) {
                    const double depth = holeCuttingDepth("hole_chamfer", hole, params, tool);
                    fits = fits && std::isfinite(depth)
                        && !reviewToolCompatibility("hole_chamfer", tool, hole, true, depth).hasBlockingIssues();
                }
                if (fits) m_tool->addItem(QStringLiteral("T%1 %2 · Ø%3 · 夹角%4° · 刀尖Ø%5")
                    .arg(tool.id).arg(tool.name).arg(tool.diameter)
                    .arg(tool.extra.value("angle").toDouble()).arg(tool.extra.value("tipDiameter").toDouble()), tool.id);
            }
        }
        const int choice = m_tool->findData(previous);
        m_tool->setCurrentIndex(choice < 0 ? 0 : choice);
        m_tool->blockSignals(false);updateChoice();
    }
    void updateChoice() {
        const int index = selectedToolIndex();
        const bool selected = index >= 0 && index < m_tools.size();
        m_next->setEnabled(selected);
        m_note->setText(selected
            ? QStringLiteral("建议半角 %1°（与刀轴）。参数页请核对刀具、宽度、切削用量与安全高度；尚未创建工序。")
                .arg(m_tools[index].extra.value("angle").toDouble() * .5, 0, 'g', 12)
            : m_groups.isEmpty() ? QStringLiteral("当前模型没有识别孔。")
            : m_table->currentRow() < 0 ? QStringLiteral("请选择一组孔，再明确选择倒角刀。")
            : m_tool->count() <= 1 ? QStringLiteral("没有适配整组的刀具。请核对宽度，并在刀具库明确录入夹角、刀尖直径和刃长。")
            : QStringLiteral("请选择倒角刀；缺少刀尖尺寸的旧刀具不参与建议，尖头也必须明确录入0。"));
    }
    QVector<AutoHoleGroup> m_groups;
    QVector<ToolEntry> m_tools;
    QVector<HoleFeature> m_selected;
    ToolEntry m_selectedTool;
    StrategyParams m_selectedParams;
    QTableWidget *m_table;
    QDoubleSpinBox *m_width;
    QComboBox *m_tool;
    QLabel *m_note;
    QPushButton *m_next;
};
