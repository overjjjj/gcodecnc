#include "OperationListPanel.h"

#include "../core/Settings.h"
#include "../tool/ToolLibrary.h"
#include "../strategies/StrategyFactory.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
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
    if (strategyId == QStringLiteral("mill_closed_contour")) {
        return zh ? QStringLiteral("封闭轮廓铣") : QStringLiteral("Closed Contour");
    }
    if (strategyId == QStringLiteral("mill_open_contour")) {
        return zh ? QStringLiteral("开放轮廓铣") : QStringLiteral("Open Contour");
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

static OperationStage inferStage(const QString &strategyId, OperationType opType)
{
    if (strategyId == QStringLiteral("hole_spot")) {
        return OperationStage::Setup;
    }
    if (strategyId == QStringLiteral("mill_pocket_rough") ||
        strategyId == QStringLiteral("mill_slot") ||
        strategyId == QStringLiteral("mill_blind_slot")) {
        return OperationStage::RoughCut;
    }
    if (strategyId == QStringLiteral("hole_deephole") ||
        strategyId == QStringLiteral("hole_peck")) {
        return OperationStage::DeepHole;
    }
    if (strategyId == QStringLiteral("hole_chamfer")) {
        return OperationStage::Cleanup;
    }
    if (opType == OperationType::Finish ||
        strategyId == QStringLiteral("hole_reaming") ||
        strategyId == QStringLiteral("hole_circular_mill")) {
        return OperationStage::FinishCut;
    }
    return OperationStage::RoughCut;
}

} // namespace

OperationListPanel::OperationListPanel(QWidget *parent)
    : QWidget(parent)
    , m_table(new QTableWidget(this))
    , m_btnUp(new QToolButton(this))
    , m_btnDown(new QToolButton(this))
    , m_btnDelete(new QToolButton(this))
    , m_btnApplyTool(new QToolButton(this))
    , m_btnSortStage(new QToolButton(this))
    , m_btnGenerate(new QPushButton(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(m_btnUp);
    toolbar->addWidget(m_btnDown);
    toolbar->addWidget(m_btnDelete);
    toolbar->addWidget(m_btnApplyTool);
    toolbar->addWidget(m_btnSortStage);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    m_table->setColumnCount(5);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);

    layout->addWidget(m_btnGenerate);

    retranslateUi();

    connect(m_btnUp, &QToolButton::clicked, this, &OperationListPanel::onMoveUp);
    connect(m_btnDown, &QToolButton::clicked, this, &OperationListPanel::onMoveDown);
    connect(m_btnDelete, &QToolButton::clicked, this, &OperationListPanel::onDelete);
    connect(m_btnApplyTool, &QToolButton::clicked, this, &OperationListPanel::applyCurrentToolRequested);
    connect(m_btnSortStage, &QToolButton::clicked, this, &OperationListPanel::onSortByStage);
    connect(m_btnGenerate, &QPushButton::clicked, this, &OperationListPanel::onGenerateAll);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &OperationListPanel::onSelectionChanged);

    onSelectionChanged();
}

void OperationListPanel::setOperations(const QList<MachiningOperation> &operations)
{
    m_operations = operations;
    refreshTable();
}

int OperationListPanel::currentOperationNumber() const
{
    const int row = m_table->currentRow();
    return row >= 0 ? row + 1 : -1;
}

void OperationListPanel::addHoleOperation(const HoleFeature &feature,
                                          const QString &strategyId,
                                          const StrategyParams &params,
                                          int toolId)
{
    MachiningOperation op;
    op.opType = OperationType::Hole;
    op.holeFeature = feature;
    op.strategyId = strategyId;
    op.params = params;
    op.toolId = toolId;
    op.stage = inferStage(strategyId, op.opType);
    op.featureRef = QStringLiteral("D%1 Z-%2")
                        .arg(feature.radius * 2.0, 0, 'f', 1)
                        .arg(feature.depth, 0, 'f', 1);
    m_operations.append(op);
    refreshTable();
}

