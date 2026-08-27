#include "OperationListPanel.h"

#include "../core/OperationWorkflow.h"
#include "../core/Settings.h"
#include "ParameterEditorDialog.h"
#include "../tool/ToolLibrary.h"
#include "../strategies/StrategyFactory.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QUuid>
#include <algorithm>

namespace {

static bool isChineseUi()
{
    return Settings::instance().language() == QStringLiteral("zh_CN");
}

static QString strategyTypeText(const QString &strategyId, bool zh)
{
    if (strategyId == QStringLiteral("mill_face")) {
        return zh ? QStringLiteral("铣平面") : QStringLiteral("Face Mill");
    }
    if (strategyId == QStringLiteral("mill_pocket_rough")) {
        return zh ? QStringLiteral("型腔开粗") : QStringLiteral("Pocket Rough");
    }
    if (strategyId == QStringLiteral("mill_contour_finish")) {
        return zh ? QStringLiteral("轮廓精加工") : QStringLiteral("Contour Finish");
    }
    if (strategyId == QStringLiteral("mill_surface_finish")) {
        return zh ? QStringLiteral("曲面精加工") : QStringLiteral("Surface Finish");
    }
    if (strategyId == QStringLiteral("mill_circle")) {
        return zh ? QStringLiteral("封闭圆铣边") : QStringLiteral("Circle Mill");
    }
    if (strategyId == QStringLiteral("mill_slot")) {
        return zh ? QStringLiteral("开口槽铣削") : QStringLiteral("Open Slot");
    }
    if (strategyId == QStringLiteral("mill_blind_slot")) {
        return zh ? QStringLiteral("盲槽铣削") : QStringLiteral("Blind Slot");
    }
    if (strategyId == QStringLiteral("mill_tapered_slot")) {
        return zh ? QStringLiteral("斜底槽铣削") : QStringLiteral("Tapered Slot");
    }
    if (strategyId == QStringLiteral("mill_closed_contour")) {
        return zh ? QStringLiteral("封闭轮廓铣") : QStringLiteral("Closed Contour");
    }
    if (strategyId == QStringLiteral("mill_open_contour")) {
        return zh ? QStringLiteral("开放轮廓铣") : QStringLiteral("Open Contour");
    }
    if (strategyId == QStringLiteral("mill_outer_chamfer")) {
        return zh ? QStringLiteral("二维外形倒角") : QStringLiteral("2D Outer Chamfer");
    }
    if (strategyId == QStringLiteral("mill_slope_plane_2d")) {
        return zh ? QStringLiteral("二维斜面铣") : QStringLiteral("2D Planar Slope");
    }
    return QString();
}

static QString operationTypeText(const MachiningOperation &op, bool zh)
{
    const QString byStrategy = strategyTypeText(op.strategyId, zh);
    if (!byStrategy.isEmpty()) {
        return byStrategy;
    }

    switch (op.opType) {
    case OperationType::Roughing:
        return zh ? QStringLiteral("粗加工") : QStringLiteral("Roughing");
    case OperationType::SemiFinish:
        return zh ? QStringLiteral("半精加工") : QStringLiteral("Semi Finish");
    case OperationType::Finish:
        return zh ? QStringLiteral("精加工") : QStringLiteral("Finish");
    case OperationType::Hole:
        return zh ? QStringLiteral("孔加工") : QStringLiteral("Hole");
    case OperationType::Contour:
        return zh ? QStringLiteral("轮廓") : QStringLiteral("Contour");
    }
    return zh ? QStringLiteral("工序") : QStringLiteral("Operation");
}

static int stageOrder(OperationStage stage)
{
    switch (stage) {
    case OperationStage::Setup:     return 0;
    case OperationStage::RoughCut:  return 1;
    case OperationStage::DeepHole:  return 2;
    case OperationStage::FinishCut: return 3;
    case OperationStage::Cleanup:   return 4;
    }
    return 1;
}

static QString operationStageText(OperationStage stage, bool zh)
{
    switch (stage) {
    case OperationStage::Setup:
        return zh ? QStringLiteral("对刀/准备") : QStringLiteral("Setup");
    case OperationStage::RoughCut:
        return zh ? QStringLiteral("粗切") : QStringLiteral("Rough");
    case OperationStage::FinishCut:
        return zh ? QStringLiteral("精修") : QStringLiteral("Finish");
    case OperationStage::DeepHole:
        return zh ? QStringLiteral("深孔") : QStringLiteral("Deep Hole");
    case OperationStage::Cleanup:
        return zh ? QStringLiteral("收尾") : QStringLiteral("Cleanup");
    }
    return zh ? QStringLiteral("粗切") : QStringLiteral("Rough");
}

static QString toolpathStateText(ToolpathState state, bool zh)
{
    switch (state) {
    case ToolpathState::Empty:
        return zh ? QStringLiteral("未生成") : QStringLiteral("Empty");
    case ToolpathState::Calculating:
        return zh ? QStringLiteral("计算中") : QStringLiteral("Calculating");
    case ToolpathState::Valid:
        return zh ? QStringLiteral("有效") : QStringLiteral("Valid");
    case ToolpathState::Stale:
        return zh ? QStringLiteral("需重算") : QStringLiteral("Stale");
    case ToolpathState::Error:
        return zh ? QStringLiteral("错误") : QStringLiteral("Error");
    }
    return zh ? QStringLiteral("未生成") : QStringLiteral("Empty");
}

static FaceRegion operationRegion(const MachiningOperation &op)
{
    return op.opType == OperationType::Hole
        ? op.holeFeature.region
        : op.contourFeature.region;
}

static int activeRegionOrder(const MachiningOperation &op, FaceRegion activeRegion)
{
    if (activeRegion == FaceRegion::Unknown) {
        return 0;
    }
    return operationRegion(op) == activeRegion ? 0 : 1;
}

} // namespace

