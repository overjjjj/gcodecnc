#include "ParameterEditorDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

static QStringList preferredParamOrder()
{
    return {
        QStringLiteral("safeHeight"),
        QStringLiteral("feedHeight"),
        QStringLiteral("depth"),
        QStringLiteral("stepDown"),
        QStringLiteral("stepover"),
        QStringLiteral("spindleSpeed"),
        QStringLiteral("feedRate"),
        QStringLiteral("plungeRate"),
        QStringLiteral("sideStockToLeave"),
        QStringLiteral("floorStockToLeave"),
        QStringLiteral("stockToLeave"),
        QStringLiteral("slotLength"),
        QStringLiteral("slotWidth"),
        QStringLiteral("angle"),
        QStringLiteral("pitch"),
        QStringLiteral("chamferWidth"),
        QStringLiteral("chamferAngle"),
        QStringLiteral("leadLength"),
        QStringLiteral("compensation"),
        QStringLiteral("helixRadius"),
        QStringLiteral("helixPitch"),
        QStringLiteral("finishStock"),
        QStringLiteral("slopeDirection")
    };
}

} // namespace

ParameterEditorDialog::ParameterEditorDialog(QWidget *parent)
    : QDialog(parent)
    , m_hintLabel(new QLabel(this))
    , m_showAdvancedCheck(new QCheckBox(this))
    , m_table(new QTableWidget(this))
    , m_restoreDefaultsButton(new QPushButton(this))
{
    setModal(true);
    resize(500, 560);

    m_hintLabel->setWordWrap(true);

    m_table->setColumnCount(2);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_hintLabel);
    layout->addWidget(m_showAdvancedCheck);
    layout->addWidget(m_table, 1);

    auto *footerLayout = new QHBoxLayout;
    footerLayout->addWidget(m_restoreDefaultsButton);
    footerLayout->addStretch(1);
    footerLayout->addWidget(buttons);
    layout->addLayout(footerLayout);

    connect(m_showAdvancedCheck, &QCheckBox::toggled,
            this, &ParameterEditorDialog::onShowAdvancedToggled);
    connect(m_restoreDefaultsButton, &QPushButton::clicked,
            this, &ParameterEditorDialog::onRestoreDefaults);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &ParameterEditorDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    setChineseUi(false);
}

void ParameterEditorDialog::setChineseUi(bool enabled)
{
    m_chineseUi = enabled;
    m_hintLabel->setText(
        m_chineseUi
            ? QStringLiteral("前几项为常用参数；勾选后可查看高级参数。")
            : QStringLiteral("Common parameters are shown first. Enable advanced parameters when needed."));
    m_showAdvancedCheck->setText(
        m_chineseUi ? QStringLiteral("显示高级参数") : QStringLiteral("Show advanced parameters"));
    m_restoreDefaultsButton->setText(
        m_chineseUi ? QStringLiteral("恢复默认值") : QStringLiteral("Restore defaults"));
    m_table->setHorizontalHeaderLabels({
        m_chineseUi ? QStringLiteral("参数") : QStringLiteral("Parameter"),
        m_chineseUi ? QStringLiteral("数值") : QStringLiteral("Value")
    });
}

void ParameterEditorDialog::setLabelMap(const QMap<QString, QString> &labels)
{
    m_labels = labels;
}

void ParameterEditorDialog::setBasicKeys(const QSet<QString> &keys)
{
    m_basicKeys = keys;
    syncRowVisibility();
}

void ParameterEditorDialog::setNonNegativeKeys(const QSet<QString> &keys)
{
    m_nonNegativeKeys = keys;
}

void ParameterEditorDialog::setAllowAnyValueKeys(const QSet<QString> &keys)
{
    m_allowAnyValueKeys = keys;
}

void ParameterEditorDialog::setDefaultParams(const StrategyParams &params)
{
    m_defaultParams = params;
}

void ParameterEditorDialog::setParams(const StrategyParams &params)
{
    m_params = params;
    rebuildTable(params);
}

StrategyParams ParameterEditorDialog::params() const
{
    return m_params;
}