void OperationListPanel::addContourOperation(const ContourFeature &feature,
                                             const QString &strategyId,
                                             const StrategyParams &params,
                                             int toolId)
{
    MachiningOperation op;
    op.opType = OperationType::Contour;
    if (strategyId == QStringLiteral("mill_pocket_rough")) {
        op.opType = OperationType::Roughing;
    } else if (strategyId == QStringLiteral("mill_contour_finish") ||
               strategyId == QStringLiteral("mill_surface_finish") ||
               strategyId == QStringLiteral("mill_closed_contour") ||
               strategyId == QStringLiteral("mill_open_contour")) {
        op.opType = OperationType::Finish;
    }
    op.contourFeature = feature;
    op.strategyId = strategyId;
    op.params = params;
    op.toolId = toolId;
    op.stage = inferStage(strategyId, op.opType);

    auto strategy = StrategyFactory::instance().strategy(strategyId);
    const QString strategyName = strategy ? strategy->displayName() : strategyId;
    if (strategyId == QStringLiteral("mill_slot") ||
        strategyId == QStringLiteral("mill_blind_slot")) {
        const double slotLength = params.get(QStringLiteral("slotLength"), feature.radius * 2.0);
        const double slotWidth = params.get(QStringLiteral("slotWidth"), feature.radius);
        op.featureRef = QStringLiteral("%1 L%2 W%3 Z-%4")
                            .arg(strategyName)
                            .arg(slotLength, 0, 'f', 1)
                            .arg(slotWidth, 0, 'f', 1)
                            .arg(feature.depth, 0, 'f', 1);
    } else {
        op.featureRef = QStringLiteral("%1 R%2 Z-%3")
                            .arg(strategyName)
                            .arg(feature.radius, 0, 'f', 1)
                            .arg(feature.depth, 0, 'f', 1);
    }
    m_operations.append(op);
    refreshTable();
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
        }
    }
    refreshTable();
    return true;
}

void OperationListPanel::refreshTable()
{
    m_table->setRowCount(0);
    const bool zh = isChineseUi();
    int row = 0;
    for (const MachiningOperation &op : m_operations) {
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        m_table->setItem(row, 1, new QTableWidgetItem(operationTypeText(op, zh)));
        m_table->setItem(row, 2, new QTableWidgetItem(operationStageText(op.stage, zh)));
        m_table->setItem(row, 3, new QTableWidgetItem(operationLabel(op)));

        const ToolEntry tool = ToolLibrary::instance().tool(op.toolId);
        const QString toolText = tool.id > 0
            ? QStringLiteral("T%1 D%2").arg(tool.id).arg(tool.diameter, 0, 'f', 1)
            : QStringLiteral("-");
        m_table->setItem(row, 4, new QTableWidgetItem(toolText));
        ++row;
    }
    onSelectionChanged();
}

QString OperationListPanel::operationLabel(const MachiningOperation &op) const
{
    if (op.opType == OperationType::Hole) {
        return QStringLiteral("D%1 Z-%2")
            .arg(op.holeFeature.radius * 2.0, 0, 'f', 1)
            .arg(op.holeFeature.depth, 0, 'f', 1);
    }

    if (op.strategyId == QStringLiteral("mill_slot") ||
        op.strategyId == QStringLiteral("mill_blind_slot")) {
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
}

void OperationListPanel::onDelete()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_operations.size()) {
        return;
    }
    m_operations.removeAt(row);
    refreshTable();
}

void OperationListPanel::onSortByStage()
{
    std::stable_sort(m_operations.begin(), m_operations.end(),
                     [](const MachiningOperation &a, const MachiningOperation &b) {
        return stageOrder(a.stage) < stageOrder(b.stage);
    });
    refreshTable();
}

void OperationListPanel::onGenerateAll()
{
    if (!m_operations.isEmpty()) {
        emit generateAllRequested(m_operations);
    }
}

void OperationListPanel::onSelectionChanged()
{
    const int row = m_table->currentRow();
    const bool hasSelection = row >= 0;
    m_btnUp->setEnabled(hasSelection && row > 0);
    m_btnDown->setEnabled(hasSelection && row < m_operations.size() - 1);
    m_btnDelete->setEnabled(hasSelection);
    m_btnApplyTool->setEnabled(!m_operations.isEmpty());
    emit currentOperationChanged(hasSelection ? row + 1 : -1);
}

void OperationListPanel::retranslateUi()
{
    const bool zh = isChineseUi();
    m_btnUp->setText(zh ? QStringLiteral("上移") : QStringLiteral("Up"));
    m_btnDown->setText(zh ? QStringLiteral("下移") : QStringLiteral("Down"));
    m_btnDelete->setText(zh ? QStringLiteral("删除") : QStringLiteral("Delete"));
    m_btnApplyTool->setText(zh ? QStringLiteral("用当前刀具") : QStringLiteral("Use Tool"));
    m_btnSortStage->setText(zh ? QStringLiteral("按流程排序") : QStringLiteral("Sort Flow"));
    m_btnGenerate->setText(zh ? QStringLiteral("生成全部 G 代码") : QStringLiteral("Generate All G-code"));
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("#"),
        zh ? QStringLiteral("类型") : QStringLiteral("Type"),
        zh ? QStringLiteral("阶段") : QStringLiteral("Stage"),
        zh ? QStringLiteral("特征") : QStringLiteral("Feature"),
        zh ? QStringLiteral("刀具") : QStringLiteral("Tool")
    });
    refreshTable();
}