OperationListPanel::OperationListPanel(QWidget *parent)
    : QWidget(parent)
    , m_titleLabel(new QLabel(this))
    , m_summaryLabel(new QLabel(this))
    , m_table(new QTableWidget(this))
    , m_btnUp(new QToolButton(this))
    , m_btnDown(new QToolButton(this))
    , m_btnDelete(new QToolButton(this))
    , m_btnApplyTool(new QToolButton(this))
    , m_btnSortStage(new QToolButton(this))
    , m_btnEditParameters(new QToolButton(this))
    , m_btnRecalculate(new QToolButton(this))
    , m_btnGenerateProgram(new QPushButton(this))
{
    setObjectName(QStringLiteral("operationListPanel"));
    setStyleSheet(QStringLiteral(
        "#operationListPanel QLabel#operationTitleLabel { color: #172033; font-size: 15px; font-weight: 700; }"
        "#operationListPanel QLabel#operationSummaryLabel { color: #607089; }"
        "#operationListPanel QToolButton { min-height: 28px; border: 1px solid #c6d0df; border-radius: 5px; padding: 3px 7px; background: #ffffff; color: #27364d; }"
        "#operationListPanel QToolButton:hover { background: #f3f6fb; border-color: #9fb0c8; }"
        "#operationListPanel QPushButton#operationPrimaryAction { min-height: 36px; border: 1px solid #2f6fec; border-radius: 6px; padding: 6px 12px; background: #2f6fec; color: #ffffff; font-weight: 600; }"
        "#operationListPanel QPushButton#operationPrimaryAction:hover { background: #245fd0; }"
        "#operationListPanel QPushButton#operationPrimaryAction:disabled { background: #b8c6e3; border-color: #b8c6e3; color: #f8fbff; }"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *heading = new QHBoxLayout;
    m_titleLabel->setObjectName(QStringLiteral("operationTitleLabel"));
    m_summaryLabel->setObjectName(QStringLiteral("operationSummaryLabel"));
    heading->addWidget(m_titleLabel);
    heading->addStretch(1);
    heading->addWidget(m_summaryLabel);
    layout->addLayout(heading);

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(m_btnUp);
    toolbar->addWidget(m_btnDown);
    toolbar->addWidget(m_btnDelete);
    toolbar->addWidget(m_btnApplyTool);
    toolbar->addWidget(m_btnSortStage);
    toolbar->addWidget(m_btnEditParameters);
    toolbar->addWidget(m_btnRecalculate);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    m_table->setColumnCount(6);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);

    m_btnGenerateProgram->setObjectName(QStringLiteral("operationPrimaryAction"));
    layout->addWidget(m_btnGenerateProgram);

    retranslateUi();

    connect(m_btnUp, &QToolButton::clicked, this, &OperationListPanel::onMoveUp);
    connect(m_btnDown, &QToolButton::clicked, this, &OperationListPanel::onMoveDown);
    connect(m_btnDelete, &QToolButton::clicked, this, &OperationListPanel::onDelete);
    connect(m_btnApplyTool, &QToolButton::clicked, this, &OperationListPanel::applyCurrentToolRequested);
    connect(m_btnSortStage, &QToolButton::clicked, this, &OperationListPanel::onSortByStage);
    connect(m_btnEditParameters, &QToolButton::clicked,
            this, &OperationListPanel::onEditParameters);
    connect(m_btnRecalculate, &QToolButton::clicked, this, &OperationListPanel::onRecalculate);
    connect(m_btnGenerateProgram, &QPushButton::clicked,
            this, &OperationListPanel::onGenerateProgram);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &OperationListPanel::onSelectionChanged);
    connect(m_table, &QTableWidget::itemChanged,
            this, &OperationListPanel::onItemChanged);

    onSelectionChanged();
}

