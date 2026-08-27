#include "ToolLibraryPanel.h"

#include "../core/Settings.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

static bool isChineseUi()
{
    return Settings::instance().language() == QStringLiteral("zh_CN");
}

static QString toolTypeLabel(const QString &type)
{
    const bool zh = isChineseUi();
    if (type == QStringLiteral("drill"))        return zh ? QStringLiteral("钻头")   : QStringLiteral("Drill");
    if (type == QStringLiteral("tap"))          return zh ? QStringLiteral("丝锥")   : QStringLiteral("Tap");
    if (type == QStringLiteral("reamer"))       return zh ? QStringLiteral("铰刀")   : QStringLiteral("Reamer");
    if (type == QStringLiteral("boring_bar"))   return zh ? QStringLiteral("镗刀杆") : QStringLiteral("Boring Bar");
    if (type == QStringLiteral("thread_mill"))  return zh ? QStringLiteral("螺纹铣刀") : QStringLiteral("Thread Mill");
    if (type == QStringLiteral("spot_drill"))   return zh ? QStringLiteral("中心钻") : QStringLiteral("Spot Drill");
    if (type == QStringLiteral("end_mill"))     return zh ? QStringLiteral("立铣刀") : QStringLiteral("End Mill");
    if (type == QStringLiteral("ball_end_mill")) return zh ? QStringLiteral("球头铣刀") : QStringLiteral("Ball End Mill");
    if (type == QStringLiteral("chamfer_mill")) return zh ? QStringLiteral("倒角刀") : QStringLiteral("Chamfer Mill");
    return type;
}

// All known tool type keys in order
static const char * const kTypeKeys[] = {
    "drill", "tap", "reamer", "boring_bar", "thread_mill", "spot_drill", "end_mill", "ball_end_mill", "chamfer_mill"
};
static const int kTypeCount = 9;

