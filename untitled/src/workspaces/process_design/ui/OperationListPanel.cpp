#include "OperationListPanel.h"

#include "../operations/OperationWorkflow.h"
#include "../../../core/Settings.h"
#include "../../../tool/ToolLibrary.h"
#include "../../../strategies/StrategyFactory.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QUuid>
#include <QTreeWidgetItemIterator>
#include <QSet>
#include <QMenu>
#include <QGridLayout>
#include <algorithm>

namespace {

// 函数说明：isChineseUi负责当前模块的核心处理。
static bool isChineseUi()
{
    return Settings::instance().language() == QStringLiteral("zh_CN");
}

// 函数说明：strategyTypeText负责当前模块的核心处理。
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
    return QString();
}

// 函数说明：operationTypeText负责当前模块的核心处理。
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

// 函数说明：stageOrder负责当前模块的核心处理。
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

// 函数说明：operationStageText负责当前模块的核心处理。
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

// 函数说明：toolpathStateText负责当前模块的核心处理。
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

// 函数说明：operationRegion负责当前模块的核心处理。
static FaceRegion operationRegion(const MachiningOperation &op)
{
    return op.opType == OperationType::Hole
        ? op.holeFeature.region
        : op.contourFeature.region;
}

// 函数说明：activeRegionOrder负责当前模块的核心处理。
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
    , m_table(new QTreeWidget(this))
    // 函数说明：m_btnUp负责当前模块的核心处理。
    , m_btnUp(new QToolButton(this))
    // 函数说明：m_btnDown负责当前模块的核心处理。
    , m_btnDown(new QToolButton(this))
    // 函数说明：m_btnDelete负责当前模块的核心处理。
    , m_btnDelete(new QToolButton(this))
    // 函数说明：m_btnApplyTool负责当前模块的核心处理。
    , m_btnApplyTool(new QToolButton(this))
    // 函数说明：m_btnSortStage负责当前模块的核心处理。
    , m_btnSortStage(new QToolButton(this))
    // 函数说明：m_btnRecalculate负责当前模块的核心处理。
    , m_btnRecalculate(new QToolButton(this))
    , m_btnCopy(new QToolButton(this))
    , m_btnBatchEdit(new QToolButton(this))
    // 函数说明：m_btnGenerateProgram负责当前模块的核心处理。
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

    auto *toolbar = new QGridLayout;
    toolbar->addWidget(m_btnUp, 0, 0);
    toolbar->addWidget(m_btnDown, 0, 1);
    toolbar->addWidget(m_btnDelete, 0, 2);
    toolbar->addWidget(m_btnApplyTool, 1, 0);
    toolbar->addWidget(m_btnSortStage, 1, 1);
    toolbar->addWidget(m_btnRecalculate, 1, 2);
    m_btnCopy->setObjectName(QStringLiteral("copyOperationsButton"));
    m_btnBatchEdit->setObjectName(QStringLiteral("batchEditOperationsButton"));
    toolbar->addWidget(m_btnCopy, 2, 0);
    toolbar->addWidget(m_btnBatchEdit, 2, 1, 1, 2);
    layout->addLayout(toolbar);

    m_table->setObjectName(QStringLiteral("operationTree"));
    m_table->setColumnCount(3);
    m_table->header()->setStretchLastSection(false);
    m_table->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setUniformRowHeights(true);
    m_table->setIndentation(18);
    m_table->setStyleSheet(QStringLiteral("QTreeWidget { border: 1px solid #d8e0eb; background: white; } QTreeWidget::item { min-height: 28px; } QTreeWidget::item:selected { background: #dcecff; color: #172033; }"));
    connect(m_table, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int column) {
        const int row = operationRow(item);
        if (column == 0 && row >= 0) emit editOperationRequested(m_operations.at(row).id);
    });
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = m_table->itemAt(pos);
        const int row = operationRow(item);
        if (row < 0) return;
        m_table->setCurrentItem(item);
        QMenu menu(this);
        auto *edit = menu.addAction(isChineseUi() ? QStringLiteral("编辑参数") : QStringLiteral("Edit parameters"));
        if (menu.exec(m_table->viewport()->mapToGlobal(pos)) == edit)
            emit editOperationRequested(m_operations.at(row).id);
    });
    layout->addWidget(m_table, 1);

    m_btnGenerateProgram->setObjectName(QStringLiteral("operationPrimaryAction"));
    layout->addWidget(m_btnGenerateProgram);

    retranslateUi();

    connect(m_btnUp, &QToolButton::clicked, this, &OperationListPanel::onMoveUp);
    connect(m_btnDown, &QToolButton::clicked, this, &OperationListPanel::onMoveDown);
    connect(m_btnDelete, &QToolButton::clicked, this, &OperationListPanel::onDelete);
    connect(m_btnCopy, &QToolButton::clicked, this, &OperationListPanel::onCopy);
    connect(m_btnBatchEdit, &QToolButton::clicked, this, [this]() {
        QStringList ids;
        for (int row : selectedOperationRows()) ids.append(m_operations.at(row).id);
        if (!ids.isEmpty()) emit batchEditOperationsRequested(ids);
    });
    connect(m_btnApplyTool, &QToolButton::clicked, this, &OperationListPanel::applyCurrentToolRequested);
    connect(m_btnSortStage, &QToolButton::clicked, this, &OperationListPanel::onSortByStage);
    connect(m_btnRecalculate, &QToolButton::clicked, this, &OperationListPanel::onRecalculate);
    connect(m_btnGenerateProgram, &QPushButton::clicked,
            this, &OperationListPanel::onGenerateProgram);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &OperationListPanel::onSelectionChanged);
    connect(m_table, &QTreeWidget::itemChanged,
            this, &OperationListPanel::onItemChanged);

    onSelectionChanged();
}