void OperationListPanel::setOperations(const QList<MachiningOperation> &operations)
{
    m_operations = operations;
    for (MachiningOperation &op : m_operations) {
        if (op.id.trimmed().isEmpty()) {
            op.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
    }
    refreshTable();
}

int OperationListPanel::currentOperationNumber() const
{
    const int row = m_table->currentRow();
    return row >= 0 ? row + 1 : -1;
}

QString OperationListPanel::currentOperationId() const
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_operations.size()) {
        return QString();
    }
    return m_operations[row].id;
}

bool OperationListPanel::selectOperationById(const QString &operationId)
{
    if (operationId.trimmed().isEmpty()) {
        return false;
    }
    for (int row = 0; row < m_operations.size(); ++row) {
        if (m_operations[row].id == operationId) {
            m_table->selectRow(row);
            return true;
        }
    }
    return false;
}

QStringList OperationListPanel::addConfirmedOperations(
    const QList<MachiningOperation> &operations)
{
    QStringList addedIds;
    for (MachiningOperation operation : operations) {
        if (operation.id.trimmed().isEmpty()) {
            continue;
        }
        if (operation.featureRef.trimmed().isEmpty()) {
            if (operation.opType == OperationType::Hole) {
                operation.featureRef = QStringLiteral("D%1 Z-%2")
                    .arg(operation.holeFeature.radius * 2.0, 0, 'f', 1)
                    .arg(operation.holeFeature.depth, 0, 'f', 1);
            } else {
                auto strategy = StrategyFactory::instance().strategy(operation.strategyId);
                const QString strategyName = strategy
                    ? strategy->displayName()
                    : operation.strategyId;
                if (operation.strategyId == QStringLiteral("mill_slot") ||
                    operation.strategyId == QStringLiteral("mill_blind_slot") ||
                    operation.strategyId == QStringLiteral("mill_tapered_slot")) {
                    const double slotLength = operation.params.get(
                        QStringLiteral("slotLength"), operation.contourFeature.radius * 2.0);
                    const double slotWidth = operation.params.get(
                        QStringLiteral("slotWidth"), operation.contourFeature.radius);
                    operation.featureRef = QStringLiteral("%1 L%2 W%3 Z-%4")
                        .arg(strategyName)
                        .arg(slotLength, 0, 'f', 1)
                        .arg(slotWidth, 0, 'f', 1)
                        .arg(operation.contourFeature.depth, 0, 'f', 1);
                } else {
                    operation.featureRef = QStringLiteral("%1 R%2 Z-%3")
                        .arg(strategyName)
                        .arg(operation.contourFeature.radius, 0, 'f', 1)
                        .arg(operation.contourFeature.depth, 0, 'f', 1);
                }
            }
        }
        addedIds.append(operation.id);
        m_operations.append(operation);
    }
    if (addedIds.isEmpty()) {
        return addedIds;
    }
    refreshTable();
    emit operationsEdited(m_operations);
    return addedIds;
}

