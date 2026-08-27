#include "ContourMachiningChoiceDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

ContourMachiningChoiceDialog::ContourMachiningChoiceDialog(QWidget *parent)
    : QDialog(parent)
    , m_introLabel(new QLabel(this))
    , m_stateValue(new QLabel(this))
    , m_directionLabel(new QLabel(this))
    , m_startLabel(new QLabel(this))
    , m_compensationLabel(new QLabel(this))
    , m_geometrySourceLabel(new QLabel(this))
    , m_selectionModeLabel(new QLabel(this))
    , m_machiningSideLabel(new QLabel(this))
    , m_sortStrategyLabel(new QLabel(this))
    , m_branchLabel(new QLabel(this))
    , m_directionCombo(new QComboBox(this))
    , m_startCombo(new QComboBox(this))
    , m_compensationCombo(new QComboBox(this))
    , m_geometrySourceCombo(new QComboBox(this))
    , m_selectionModeCombo(new QComboBox(this))
    , m_machiningSideCombo(new QComboBox(this))
    , m_sortStrategyCombo(new QComboBox(this))
    , m_branchCombo(new QComboBox(this))
    , m_summaryLabel(new QLabel(this))
{
    setModal(true);
    setMinimumWidth(460);

    m_introLabel->setObjectName(QStringLiteral("contourChoiceIntro"));
    m_introLabel->setWordWrap(true);

    auto *form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow(QStringLiteral("轮廓状态"), m_stateValue);
    form->addRow(m_directionLabel, m_directionCombo);
    form->addRow(m_startLabel, m_startCombo);
    form->addRow(m_compensationLabel, m_compensationCombo);
    form->addRow(m_geometrySourceLabel, m_geometrySourceCombo);
    form->addRow(m_selectionModeLabel, m_selectionModeCombo);
    form->addRow(m_machiningSideLabel, m_machiningSideCombo);
    form->addRow(m_sortStrategyLabel, m_sortStrategyCombo);
    form->addRow(m_branchLabel, m_branchCombo);

    m_geometrySourceCombo->setObjectName(QStringLiteral("chainGeometrySourceCombo"));
    m_selectionModeCombo->setObjectName(QStringLiteral("chainSelectionModeCombo"));
    m_machiningSideCombo->setObjectName(QStringLiteral("chainMachiningSideCombo"));
    m_sortStrategyCombo->setObjectName(QStringLiteral("chainSortStrategyCombo"));
    m_branchCombo->setObjectName(QStringLiteral("chainBranchCombo"));

    m_summaryLabel->setObjectName(QStringLiteral("contourChoiceSummary"));
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setProperty("notice", true);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() {
                updateStartOptions();
                updateSummary();
            });
    connect(m_startCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() {
                if (!m_closed && m_startCombo->currentIndex() >= 0) {
                    const QSignalBlocker blocker(m_directionCombo);
                    const ContourTraversalDirection direction =
                        m_startCombo->currentData().toInt() == 0
                            ? ContourTraversalDirection::Forward
                            : ContourTraversalDirection::Reverse;
                    const int index = m_directionCombo->findData(int(direction));
                    if (index >= 0) m_directionCombo->setCurrentIndex(index);
                }
                updateSummary();
            });
    connect(m_compensationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { updateSummary(); });
    for (QComboBox *combo : {m_geometrySourceCombo, m_selectionModeCombo,
                             m_machiningSideCombo, m_sortStrategyCombo,
                             m_branchCombo}) {
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this]() { updateSummary(); });
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);
    layout->addWidget(m_introLabel);
    layout->addLayout(form);
    layout->addWidget(m_summaryLabel);
    layout->addWidget(buttons);
    updateText();
}

void ContourMachiningChoiceDialog::setChineseUi(bool chinese)
{
    m_chinese = chinese;
    updateText();
}