void ParameterEditorDialog::onAccept()
{
    const QString error = validationError();
    if (!error.isEmpty()) {
        QMessageBox::warning(this,
                             m_chineseUi ? QStringLiteral("参数无效") : QStringLiteral("Invalid Parameters"),
                             error);
        return;
    }

    StrategyParams updated;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QTableWidgetItem *nameItem = m_table->item(row, 0);
        const QTableWidgetItem *valueItem = m_table->item(row, 1);
        if (!nameItem || !valueItem) {
            continue;
        }
        updated.set(nameItem->data(Qt::UserRole).toString(), valueItem->text().toDouble());
    }
    m_params = updated;
    accept();
}

void ParameterEditorDialog::onRestoreDefaults()
{
    rebuildTable(m_defaultParams.values.isEmpty() ? m_params : m_defaultParams);
}

void ParameterEditorDialog::onShowAdvancedToggled(bool)
{
    syncRowVisibility();
}

void ParameterEditorDialog::rebuildTable(const StrategyParams &params)
{
    m_table->setRowCount(0);

    const QStringList keys = orderedKeys(params);
    int row = 0;
    for (const QString &key : keys) {
        m_table->insertRow(row);
        auto *nameItem = new QTableWidgetItem(labelForKey(key));
        nameItem->setData(Qt::UserRole, key);
        nameItem->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, new QTableWidgetItem(params.values.value(key).toString()));
        ++row;
    }

    const bool hasAdvanced = !m_basicKeys.isEmpty() && m_table->rowCount() > m_basicKeys.size();
    m_showAdvancedCheck->setVisible(hasAdvanced);
    if (!hasAdvanced) {
        m_showAdvancedCheck->setChecked(false);
    }
    syncRowVisibility();
}

void ParameterEditorDialog::syncRowVisibility()
{
    const bool showAdvanced = m_showAdvancedCheck->isChecked();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QTableWidgetItem *nameItem = m_table->item(row, 0);
        const QString key = nameItem ? nameItem->data(Qt::UserRole).toString() : QString();
        const bool isAdvanced = !m_basicKeys.isEmpty() && !m_basicKeys.contains(key);
        m_table->setRowHidden(row, isAdvanced && !showAdvanced);
    }
}

QString ParameterEditorDialog::labelForKey(const QString &key) const
{
    return m_labels.value(key, key);
}

QString ParameterEditorDialog::validationError() const
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QTableWidgetItem *nameItem = m_table->item(row, 0);
        const QTableWidgetItem *valueItem = m_table->item(row, 1);
        if (!nameItem || !valueItem) {
            continue;
        }

        bool ok = false;
        const double value = valueItem->text().toDouble(&ok);
        const QString key = nameItem->data(Qt::UserRole).toString();
        const QString label = nameItem->text();
        if (!ok) {
            return m_chineseUi
                ? QStringLiteral("%1 必须是数字。").arg(label)
                : QStringLiteral("%1 must be numeric.").arg(label);
        }

        if (m_allowAnyValueKeys.contains(key)) {
            continue;
        }
        if (m_nonNegativeKeys.contains(key)) {
            if (value < 0.0) {
                return m_chineseUi
                    ? QStringLiteral("%1 不能小于 0。").arg(label)
                    : QStringLiteral("%1 cannot be less than 0.").arg(label);
            }
            continue;
        }
        if (value <= 0.0) {
            return m_chineseUi
                ? QStringLiteral("%1 必须大于 0。").arg(label)
                : QStringLiteral("%1 must be greater than 0.").arg(label);
        }
    }
    return QString();
}

QStringList ParameterEditorDialog::orderedKeys(const StrategyParams &params) const
{
    QStringList ordered;
    QSet<QString> seen;

    const QStringList preferred = preferredParamOrder();
    for (const QString &key : preferred) {
        if (params.values.contains(key)) {
            ordered.append(key);
            seen.insert(key);
        }
    }

    const QStringList keys = params.values.keys();
    for (const QString &key : keys) {
        if (!seen.contains(key)) {
            ordered.append(key);
        }
    }

    return ordered;
}