bool OperationListPanel::applyToolToSelection(int toolId)
{
    if (toolId <= 0 || m_operations.isEmpty()) {
        return false;
    }

    QList<int> rows;
    for (const QModelIndex &index : m_table->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    if (rows.isEmpty() && m_table->currentRow() >= 0) {
        rows.append(m_table->currentRow());
    }
    if (rows.isEmpty()) {
        for (int row = 0; row < m_operations.size(); ++row) {
            rows.append(row);
        }
    }

    for (int row : rows) {
        if (row >= 0 && row < m_operations.size()) {
            m_operations[row].toolId = toolId;
            m_operations[row].markToolpathStale(QStringLiteral("tool changed"));
        }
    }
    refreshTable();
    emit operationsEdited(m_operations);
    return true;
}

bool OperationListPanel::updateOperationParameters(
    const QString &operationId,
    const StrategyParams &params)
{
    if (!::updateOperationParameters(&m_operations, operationId, params)) {
        return false;
    }
    refreshTable();
    selectOperationById(operationId);
    emit operationsEdited(m_operations);
    return true;
}

bool OperationListPanel::setToolpathResult(const QString &operationId,
                                           bool success,
                                           const QString &message)
{
    for (MachiningOperation &operation : m_operations) {
        if (operation.id != operationId) {
            continue;
        }
        if (success) {
            operation.markToolpathValid();
        } else {
            operation.markToolpathError(message);
        }
        refreshTable();
        emit operationsEdited(m_operations);
        return true;
    }
    return false;
}

void OperationListPanel::setActiveRegion(FaceRegion region)
{
    m_activeRegion = region;
}

void OperationListPanel::refreshTable()
{
    const QSignalBlocker blocker(m_table);
    m_table->setRowCount(0);
    const bool zh = isChineseUi();
    int row = 0;
    for (const MachiningOperation &op : m_operations) {
        m_table->insertRow(row);
        auto *orderItem = new QTableWidgetItem(QString::number(row + 1));
        orderItem->setFlags(orderItem->flags() | Qt::ItemIsUserCheckable);
        orderItem->setCheckState(op.enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(row, 0, orderItem);
        m_table->setItem(row, 1, new QTableWidgetItem(operationTypeText(op, zh)));
        m_table->setItem(row, 2, new QTableWidgetItem(operationStageText(op.stage, zh)));
        m_table->setItem(row, 3, new QTableWidgetItem(operationLabel(op)));

        const ToolEntry tool = ToolLibrary::instance().tool(op.toolId);
        const QString toolText = tool.id > 0
            ? QStringLiteral("T%1 D%2").arg(tool.id).arg(tool.diameter, 0, 'f', 1)
            : QStringLiteral("-");
        auto *stateItem = new QTableWidgetItem(toolpathStateText(op.toolpathState, zh));
        stateItem->setToolTip(op.warnings.join(QLatin1Char('\n')));
        m_table->setItem(row, 4, stateItem);
        m_table->setItem(row, 5, new QTableWidgetItem(toolText));
        ++row;
    }
    updateSummary();
    onSelectionChanged();
}

void OperationListPanel::updateSummary()
{
    if (!m_summaryLabel) {
        return;
    }
    const bool zh = isChineseUi();
    const int enabledCount = enabledOperations(m_operations).size();
    m_summaryLabel->setText(
        zh ? QStringLiteral("%1/%2 道启用").arg(enabledCount).arg(m_operations.size())
           : QStringLiteral("%1/%2 enabled").arg(enabledCount).arg(m_operations.size()));
}

QString OperationListPanel::operationLabel(const MachiningOperation &op) const
{
    if (op.opType == OperationType::Hole) {
        return QStringLiteral("D%1 Z-%2")
            .arg(op.holeFeature.radius * 2.0, 0, 'f', 1)
            .arg(op.holeFeature.depth, 0, 'f', 1);
    }

    if (op.strategyId == QStringLiteral("mill_slot") ||
        op.strategyId == QStringLiteral("mill_blind_slot") ||
        op.strategyId == QStringLiteral("mill_tapered_slot")) {
        const double length = op.params.get(QStringLiteral("slotLength"),
                                            op.contourFeature.length > 0.0 ? op.contourFeature.length
                                                                           : op.contourFeature.radius * 2.0);
        const double width = op.params.get(QStringLiteral("slotWidth"),
                                           op.contourFeature.width > 0.0 ? op.contourFeature.width
                                                                         : op.contourFeature.radius);
        return QStringLiteral("L%1 W%2 Z-%3")
            .arg(length, 0, 'f', 1)
            .arg(width, 0, 'f', 1)
            .arg(op.contourFeature.depth, 0, 'f', 1);
    }

    if (!op.featureRef.isEmpty()) {
        return op.featureRef;
    }
    return op.strategyId;
}

void OperationListPanel::onMoveUp()
{
    const int row = m_table->currentRow();
    if (row <= 0) {
        return;
    }
    m_operations.swapItemsAt(row, row - 1);
    refreshTable();
    m_table->selectRow(row - 1);
    emit operationsEdited(m_operations);
}

void OperationListPanel::onMoveDown()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_operations.size() - 1) {
        return;
    }
    m_operations.swapItemsAt(row, row + 1);
    refreshTable();
    m_table->selectRow(row + 1);
    emit operationsEdited(m_operations);
}

