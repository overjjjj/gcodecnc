#include "MaterialLibraryDialog.h"
#include "../../../strategies/StrategyFactory.h"
#include "../../../tool/ToolLibrary.h"
#include "../../program_verification/fingerprint/ProgramSnapshotFingerprint.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString parameterLabel(const QString &key) {
    if (key == "spindleSpeed") return QStringLiteral("主轴转速 (rpm)");
    if (key == "feedRate") return QStringLiteral("切削进给 (mm/min)");
    return QStringLiteral("下刀进给 (mm/min)");
}
QString strategyLabel(const QString &id) {
    const auto strategy = StrategyFactory::instance().strategy(id);
    return strategy ? strategy->displayName() : id;
}
}

MaterialLibraryDialog::MaterialLibraryDialog(QWidget *parent, const QString &strategyId,
        const ToolEntry &tool, const StrategyParams &params, const QString &storePath)
    : QDialog(parent), m_store(storePath), m_strategyId(strategyId),
      m_toolFingerprint(ProgramSnapshotFingerprint::toolFingerprint(tool)), m_toolId(tool.id),
      m_original(params.values) {
    setObjectName(QStringLiteral("materialLibraryDialog"));
    setWindowTitle(tr("材料切削建议库"));
    resize(680, 520);
    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("按工件材料、工艺和刀具规格保存用户建议。预览数值后应用到草稿，再确认工序。"), this);
    hint->setWordWrap(true); layout->addWidget(hint);
    m_list = new QListWidget(this);m_list->setObjectName("materialRecommendations");layout->addWidget(m_list, 1);
    m_preview = new QTableWidget(0, 3, this);m_preview->setObjectName("materialRecommendationPreview");
    m_preview->setHorizontalHeaderLabels({tr("参数"), tr("当前草稿"), tr("材料建议")});
    m_preview->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);m_preview->verticalHeader()->hide();
    m_preview->setMaximumHeight(145);layout->addWidget(m_preview);
    m_status = new QLabel(this);m_status->setObjectName("materialRecommendationStatus");
    m_status->setWordWrap(true);layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    m_new = buttons->addButton(tr("新建建议"), QDialogButtonBox::ActionRole);m_new->setObjectName("materialNew");
    m_edit = buttons->addButton(tr("编辑"), QDialogButtonBox::ActionRole);m_edit->setObjectName("materialEdit");
    m_remove = buttons->addButton(tr("删除"), QDialogButtonBox::ActionRole);m_remove->setObjectName("materialDelete");
    m_apply = buttons->addButton(tr("确认应用到草稿"), QDialogButtonBox::ActionRole);m_apply->setObjectName("materialApply");
    m_apply->setVisible(!m_strategyId.isEmpty());layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_new, &QPushButton::clicked, this, [this]() { editSelection(true); });
    connect(m_edit, &QPushButton::clicked, this, [this]() { editSelection(false); });
    connect(m_list, &QListWidget::currentRowChanged, this, [this]() { previewSelection(); });
    connect(m_apply, &QPushButton::clicked, this, [this]() { applySelection(); });
    connect(m_remove, &QPushButton::clicked, this, [this]() {
        if (!m_list->currentItem()) return;
        QString error;
        if (!m_store.remove(m_list->currentItem()->data(Qt::UserRole).toJsonObject().value("id").toString(), &error)) {
            m_status->setText(error);return;
        }
        reload();
    });
    reload();
}

void MaterialLibraryDialog::reload(const QString &selectedId) {
    m_list->clear();QString error;
    const auto rows = m_store.entries(&error);
    m_new->setEnabled(error.isEmpty());
    for (const auto &value : rows) {
        const auto row = value.toObject();
        auto *item = new QListWidgetItem(QStringLiteral("%1 · %2 · %3")
            .arg(row.value("name").toString(), strategyLabel(row.value("strategyId").toString()), row.value("toolName").toString()), m_list);
        item->setData(Qt::UserRole, row);
        if (row.value("id").toString() == selectedId) m_list->setCurrentItem(item);
    }
    previewSelection();
    if (!error.isEmpty()) m_status->setText(error);
}

void MaterialLibraryDialog::previewSelection() {
    m_preview->setRowCount(0);m_apply->setEnabled(false);
    const bool selected = m_list->currentItem();m_edit->setEnabled(selected);m_remove->setEnabled(selected);
    if (!selected) { m_status->setText(m_list->count() ? tr("选择材料建议以查看数值。") : tr("尚无材料建议，点击“新建建议”添加。"));return; }
    const auto row = m_list->currentItem()->data(Qt::UserRole).toJsonObject();
    const auto params = row.value("parameters").toObject();
    for (auto it = params.begin(); it != params.end(); ++it) {
        const int index = m_preview->rowCount();m_preview->insertRow(index);
        m_preview->setItem(index, 0, new QTableWidgetItem(parameterLabel(it.key())));
        m_preview->setItem(index, 1, new QTableWidgetItem(m_original.contains(it.key()) ? QString::number(m_original.value(it.key()).toDouble(), 'g', 12) : QStringLiteral("—")));
        m_preview->setItem(index, 2, new QTableWidgetItem(QString::number(it.value().toDouble(), 'g', 12)));
    }
    if (m_strategyId.isEmpty()) { m_status->setText(tr("在工序参数窗口选择并确认应用建议。"));return; }
    auto proposed = m_original;QString error;
    const bool compatible = m_store.apply(row.value("id").toString(), m_strategyId, m_toolFingerprint, &proposed, &error, row);
    m_apply->setEnabled(compatible);
    m_status->setText(compatible ? tr("确认后仅复制表中切削参数到草稿，仍需确认工序。") : error);
}