// 函数说明：setOperations负责当前模块的核心处理。
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

// 函数说明：currentOperationNumber负责当前模块的核心处理。
int OperationListPanel::currentOperationNumber() const
{
    const int row = operationRow(m_table->currentItem());
    return row >= 0 ? row + 1 : -1;
}

int OperationListPanel::operationRow(QTreeWidgetItem *item) const
{
    if (!item) return -1;
    const QString id = item->data(0, Qt::UserRole).toString();
    if (id.isEmpty()) return -1;
    for (int row = 0; row < m_operations.size(); ++row)
        if (m_operations.at(row).id == id) return row;
    return -1;
}

QList<int> OperationListPanel::selectedOperationRows() const
{
    QList<int> rows;
    for (auto *item : m_table->selectedItems()) {
        const int row = operationRow(item);
        if (row >= 0 && !rows.contains(row)) rows.append(row);
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

QString OperationListPanel::currentOperationId() const
{
    auto *item = m_table->currentItem();
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

bool OperationListPanel::selectOperationById(const QString &operationId)
{
    if (operationId.isEmpty()) return false;
    for (QTreeWidgetItemIterator it(m_table); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toString() == operationId) {
            m_table->setCurrentItem(*it);
            m_table->scrollToItem(*it);
            return true;
        }
    }
    return false;
}

// 中文说明：只接收已由用户确认的操作提案并建立稳定 ID；此处不生成 G 代码，
// 程序生成统一由 ProgramGenerationService 负责。
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

// 函数说明：applyToolToSelection负责当前模块的核心处理。
bool OperationListPanel::applyToolToSelection(int toolId)
{
    if (toolId <= 0 || m_operations.isEmpty()) {
        return false;
    }

    QList<int> rows = selectedOperationRows();
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

void OperationListPanel::onCopy()
{
    QList<MachiningOperation> copies;
    for (int row : selectedOperationRows()) {
        auto copy = m_operations.at(row);
        copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        copy.selectionGroupId.clear();
        copy.markToolpathStale(QStringLiteral("copied operation requires recalculation"));
        copies.append(copy);
    }
    const auto ids = addConfirmedOperations(copies);
    if (!ids.isEmpty()) selectOperationById(ids.first());
}

// 函数说明：setToolpathResult负责当前模块的核心处理。
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

// 函数说明：setActiveRegion负责当前模块的核心处理。
void OperationListPanel::setActiveRegion(FaceRegion region)
{
    m_activeRegion = region;
}

// 函数说明：refreshTable负责当前模块的核心处理。
void OperationListPanel::refreshTable()
{
    const QString currentId = currentOperationId();
    QSet<QString> selectedIds, expandedIds;
    for (auto *item : m_table->selectedItems()) selectedIds.insert(item->data(0, Qt::UserRole).toString());
    for (QTreeWidgetItemIterator it(m_table); *it; ++it)
        if ((*it)->isExpanded()) expandedIds.insert((*it)->data(0, Qt::UserRole).toString());
    {
        const QSignalBlocker blocker(m_table);
        const QSignalBlocker selectionBlocker(m_table->selectionModel());
        m_table->clear();
        const bool zh = isChineseUi();
        QHash<QString, QTreeWidgetItem *> setups;
        for (int row = 0; row < m_operations.size(); ++row) {
            const auto &op = m_operations.at(row);
            const auto &evidence = op.selectionEvidence;
            const QString setupKey = evidence.setupFingerprint + QLatin1Char('|') + evidence.coordinateSystemId
                + QString::number(static_cast<int>(operationRegion(op)));
            auto *setup = setups.value(setupKey, nullptr);
            if (!setup) {
                setup = new QTreeWidgetItem(m_table);
                QString region;
                switch (operationRegion(op)) {
                case FaceRegion::Front: region = zh ? QStringLiteral("正面") : QStringLiteral("Front"); break;
                case FaceRegion::Back: region = zh ? QStringLiteral("背面") : QStringLiteral("Back"); break;
                case FaceRegion::Side: region = zh ? QStringLiteral("侧面") : QStringLiteral("Side"); break;
                default: region = zh ? QStringLiteral("加工设置") : QStringLiteral("Setup"); break;
                }
                setup->setText(0, region + QStringLiteral(" · ") + evidence.coordinateSystemId);
                setup->setToolTip(0, evidence.setupFingerprint);
                setup->setExpanded(true);
                setups.insert(setupKey, setup);
            }
            auto *item = new QTreeWidgetItem(setup);
            item->setData(0, Qt::UserRole, op.id);
            item->setText(0, QStringLiteral("%1  %2").arg(row + 1, 2, 10, QLatin1Char('0')).arg(operationTypeText(op, zh)));
            item->setToolTip(0, op.strategyId + QStringLiteral(" · ") + operationStageText(op.stage, zh));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(1, op.enabled ? Qt::Checked : Qt::Unchecked);
            item->setCheckState(2, m_visibility.value(op.id, true) ? Qt::Checked : Qt::Unchecked);
            const ToolEntry tool = ToolLibrary::instance().tool(op.toolId);
            const QString toolText = tool.id > 0
                ? QStringLiteral("T%1 %2 Ø%3").arg(tool.id).arg(tool.name).arg(tool.diameter, 0, 'f', 2)
                : (zh ? QStringLiteral("未指定刀具") : QStringLiteral("No tool assigned"));
            QStringList params;
            const QList<QPair<QString, QString>> summaries = {
                {QStringLiteral("spindleSpeed"), QStringLiteral("S")},
                {QStringLiteral("feedRate"), QStringLiteral("F")},
                {QStringLiteral("stepDown"), zh ? QStringLiteral("层深") : QStringLiteral("Step down")},
                {QStringLiteral("depth"), zh ? QStringLiteral("深度") : QStringLiteral("Depth")}
            };
            for (const auto &field : summaries)
                if (op.params.values.contains(field.first)) params.append(field.second + QStringLiteral(" ") + op.params.values.value(field.first).toString());
            if (params.isEmpty()) params.append(zh ? QStringLiteral("%1 项已确认参数").arg(op.params.values.size()) : QStringLiteral("%1 confirmed parameters").arg(op.params.values.size()));
            const QStringList details = {
                (zh ? QStringLiteral("几何：") : QStringLiteral("Geometry: ")) + operationLabel(op),
                (zh ? QStringLiteral("刀具：") : QStringLiteral("Tool: ")) + toolText,
                (zh ? QStringLiteral("参数：") : QStringLiteral("Parameters: ")) + params.join(QStringLiteral(" · ")),
                (zh ? QStringLiteral("刀路：") : QStringLiteral("Toolpath: ")) + toolpathStateText(op.toolpathState, zh)
            };
            for (const QString &detail : details) {
                auto *child = new QTreeWidgetItem(item, QStringList{detail});
                child->setData(0, Qt::UserRole, op.id);
                child->setToolTip(0, detail);
            }
            item->child(0)->setToolTip(0, op.geometryRefs.join(QLatin1Char('\n')));
            item->child(3)->setToolTip(0, op.warnings.join(QLatin1Char('\n')));
            item->setExpanded(expandedIds.contains(op.id) || currentId == op.id);
            if (currentId == op.id) m_table->setCurrentItem(item);
            item->setSelected(selectedIds.contains(op.id));
        }
    }
    updateSummary();
    onSelectionChanged();
}

// 函数说明：updateSummary负责当前模块的核心处理。
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

// 函数说明：operationLabel负责当前模块的核心处理。
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

// 函数说明：onMoveUp负责当前模块的核心处理。
void OperationListPanel::onMoveUp()
{
    const int row = operationRow(m_table->currentItem());
    if (row <= 0) {
        return;
    }
    m_operations.swapItemsAt(row, row - 1);
    refreshTable();
    selectOperationById(m_operations.at(row - 1).id);
    emit operationsEdited(m_operations);
}

// 函数说明：onMoveDown负责当前模块的核心处理。
void OperationListPanel::onMoveDown()
{
    const int row = operationRow(m_table->currentItem());
    if (row < 0 || row >= m_operations.size() - 1) {
        return;
    }
    m_operations.swapItemsAt(row, row + 1);
    refreshTable();
    selectOperationById(m_operations.at(row + 1).id);
    emit operationsEdited(m_operations);
}

// 函数说明：onDelete负责当前模块的核心处理。
void OperationListPanel::onDelete()
{
    const int row = operationRow(m_table->currentItem());
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

// 函数说明：onSortByStage负责当前模块的核心处理。
void OperationListPanel::onSortByStage()
{
    // 函数说明：stable_sort负责当前模块的核心处理。
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

// 函数说明：onRecalculate负责当前模块的核心处理。
void OperationListPanel::onRecalculate()
{
    QList<MachiningOperation> selected;
    for (int row : selectedOperationRows()) {
        if (m_operations.at(row).enabled) selected.append(m_operations.at(row));
    }
    if (selected.isEmpty()) {
        const int row = operationRow(m_table->currentItem());
        if (row >= 0 && row < m_operations.size() && m_operations.at(row).enabled) {
            selected.append(m_operations.at(row));
        }
    }
    if (!selected.isEmpty()) {
        emit recalculateRequested(selected);
    }
}

// 中文说明：触发当前有效工序的程序生成请求；禁用、过期或错误工序不得绕过状态门。
void OperationListPanel::onGenerateProgram()
{
    const QList<MachiningOperation> executable = enabledOperations(m_operations);
    if (!executable.isEmpty()) {
        emit generateProgramRequested(executable);
    }
}

// 函数说明：onItemChanged负责当前模块的核心处理。
void OperationListPanel::onItemChanged(QTreeWidgetItem *item, int column)
{
    const int row = operationRow(item);
    if (row < 0 || (column != 1 && column != 2)) return;
    const bool checked = item->checkState(column) == Qt::Checked;
    if (column == 2) {
        const QString id = m_operations.at(row).id;
        if (m_visibility.value(id, true) == checked) return;
        m_visibility.insert(id, checked);
        emit operationVisibilityChanged(id, checked);
        return;
    }
    if (m_operations.at(row).enabled == checked) return;
    m_operations[row].enabled = checked;
    updateSummary();
    onSelectionChanged();
    emit operationsEdited(m_operations);
}

// 函数说明：onSelectionChanged负责当前模块的核心处理。
void OperationListPanel::onSelectionChanged()
{
    const int row = operationRow(m_table->currentItem());
    const bool hasSelection = row >= 0;
    m_btnUp->setEnabled(hasSelection && row > 0);
    m_btnDown->setEnabled(hasSelection && row < m_operations.size() - 1);
    m_btnDelete->setEnabled(hasSelection);
    m_btnCopy->setEnabled(!selectedOperationRows().isEmpty());
    m_btnBatchEdit->setEnabled(!selectedOperationRows().isEmpty());
    m_btnApplyTool->setEnabled(!m_operations.isEmpty());
    bool canRecalculate = hasSelection && row < m_operations.size() && m_operations.at(row).enabled;
    for (int selectedRow : selectedOperationRows())
        canRecalculate |= m_operations.at(selectedRow).enabled;
    m_btnRecalculate->setEnabled(canRecalculate);
    m_btnGenerateProgram->setEnabled(!enabledOperations(m_operations).isEmpty());
    emit currentOperationChanged(hasSelection ? row + 1 : -1);
}

// 函数说明：retranslateUi负责当前模块的核心处理。
void OperationListPanel::retranslateUi()
{
    const bool zh = isChineseUi();
    m_titleLabel->setText(zh ? QStringLiteral("工艺树")
                             : QStringLiteral("Operations"));
    m_btnUp->setText(zh ? QStringLiteral("上移") : QStringLiteral("Up"));
    m_btnDown->setText(zh ? QStringLiteral("下移") : QStringLiteral("Down"));
    m_btnDelete->setText(zh ? QStringLiteral("删除") : QStringLiteral("Delete"));
    m_btnCopy->setText(zh ? QStringLiteral("复制") : QStringLiteral("Copy"));
    m_btnBatchEdit->setText(zh ? QStringLiteral("批量参数") : QStringLiteral("Batch parameters"));
    m_btnApplyTool->setText(zh ? QStringLiteral("用当前刀具") : QStringLiteral("Use Tool"));
    m_btnSortStage->setText(zh ? QStringLiteral("流程排序") : QStringLiteral("Sort Flow"));
    m_btnRecalculate->setText(zh ? QStringLiteral("重算刀路") : QStringLiteral("Recalculate Toolpath"));
    m_btnGenerateProgram->setText(zh ? QStringLiteral("从已确认工序生成程序")
                                     : QStringLiteral("Generate Program from Confirmed Operations"));
    m_table->setHeaderLabels({zh ? QStringLiteral("加工设置 / 工序") : QStringLiteral("Setup / operation"),
                             zh ? QStringLiteral("启用") : QStringLiteral("Enabled"),
                             zh ? QStringLiteral("显示") : QStringLiteral("Visible")});
    m_table->setAccessibleName(zh ? QStringLiteral("已确认工序列表")
                                  : QStringLiteral("Confirmed operations list"));
    m_btnGenerateProgram->setAccessibleName(
        zh ? QStringLiteral("从已确认工序生成程序")
           : QStringLiteral("Generate program from confirmed operations"));
    refreshTable();
}