void OperationListPanel::onDelete()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_operations.size()) {
        return;
    }
    const bool zh = isChineseUi();
    const QString label = operationLabel(m_operations[row]);
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        zh ? QStringLiteral("确认删除工序") : QStringLiteral("Confirm Operation Deletion"),
        zh ? QStringLiteral("删除工序“%1”？关联程序将需要重新生成。")
                 .arg(label)
           : QStringLiteral("Delete operation \"%1\"? Related programs will need to be regenerated.")
                 .arg(label),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }
    m_operations.removeAt(row);
    refreshTable();
    emit operationsEdited(m_operations);
}

void OperationListPanel::onSortByStage()
{
    std::stable_sort(m_operations.begin(), m_operations.end(),
                     [this](const MachiningOperation &a, const MachiningOperation &b) {
        const int activeA = activeRegionOrder(a, m_activeRegion);
        const int activeB = activeRegionOrder(b, m_activeRegion);
        if (activeA != activeB) {
            return activeA < activeB;
        }
        return stageOrder(a.stage) < stageOrder(b.stage);
    });
    refreshTable();
    emit operationsEdited(m_operations);
}

void OperationListPanel::onEditParameters()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_operations.size()) {
        return;
    }
    const MachiningOperation &operation = m_operations.at(row);
    const std::shared_ptr<StrategyBase> strategy =
        StrategyFactory::instance().strategy(operation.strategyId);
    if (!strategy) {
        return;
    }

    ParameterEditorDialog dialog(this);
    dialog.setChineseUi(isChineseUi());
    dialog.setParameterSchema(strategy->parameterSchema());
    dialog.setParameterSources(operation.parameterSources);
    dialog.setTemplateParams(operation.parameterTemplate.values,
                             operation.parameterTemplate.id,
                             operation.parameterTemplate.version,
                             operation.parameterTemplate.sources);
    dialog.setDefaultParams(strategy->defaultParams());
    dialog.setParams(operation.params);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    updateOperationParameters(operation.id, dialog.params());
}