void MaterialLibraryDialog::editSelection(bool creating) {
    if (!creating && !m_list->currentItem()) return;
    QJsonObject row = creating ? QJsonObject() : m_list->currentItem()->data(Qt::UserRole).toJsonObject();
    QDialog editor(this);editor.setObjectName("materialRecommendationEditor");editor.setWindowTitle(creating ? tr("新建材料建议") : tr("编辑材料建议"));
    auto *layout = new QVBoxLayout(&editor);auto *form = new QFormLayout;layout->addLayout(form);
    auto *name = new QLineEdit(row.value("name").toString(), &editor);name->setObjectName("materialName");form->addRow(tr("工件材料名称"), name);
    auto *strategy = new QComboBox(&editor);strategy->setObjectName("materialStrategy");strategy->addItem(tr("请选择工艺"), QString());
    for (const auto &id : StrategyFactory::instance().availableIds()) strategy->addItem(strategyLabel(id), id);
    strategy->setCurrentIndex(qMax(0, strategy->findData(creating ? m_strategyId : row.value("strategyId").toString())));form->addRow(tr("适用工艺"), strategy);
    auto *tool = new QComboBox(&editor);tool->setObjectName("materialTool");tool->addItem(tr("请选择当前刀具规格"), 0);
    for (const auto &entry : ToolLibrary::instance().allTools()) {
        tool->addItem(entry.toDisplayString(), entry.id);
        if ((creating && entry.id == m_toolId) || (!creating && ProgramSnapshotFingerprint::toolFingerprint(entry) == row.value("toolFingerprint").toString()))
            tool->setCurrentIndex(tool->count() - 1);
    }
    form->addRow(tr("适用刀具"), tool);
    QMap<QString, QDoubleSpinBox *> fields;
    for (const QString &key : {QStringLiteral("spindleSpeed"), QStringLiteral("feedRate"), QStringLiteral("plungeRate")}) {
        auto *spin = new QDoubleSpinBox(&editor);spin->setObjectName("material_" + key);spin->setRange(0, 1000000);
        spin->setDecimals(key == "spindleSpeed" ? 0 : 3);spin->setSpecialValueText(tr("不提供"));
        spin->setValue(row.value("parameters").toObject().value(key).toDouble());fields.insert(key, spin);form->addRow(parameterLabel(key), spin);
    }
    auto *status = new QLabel(tr("填写用户确认的建议值；“不提供”的字段不会覆盖草稿。"), &editor);status->setWordWrap(true);layout->addWidget(status);
    const auto updateFields = [=]() {
        const auto selected = StrategyFactory::instance().strategy(strategy->currentData().toString());
        const QVariantMap available = selected ? selected->defaultParams().values : QVariantMap();
        for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
            it.value()->setEnabled(available.contains(it.key()));
            if (!it.value()->isEnabled()) it.value()->setValue(0);
        }
    };
    connect(strategy, QOverload<int>::of(&QComboBox::currentIndexChanged), &editor, [=]() { updateFields(); });updateFields();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &editor);layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Save)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    QString savedId;
    connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &editor, [&]() {
        const ToolEntry selectedTool = ToolLibrary::instance().tool(tool->currentData().toInt());
        row["name"] = name->text();row["strategyId"] = strategy->currentData().toString();
        row["toolFingerprint"] = ProgramSnapshotFingerprint::toolFingerprint(selectedTool);row["toolName"] = selectedTool.toDisplayString();
        QJsonObject params;for (auto it = fields.cbegin(); it != fields.cend(); ++it)
            if (it.value()->isEnabled() && it.value()->value() > 0) params[it.key()] = it.value()->value();
        row["parameters"] = params;QString error;savedId = m_store.save(row, &error);
        if (savedId.isEmpty()) { status->setText(error);return; }editor.accept();
    });
    if (editor.exec() == QDialog::Accepted) reload(savedId);
}

void MaterialLibraryDialog::applySelection() {
    if (!m_list->currentItem() || m_strategyId.isEmpty()) return;
    const auto reviewed = m_list->currentItem()->data(Qt::UserRole).toJsonObject();QString error;
    if (ProgramSnapshotFingerprint::toolFingerprint(ToolLibrary::instance().tool(m_toolId)) != m_toolFingerprint) {
        reload();m_status->setText(tr("建议或刀具规格已改变，请重新打开并预览。"));return;
    }
    auto proposed = m_original;
    if (!m_store.apply(reviewed.value("id").toString(), m_strategyId, m_toolFingerprint, &proposed, &error, reviewed)) {
        reload();m_status->setText(error);return;
    }
    m_applied = proposed;accept();
}