// Show a dialog to create or edit a ToolEntry.
// Pass a default-constructed entry for "add", or an existing entry for "edit".
// Returns false if the user cancelled.
static bool showToolDialog(QWidget *parent, ToolEntry &entry, bool isEdit)
{
    const bool zh = isChineseUi();

    QDialog dlg(parent);
    dlg.setWindowTitle(isEdit
        ? (zh ? QStringLiteral("编辑刀具") : QStringLiteral("Edit Tool"))
        : (zh ? QStringLiteral("新增刀具") : QStringLiteral("Add Tool")));
    dlg.setMinimumWidth(340);

    auto *form = new QFormLayout;

    // Name
    auto *edName = new QLineEdit(entry.name);
    form->addRow(zh ? QStringLiteral("刀具名称") : QStringLiteral("Name"), edName);

    // Type combo
    auto *cbType = new QComboBox;
    int selectedType = 0;
    for (int i = 0; i < kTypeCount; ++i) {
        cbType->addItem(toolTypeLabel(QLatin1String(kTypeKeys[i])), QLatin1String(kTypeKeys[i]));
        if (entry.type == QLatin1String(kTypeKeys[i]))
            selectedType = i;
    }
    cbType->setCurrentIndex(selectedType);
    form->addRow(zh ? QStringLiteral("刀具类型") : QStringLiteral("Type"), cbType);

    // Diameter
    auto *spDia = new QDoubleSpinBox;
    spDia->setRange(0.1, 300.0);
    spDia->setDecimals(3);
    spDia->setSuffix(QStringLiteral(" mm"));
    spDia->setValue(entry.diameter > 0.0 ? entry.diameter : 6.0);
    form->addRow(zh ? QStringLiteral("直径") : QStringLiteral("Diameter"), spDia);

    // Flute length
    auto *spFlute = new QDoubleSpinBox;
    spFlute->setRange(0.0, 500.0);
    spFlute->setDecimals(1);
    spFlute->setSuffix(QStringLiteral(" mm"));
    spFlute->setValue(entry.fluteLen);
    form->addRow(zh ? QStringLiteral("刃长") : QStringLiteral("Flute Len"), spFlute);

    // Total length
    auto *spTotal = new QDoubleSpinBox;
    spTotal->setRange(0.0, 500.0);
    spTotal->setDecimals(1);
    spTotal->setSuffix(QStringLiteral(" mm"));
    spTotal->setValue(entry.totalLen);
    form->addRow(zh ? QStringLiteral("全长") : QStringLiteral("Total Len"), spTotal);

    // Flutes
    auto *spFlutes = new QSpinBox;
    spFlutes->setRange(1, 12);
    spFlutes->setValue(entry.flutes > 0 ? entry.flutes : 2);
    form->addRow(zh ? QStringLiteral("刃数") : QStringLiteral("Flutes"), spFlutes);

    // Pitch (only meaningful for taps)
    auto *spPitch = new QDoubleSpinBox;
    spPitch->setRange(0.0, 10.0);
    spPitch->setDecimals(3);
    spPitch->setSuffix(QStringLiteral(" mm"));
    spPitch->setValue(entry.pitch);
    form->addRow(zh ? QStringLiteral("螺距(攻丝)") : QStringLiteral("Pitch (tap)"), spPitch);

    // Material
    auto *edMat = new QLineEdit(entry.material.isEmpty() ? QStringLiteral("HSS") : entry.material);
    form->addRow(zh ? QStringLiteral("材质") : QStringLiteral("Material"), edMat);

    auto *edModel = new QLineEdit(entry.modelPath);
    auto *btnBrowseModel = new QPushButton(zh ? QStringLiteral("浏览") : QStringLiteral("Browse"));
    auto *btnClearModel = new QPushButton(zh ? QStringLiteral("清除") : QStringLiteral("Clear"));
    auto *modelRow = new QHBoxLayout;
    modelRow->addWidget(edModel, 1);
    modelRow->addWidget(btnBrowseModel);
    modelRow->addWidget(btnClearModel);
    form->addRow(zh ? QStringLiteral("刀具模型") : QStringLiteral("Tool Model"), modelRow);
    QObject::connect(btnBrowseModel, &QPushButton::clicked, &dlg, [&]() {
        const QString path = QFileDialog::getOpenFileName(
            &dlg,
            zh ? QStringLiteral("选择刀具模型") : QStringLiteral("Select Tool Model"),
            edModel->text(),
            QStringLiteral("Tool Model (*.obj *.stl);;OBJ (*.obj);;STL (*.stl);;All Files (*.*)"));
        if (!path.isEmpty()) {
            edModel->setText(path);
        }
    });
    QObject::connect(btnClearModel, &QPushButton::clicked, edModel, &QLineEdit::clear);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *vl = new QVBoxLayout(&dlg);
    vl->addLayout(form);
    vl->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    const QString nameStr = edName->text().trimmed();
    if (nameStr.isEmpty()) {
        QMessageBox::warning(parent,
            zh ? QStringLiteral("提示") : QStringLiteral("Warning"),
            zh ? QStringLiteral("刀具名称不能为空") : QStringLiteral("Tool name cannot be empty."));
        return false;
    }

    entry.name     = nameStr;
    entry.type     = cbType->currentData().toString();
    entry.diameter = spDia->value();
    entry.fluteLen = spFlute->value();
    entry.totalLen = spTotal->value();
    entry.flutes   = spFlutes->value();
    entry.pitch    = spPitch->value();
    entry.material = edMat->text().trimmed();
    entry.modelPath = edModel->text().trimmed();
    return true;
}

} // namespace

ToolLibraryPanel::ToolLibraryPanel(QWidget *parent)
    : QWidget(parent)
    , m_titleLabel(new QLabel(this))
    , m_list(new QListWidget(this))
    , m_btnAdd(new QPushButton(this))
    , m_btnEdit(new QPushButton(this))
    , m_btnRemove(new QPushButton(this))
{
    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(m_btnAdd);
    btnRow->addWidget(m_btnEdit);
    btnRow->addWidget(m_btnRemove);
    btnRow->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_list);
    layout->addLayout(btnRow);

    connect(m_list, &QListWidget::currentRowChanged,
            this, &ToolLibraryPanel::onSelectionChanged);
    connect(m_btnAdd,    &QPushButton::clicked, this, &ToolLibraryPanel::onAdd);
    connect(m_btnEdit,   &QPushButton::clicked, this, &ToolLibraryPanel::onEdit);
    connect(m_btnRemove, &QPushButton::clicked, this, &ToolLibraryPanel::onRemove);

    retranslateUi();
    refresh();
}