void ContourMachiningChoiceDialog::setContext(const QString &strategyId,
                                               const ContourFeature &feature,
                                               const StrategyParams &params)
{
    m_closed = strategyId != QStringLiteral("mill_open_contour");
    m_canReverse = canReverseContourDirection(feature);
    m_featurePoints = feature.points;
    updateText();
    updateStartOptions();

    m_geometrySourceCombo->setCurrentIndex(
        m_geometrySourceCombo->findData(int(ChainGeometrySource::Wire)));
    m_selectionModeCombo->setCurrentIndex(
        m_selectionModeCombo->findData(int(ChainSelectionMode::Chain)));
    m_sortStrategyCombo->setCurrentIndex(
        m_sortStrategyCombo->findData(int(ChainSortStrategy::SelectionOrder)));
    if (strategyId == QStringLiteral("mill_outer_chamfer")) {
        m_machiningSideCombo->setCurrentIndex(
            m_machiningSideCombo->findData(int(ChainMachiningSide::Outside)));
        m_machiningSideCombo->setEnabled(false);
        m_machiningSideCombo->setToolTip(
            m_chinese ? QStringLiteral("外形倒角安全子集固定使用外侧闭合边界。")
                      : QStringLiteral("The verified outer-chamfer subset is fixed to the outside closed boundary."));
    } else {
        m_machiningSideCombo->setEnabled(true);
    }
    m_geometrySourceCombo->setEnabled(false);
    m_selectionModeCombo->setEnabled(false);
    m_sortStrategyCombo->setEnabled(false);
    const QString fixedChainReason = m_chinese
        ? QStringLiteral("当前入口使用已识别的闭合线框，并保持连通边顺序；其他排序需可靠几何邻接算法后开放。")
        : QStringLiteral("This entry uses the recognized closed wire in connected-edge order; other sorting requires a verified adjacency algorithm.");
    m_geometrySourceCombo->setToolTip(fixedChainReason);
    m_selectionModeCombo->setToolTip(fixedChainReason);
    m_sortStrategyCombo->setToolTip(fixedChainReason);

    m_branchCombo->clear();
    m_branchCombo->addItem(m_chinese ? QStringLiteral("外边界")
                                     : QStringLiteral("Outer boundary"),
                           QStringLiteral("outer"));
    for (int index = 0; index < feature.islands.size(); ++index) {
        m_branchCombo->addItem(
            m_chinese ? QStringLiteral("孤岛边界 %1").arg(index + 1)
                      : QStringLiteral("Island boundary %1").arg(index + 1),
            QStringLiteral("island:%1").arg(index));
    }
    const bool islandOperation = strategyId == QStringLiteral("mill_annular") ||
                                 strategyId == QStringLiteral("mill_island");
    const int islandIndex = m_branchCombo->findData(QStringLiteral("island:0"));
    if (islandOperation && islandIndex >= 0) {
        m_branchCombo->setCurrentIndex(islandIndex);
    }
    m_branchCombo->setEnabled(false);
    m_branchCombo->setToolTip(
        islandOperation
            ? (m_chinese ? QStringLiteral("该安全子集固定绑定唯一孤岛边界。")
                         : QStringLiteral("This safe subset is bound to its single island boundary."))
            : fixedChainReason);

    const double compensation = params.get(QStringLiteral("compensation"), 1.0);
    const int compensationValue = compensation > 0.5
        ? int(ContourCompensationChoice::LeftG41)
        : (compensation < -0.5 ? int(ContourCompensationChoice::RightG42)
                               : int(ContourCompensationChoice::CamOffsetG40));
    const int index = m_compensationCombo->findData(compensationValue);
    if (index >= 0) {
        m_compensationCombo->setCurrentIndex(index);
    }
    if (strategyId == QStringLiteral("mill_outer_chamfer")) {
        const int centerlineIndex = m_compensationCombo->findData(
            int(ContourCompensationChoice::CamOffsetG40));
        if (centerlineIndex >= 0) {
            m_compensationCombo->setCurrentIndex(centerlineIndex);
        }
        m_compensationCombo->setEnabled(false);
        m_compensationCombo->setToolTip(
            m_chinese ? QStringLiteral("二维外形倒角按已验证边界中心线生成，不使用机床半径刀补。")
                      : QStringLiteral("Verified 2D outer chamfer follows the boundary centerline without machine radius compensation."));
    } else {
        m_compensationCombo->setEnabled(true);
    }
    updateSummary();
}

