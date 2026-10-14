#include "OperationBatchEditDialog.h"
#include "OperationEditDialog.h"
#include "OperationParameterGroups.h"
#include "../operations/MachiningInput.h"
#include "../../../tool/ToolLibrary.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <cmath>

OperationBatchEditDialog::OperationBatchEditDialog(const QList<MachiningOperation> &operations,
                                                 QWidget *parent)
    : QDialog(parent), m_original(operations), m_result(operations)
{
    setObjectName(QStringLiteral("operationBatchEditDialog"));
    setWindowTitle(tr("批量编辑工序参数"));
    setModal(false);
    resize(510, 630);
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(tr("已选择 %1 个工序。只修改勾选字段；任一工序不符合限制时整批不提交。").arg(operations.size()), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *content = new QWidget(this);
    auto *form = new QFormLayout(content);
    if (!operations.isEmpty()) {
        const auto &first = operations.first().params;
        for (auto it = first.values.cbegin(); it != first.values.cend(); ++it) {
            const auto contract = operationParameterContract(operations.first().strategyId, it.key());
            if (!contract.editable) continue;
            bool common = true, mixed = false;
            for (const auto &operation : operations) {
                bool numeric = false;
                operation.params.values.value(it.key()).toDouble(&numeric);
                common &= numeric && operation.params.values.contains(it.key());
                const auto other = operationParameterContract(operation.strategyId, it.key());
                common &= other.editable && other.unit == contract.unit
                    && other.options == contract.options && other.optionOffset == contract.optionOffset;
                mixed |= operation.params.values.value(it.key()) != it.value();
            }
            if (!common) continue;
            const QString unit = contract.unit.isEmpty() ? QString() : " (" + contract.unit + ")";
            auto *selected = new QCheckBox(operationEditParameterLabel(it.key()) + unit, content);
            selected->setObjectName("batchParameterSelect_" + it.key());
            m_selectedFields.insert(it.key(), selected);
            if (!contract.options.isEmpty()) {
                auto *value = new QComboBox(content);
                value->setObjectName("batchParameterValue_" + it.key());
                for (int index = 0; index < contract.options.size(); ++index)
                    value->addItem(contract.options[index], index + contract.optionOffset);
                value->setCurrentIndex(mixed ? -1 : value->findData(it.value().toInt()));
                value->setEnabled(false);
                connect(selected, &QCheckBox::toggled, value, &QWidget::setEnabled);
                m_options.insert(it.key(), value);
                form->addRow(selected, value);
                continue;
            }
            auto *value = new QLineEdit(content);
            selected->setObjectName("batchParameterSelect_" + it.key());
            value->setObjectName("batchParameterValue_" + it.key());
            value->setEnabled(false);
            if (mixed) value->setPlaceholderText(tr("各工序值不同，请输入新值"));
            else value->setText(QString::number(it.value().toDouble(), 'g', 17));
            connect(selected, &QCheckBox::toggled, value, &QWidget::setEnabled);
            m_selectedFields.insert(it.key(), selected);
            m_values.insert(it.key(), value);
            form->addRow(selected, value);
        }
    }
    m_changeTool = new QCheckBox(tr("更换刀具"), content);
    m_changeTool->setObjectName(QStringLiteral("batchChangeTool"));
    m_tools = new QComboBox(content);
    m_tools->setObjectName(QStringLiteral("batchEditTool"));
    for (const auto &tool : ToolLibrary::instance().allTools())
        m_tools->addItem(QString("T%1 %2 Ø%3").arg(tool.id).arg(tool.name).arg(tool.diameter), tool.id);
    if (!operations.isEmpty()) m_tools->setCurrentIndex(m_tools->findData(operations.first().toolId));
    m_tools->setEnabled(false);
    connect(m_changeTool, &QCheckBox::toggled, m_tools, &QWidget::setEnabled);
    form->addRow(m_changeTool, m_tools);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    layout->addWidget(scroll);
    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("batchEditError"));
    m_error->setWordWrap(true);
    m_error->setStyleSheet("color: #b42318;");
    layout->addWidget(m_error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确认修改"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &OperationBatchEditDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void OperationBatchEditDialog::accept()
{
    StrategyParams patch;
    for (auto it = m_selectedFields.cbegin(); it != m_selectedFields.cend(); ++it) {
        if (!it.value()->isChecked()) continue;
        bool numeric = false;
        const auto *combo = m_options.value(it.key());
        const double value = combo ? combo->currentData().toDouble(&numeric)
                                   : m_values.value(it.key())->text().toDouble(&numeric);
        const auto contract = operationParameterContract(m_original.first().strategyId, it.key());
        if (!numeric || !contract.accepts(value)) {
            m_error->setText(tr("%1 需要合法选项或范围内的有限数值。").arg(operationEditParameterLabel(it.key())));
            return;
        }
        patch.set(it.key(), value);
    }
    if (m_original.isEmpty() || (patch.values.isEmpty() && !m_changeTool->isChecked())) {
        m_error->setText(tr("请选择工序及需要修改的字段。"));
        return;
    }
    QList<MachiningOperation> candidates;
    for (const auto &original : m_original) {
        auto operation = original;
        for (auto it = patch.values.cbegin(); it != patch.values.cend(); ++it)
            operation.params.values.insert(it.key(), it.value());
        if (m_changeTool->isChecked()) operation.toolId = m_tools->currentData().toInt();
        const QString error = operationEditError(original, operation.params, operation.toolId);
        if (!error.isEmpty()) {
            m_error->setText(tr("工序 %1（%2）：%3").arg(operation.id, operation.featureRef, error));
            return;
        }
        if (operation.opType != OperationType::Hole)
            operation.contourFeature = applyMachiningParameters(operation.contourFeature, operation.params);
        operation.markToolpathStale(tr("批量参数已修改，请重新计算"));
        candidates.append(operation);
    }
    m_result = candidates;
    QDialog::accept();
}