void OperationListPanel::onRecalculate()
{
    QList<MachiningOperation> selected;
    for (const QModelIndex &index : m_table->selectionModel()->selectedRows()) {
        const int row = index.row();
        if (row >= 0 && row < m_operations.size() && m_operations.at(row).enabled) {
            selected.append(m_operations.at(row));
        }
    }
    if (selected.isEmpty()) {
        const int row = m_table->currentRow();
        if (row >= 0 && row < m_operations.size() && m_operations.at(row).enabled) {
            selected.append(m_operations.at(row));
        }
    }
    if (!selected.isEmpty()) {
        emit recalculateRequested(selected);
    }
}

void OperationListPanel::onGenerateProgram()
{
    const QList<MachiningOperation> executable = executableOperations(m_operations);
    if (!executable.isEmpty()) {
        emit generateProgramRequested(executable);
    }
}

void OperationListPanel::onItemChanged(QTableWidgetItem *item)
{
    if (!item || item->column() != 0 ||
        item->row() < 0 || item->row() >= m_operations.size()) {
        return;
    }
    const bool enabled = item->checkState() == Qt::Checked;
    if (m_operations[item->row()].enabled == enabled) {
        return;
    }
    m_operations[item->row()].enabled = enabled;
    updateSummary();
    onSelectionChanged();
    emit operationsEdited(m_operations);
}

void OperationListPanel::onSelectionChanged()
{
    const int row = m_table->currentRow();
    const bool hasSelection = row >= 0;
    m_btnUp->setEnabled(hasSelection && row > 0);
    m_btnDown->setEnabled(hasSelection && row < m_operations.size() - 1);
    m_btnDelete->setEnabled(hasSelection);
    m_btnEditParameters->setEnabled(hasSelection);
    m_btnApplyTool->setEnabled(!m_operations.isEmpty());
    m_btnRecalculate->setEnabled(hasSelection && row < m_operations.size() &&
                                 m_operations.at(row).enabled);
    m_btnGenerateProgram->setEnabled(!executableOperations(m_operations).isEmpty());
    emit currentOperationChanged(hasSelection ? row + 1 : -1);
}

void OperationListPanel::retranslateUi()
{
    const bool zh = isChineseUi();
    m_titleLabel->setText(zh ? QStringLiteral("已确认工序")
                             : QStringLiteral("Confirmed Operations"));
    m_btnUp->setText(zh ? QStringLiteral("上移") : QStringLiteral("Up"));
    m_btnDown->setText(zh ? QStringLiteral("下移") : QStringLiteral("Down"));
    m_btnDelete->setText(zh ? QStringLiteral("删除") : QStringLiteral("Delete"));
    m_btnApplyTool->setText(zh ? QStringLiteral("用当前刀具") : QStringLiteral("Use Tool"));
    m_btnSortStage->setText(zh ? QStringLiteral("按流程排序") : QStringLiteral("Sort Flow"));
    m_btnEditParameters->setText(zh ? QStringLiteral("编辑参数")
                                     : QStringLiteral("Edit Parameters"));
    m_btnRecalculate->setText(zh ? QStringLiteral("重新生成刀路") : QStringLiteral("Recalculate Toolpath"));
    m_btnGenerateProgram->setText(zh ? QStringLiteral("从已确认工序生成程序")
                                     : QStringLiteral("Generate Program from Confirmed Operations"));
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("#"),
        zh ? QStringLiteral("类型") : QStringLiteral("Type"),
        zh ? QStringLiteral("阶段") : QStringLiteral("Stage"),
        zh ? QStringLiteral("特征") : QStringLiteral("Feature"),
        zh ? QStringLiteral("状态") : QStringLiteral("State"),
        zh ? QStringLiteral("刀具") : QStringLiteral("Tool")
    });
    m_table->setAccessibleName(zh ? QStringLiteral("已确认工序列表")
                                  : QStringLiteral("Confirmed operations list"));
    m_btnGenerateProgram->setAccessibleName(
        zh ? QStringLiteral("从已确认工序生成程序")
           : QStringLiteral("Generate program from confirmed operations"));
    refreshTable();
}