void ToolLibraryPanel::refresh()
{
    const int savedRow = m_list->currentRow();
    m_list->clear();
    for (const ToolEntry &t : ToolLibrary::instance().allTools()) {
        m_list->addItem(QStringLiteral("T%1  %2  %3  %4%5mm")
                            .arg(t.id)
                            .arg(t.name)
                            .arg(toolTypeLabel(t.type))
                            .arg(isChineseUi() ? QStringLiteral("直径") : QStringLiteral("D"))
                            .arg(t.diameter, 0, 'f', 1));
    }
    if (savedRow >= 0 && savedRow < m_list->count())
        m_list->setCurrentRow(savedRow);
}

void ToolLibraryPanel::retranslateUi()
{
    const bool zh = isChineseUi();
    m_titleLabel->setText(zh ? QStringLiteral("刀具库") : QStringLiteral("Tool Library"));
    m_btnAdd->setText(zh    ? QStringLiteral("新增") : QStringLiteral("Add"));
    m_btnEdit->setText(zh   ? QStringLiteral("编辑") : QStringLiteral("Edit"));
    m_btnRemove->setText(zh ? QStringLiteral("删除") : QStringLiteral("Delete"));
    refresh();
}

void ToolLibraryPanel::onSelectionChanged()
{
    int row = m_list->currentRow();
    const QVector<ToolEntry> &tools = ToolLibrary::instance().allTools();
    if (row >= 0 && row < tools.size()) {
        emit toolSelected(tools[row]);
    }
}

void ToolLibraryPanel::onAdd()
{
    ToolEntry entry;
    entry.type     = QStringLiteral("drill");
    entry.diameter = 6.0;
    entry.flutes   = 2;
    entry.material = QStringLiteral("HSS");

    if (!showToolDialog(this, entry, false))
        return;

    ToolLibrary::instance().addTool(entry);
    refresh();
    // Select the newly added tool (last row)
    m_list->setCurrentRow(m_list->count() - 1);
}

void ToolLibraryPanel::onEdit()
{
    int row = m_list->currentRow();
    const QVector<ToolEntry> &tools = ToolLibrary::instance().allTools();
    if (row < 0 || row >= tools.size()) {
        const bool zh = isChineseUi();
        QMessageBox::information(this,
            zh ? QStringLiteral("提示") : QStringLiteral("Info"),
            zh ? QStringLiteral("请先选择要编辑的刀具") : QStringLiteral("Please select a tool to edit."));
        return;
    }

    ToolEntry entry = tools[row];
    if (!showToolDialog(this, entry, true))
        return;

    ToolLibrary::instance().updateTool(entry);
    refresh();
    m_list->setCurrentRow(row);
}

void ToolLibraryPanel::onRemove()
{
    int row = m_list->currentRow();
    const QVector<ToolEntry> &tools = ToolLibrary::instance().allTools();
    if (row < 0 || row >= tools.size())
        return;

    const bool zh = isChineseUi();
    const QString &toolName = tools[row].name;
    const int toolId = tools[row].id;

    QMessageBox::StandardButton btn = QMessageBox::question(this,
        zh ? QStringLiteral("确认删除") : QStringLiteral("Confirm Delete"),
        zh ? QStringLiteral("确定删除刀具 \"%1\"？").arg(toolName)
           : QStringLiteral("Delete tool \"%1\"?").arg(toolName),
        QMessageBox::Yes | QMessageBox::No);

    if (btn != QMessageBox::Yes)
        return;

    ToolLibrary::instance().removeTool(toolId);
    refresh();
    if (row > 0)
        m_list->setCurrentRow(row - 1);
}