ContourMachiningChoice ContourMachiningChoiceDialog::choice() const
{
    ContourMachiningChoice result;
    result.direction = ContourTraversalDirection(m_directionCombo->currentData().toInt());
    result.compensation = ContourCompensationChoice(m_compensationCombo->currentData().toInt());
    result.geometrySource = ChainGeometrySource(m_geometrySourceCombo->currentData().toInt());
    result.selectionMode = ChainSelectionMode(m_selectionModeCombo->currentData().toInt());
    result.machiningSide = ChainMachiningSide(m_machiningSideCombo->currentData().toInt());
    result.sortStrategy = ChainSortStrategy(m_sortStrategyCombo->currentData().toInt());
    result.selectedBranchGeometryId = m_branchCombo->currentData().toString();
    result.closedContour = m_closed;
    result.startPointIndex = m_startCombo->currentData().toInt();
    return result;
}

void ContourMachiningChoiceDialog::setChoiceChangedCallback(
    std::function<void(const ContourMachiningChoice &)> callback)
{
    m_choiceChangedCallback = std::move(callback);
    updateSummary();
}

void ContourMachiningChoiceDialog::updateText()
{
    setWindowTitle(m_chinese ? QStringLiteral("确认轮廓加工侧与方向")
                             : QStringLiteral("Confirm Contour Side and Direction"));
    m_introLabel->setText(
        m_chinese ? QStringLiteral("轮廓状态由模型识别；加工方向和刀补必须由操作员确认。")
                  : QStringLiteral("Contour state comes from model recognition; the operator must confirm direction and compensation."));
    m_stateValue->setText(m_closed
        ? (m_chinese ? QStringLiteral("封闭轮廓") : QStringLiteral("Closed contour"))
        : (m_chinese ? QStringLiteral("开放轮廓") : QStringLiteral("Open contour")));
    m_directionLabel->setText(m_chinese ? QStringLiteral("加工方向")
                                        : QStringLiteral("Traversal direction"));
    m_startLabel->setText(m_chinese ? QStringLiteral("加工起点")
                                    : QStringLiteral("Start point"));
    m_compensationLabel->setText(m_chinese ? QStringLiteral("刀具侧 / 刀补")
                                           : QStringLiteral("Cutter side / compensation"));
    m_geometrySourceLabel->setText(m_chinese ? QStringLiteral("几何来源")
                                             : QStringLiteral("Geometry source"));
    m_selectionModeLabel->setText(m_chinese ? QStringLiteral("串联模式")
                                            : QStringLiteral("Chain mode"));
    m_machiningSideLabel->setText(m_chinese ? QStringLiteral("加工侧")
                                            : QStringLiteral("Machining side"));
    m_sortStrategyLabel->setText(m_chinese ? QStringLiteral("排序方式")
                                           : QStringLiteral("Sort strategy"));
    m_branchLabel->setText(m_chinese ? QStringLiteral("边界分支")
                                     : QStringLiteral("Boundary branch"));

    const int oldSource = m_geometrySourceCombo->currentData().toInt();
    m_geometrySourceCombo->clear();
    m_geometrySourceCombo->addItem(m_chinese ? QStringLiteral("实体") : QStringLiteral("Entity"),
                                   int(ChainGeometrySource::Entity));
    m_geometrySourceCombo->addItem(m_chinese ? QStringLiteral("线框") : QStringLiteral("Wire"),
                                   int(ChainGeometrySource::Wire));
    m_geometrySourceCombo->addItem(m_chinese ? QStringLiteral("毛坯") : QStringLiteral("Stock"),
                                   int(ChainGeometrySource::Stock));
    int comboIndex = m_geometrySourceCombo->findData(oldSource);
    m_geometrySourceCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 1);

    const int oldMode = m_selectionModeCombo->currentData().toInt();
    m_selectionModeCombo->clear();
    m_selectionModeCombo->addItem(m_chinese ? QStringLiteral("边") : QStringLiteral("Edge"),
                                  int(ChainSelectionMode::Edge));
    m_selectionModeCombo->addItem(m_chinese ? QStringLiteral("整串") : QStringLiteral("Chain"),
                                  int(ChainSelectionMode::Chain));
    m_selectionModeCombo->addItem(m_chinese ? QStringLiteral("面") : QStringLiteral("Face"),
                                  int(ChainSelectionMode::Face));
    m_selectionModeCombo->addItem(m_chinese ? QStringLiteral("部分串联") : QStringLiteral("Partial chain"),
                                  int(ChainSelectionMode::PartialChain));
    comboIndex = m_selectionModeCombo->findData(oldMode);
    m_selectionModeCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 1);

    const int oldSide = m_machiningSideCombo->currentData().toInt();
    m_machiningSideCombo->clear();
    m_machiningSideCombo->addItem(m_chinese ? QStringLiteral("内侧") : QStringLiteral("Inside"),
                                  int(ChainMachiningSide::Inside));
    m_machiningSideCombo->addItem(m_chinese ? QStringLiteral("外侧") : QStringLiteral("Outside"),
                                  int(ChainMachiningSide::Outside));
    m_machiningSideCombo->addItem(m_chinese ? QStringLiteral("混合") : QStringLiteral("Mixed"),
                                  int(ChainMachiningSide::Mixed));
    comboIndex = m_machiningSideCombo->findData(oldSide);
    m_machiningSideCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 2);

    const int oldSort = m_sortStrategyCombo->currentData().toInt();
    m_sortStrategyCombo->clear();
    m_sortStrategyCombo->addItem(m_chinese ? QStringLiteral("选择顺序") : QStringLiteral("Selection order"),
                                 int(ChainSortStrategy::SelectionOrder));
    m_sortStrategyCombo->addItem(m_chinese ? QStringLiteral("X 双向") : QStringLiteral("Bidirectional X"),
                                 int(ChainSortStrategy::BidirectionalX));
    m_sortStrategyCombo->addItem(m_chinese ? QStringLiteral("Y 双向") : QStringLiteral("Bidirectional Y"),
                                 int(ChainSortStrategy::BidirectionalY));
    m_sortStrategyCombo->addItem(m_chinese ? QStringLiteral("X 单向") : QStringLiteral("Unidirectional X"),
                                 int(ChainSortStrategy::UnidirectionalX));
    m_sortStrategyCombo->addItem(m_chinese ? QStringLiteral("Y 单向") : QStringLiteral("Unidirectional Y"),
                                 int(ChainSortStrategy::UnidirectionalY));
    m_sortStrategyCombo->addItem(m_chinese ? QStringLiteral("对角") : QStringLiteral("Diagonal"),
                                 int(ChainSortStrategy::Diagonal));
    m_sortStrategyCombo->addItem(m_chinese ? QStringLiteral("点到点") : QStringLiteral("Point to point"),
                                 int(ChainSortStrategy::PointToPoint));
    comboIndex = m_sortStrategyCombo->findData(oldSort);
    m_sortStrategyCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);

    const int oldDirection = m_directionCombo->currentData().toInt();
    m_directionCombo->clear();
    m_directionCombo->addItem(m_chinese ? QStringLiteral("正向（按识别点序）")
                                        : QStringLiteral("Forward (recognized point order)"),
                              int(ContourTraversalDirection::Forward));
    if (m_canReverse) {
        m_directionCombo->addItem(m_chinese ? QStringLiteral("反向（点序反转）")
                                            : QStringLiteral("Reverse (reverse point order)"),
                                  int(ContourTraversalDirection::Reverse));
    }
    const int directionIndex = m_directionCombo->findData(oldDirection);
    if (directionIndex >= 0) {
        m_directionCombo->setCurrentIndex(directionIndex);
    }

    const int oldCompensation = m_compensationCombo->currentData().toInt();
    m_compensationCombo->clear();
    m_compensationCombo->addItem(m_chinese ? QStringLiteral("G41 左刀补")
                                           : QStringLiteral("G41 left compensation"),
                                 int(ContourCompensationChoice::LeftG41));
    m_compensationCombo->addItem(m_chinese ? QStringLiteral("G42 右刀补")
                                           : QStringLiteral("G42 right compensation"),
                                 int(ContourCompensationChoice::RightG42));
    m_compensationCombo->addItem(m_chinese ? QStringLiteral("G40 CAM 侧几何偏置")
                                           : QStringLiteral("G40 CAM-side geometric offset"),
                                 int(ContourCompensationChoice::CamOffsetG40));
    const int compensationIndex = m_compensationCombo->findData(oldCompensation);
    if (compensationIndex >= 0) {
        m_compensationCombo->setCurrentIndex(compensationIndex);
    }
    updateSummary();
}

void ContourMachiningChoiceDialog::updateStartOptions()
{
    const QSignalBlocker blocker(m_startCombo);
    const int oldStart = m_startCombo->currentData().toInt();
    m_startCombo->clear();

    if (m_featurePoints.size() < 2) {
        m_startCombo->addItem(
            m_chinese ? QStringLiteral("策略生成的默认起点")
                      : QStringLiteral("Strategy-generated default start"), 0);
        m_startCombo->setEnabled(false);
        return;
    }

    m_startCombo->setEnabled(true);
    if (m_closed) {
        for (int i = 0; i < m_featurePoints.size(); ++i) {
            const QVector3D &point = m_featurePoints.at(i);
            m_startCombo->addItem(
                m_chinese
                    ? QStringLiteral("点 %1  (X%2, Y%3)")
                          .arg(i + 1).arg(point.x(), 0, 'f', 2).arg(point.y(), 0, 'f', 2)
                    : QStringLiteral("Point %1  (X%2, Y%3)")
                          .arg(i + 1).arg(point.x(), 0, 'f', 2).arg(point.y(), 0, 'f', 2),
                i);
        }
        const int index = m_startCombo->findData(oldStart);
        m_startCombo->setCurrentIndex(index >= 0 ? index : 0);
        return;
    }

    m_startCombo->addItem(
        m_chinese ? QStringLiteral("首端点 → 末端点")
                  : QStringLiteral("First endpoint → last endpoint"), 0);
    m_startCombo->addItem(
        m_chinese ? QStringLiteral("末端点 → 首端点")
                  : QStringLiteral("Last endpoint → first endpoint"),
        m_featurePoints.size() - 1);
    const bool reverse = m_directionCombo->currentData().toInt()
        == int(ContourTraversalDirection::Reverse);
    m_startCombo->setCurrentIndex(reverse ? 1 : 0);
}

void ContourMachiningChoiceDialog::updateSummary()
{
    const ContourMachiningChoice current = choice();
    QString side;
    if (current.compensation == ContourCompensationChoice::LeftG41) {
        side = m_chinese ? QStringLiteral("刀具位于编程路径左侧，由机床执行 G41。")
                         : QStringLiteral("The cutter is left of the programmed path; the control applies G41.");
    } else if (current.compensation == ContourCompensationChoice::RightG42) {
        side = m_chinese ? QStringLiteral("刀具位于编程路径右侧，由机床执行 G42。")
                         : QStringLiteral("The cutter is right of the programmed path; the control applies G42.");
    } else {
        side = m_chinese ? QStringLiteral("输出 G40；刀具中心路径由 CAM 按当前点序的左侧计算。")
                         : QStringLiteral("G40 is output; CAM calculates the cutter-center path on the left of the current point order.");
    }

    if (!m_canReverse) {
        side += m_chinese
            ? QStringLiteral(" 当前特征没有实际点列，只允许正向；请在仿真中核对内外侧。")
            : QStringLiteral(" This feature has no explicit point list, so only forward traversal is available; verify inside/outside in simulation.");
    } else if (m_closed) {
        side += m_chinese
            ? QStringLiteral(" 封闭轮廓的实际内/外侧取决于点序方向，生成后必须仿真确认。")
            : QStringLiteral(" For a closed contour, physical inside/outside depends on point order and must be verified in simulation.");
    }
    m_summaryLabel->setText(side);
    if (m_choiceChangedCallback) {
        m_choiceChangedCallback(current);
    }
}
