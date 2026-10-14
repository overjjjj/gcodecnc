#include "ContourMachiningChoiceDialog.h"
#include "../../../strategies/mill/ContourSideGeometry.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

ContourMachiningChoiceDialog::ContourMachiningChoiceDialog(QWidget *parent)
    : QDialog(parent)
    , m_introLabel(new QLabel(this))
    , m_stateValue(new QLabel(this))
    // 函数说明：m_directionLabel负责当前模块的核心处理。
    , m_directionLabel(new QLabel(this))
    // 函数说明：m_startLabel负责当前模块的核心处理。
    , m_startLabel(new QLabel(this))
    // 函数说明：m_compensationLabel负责当前模块的核心处理。
    , m_compensationLabel(new QLabel(this))
    // 函数说明：m_directionCombo负责当前模块的核心处理。
    , m_directionCombo(new QComboBox(this))
    // 函数说明：m_startCombo负责当前模块的核心处理。
    , m_startCombo(new QComboBox(this))
    // 函数说明：m_compensationCombo负责当前模块的核心处理。
    , m_compensationCombo(new QComboBox(this))
    // 函数说明：m_summaryLabel负责当前模块的核心处理。
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

    m_summaryLabel->setObjectName(QStringLiteral("contourChoiceSummary"));
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setProperty("notice", true);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_confirm = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &ContourMachiningChoiceDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() {
                updateStartOptions();
                updateSummary();
            });
    // 函数说明：connect负责当前模块的核心处理。
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
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_compensationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { updateSummary(); });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);
    layout->addWidget(m_introLabel);
    layout->addLayout(form);
    layout->addWidget(m_summaryLabel);
    layout->addWidget(buttons);
    updateText();
}

// 函数说明：setChineseUi负责当前模块的核心处理。
void ContourMachiningChoiceDialog::setChineseUi(bool chinese)
{
    m_chinese = chinese;
    updateText();
}

// 函数说明：setContext负责当前模块的核心处理。
void ContourMachiningChoiceDialog::setContext(const QString &strategyId,
                                               const ContourFeature &feature,
                                               const StrategyParams &params,
                                               ChainMachiningSide side)
{
    m_restrictStartIndices = false;
    m_machiningSide = side;
    m_feature = feature;
    m_params = params;
    m_closed = strategyId == QStringLiteral("mill_closed_contour");
    m_canReverse = canReverseContourDirection(feature);
    m_featurePoints = feature.points;
    updateText();
    updateStartOptions();

    const double compensation = params.get(QStringLiteral("compensation"), 1.0);
    const int compensationValue = compensation > 0.5
        ? int(ContourCompensationChoice::LeftG41)
        : (compensation < -0.5 ? int(ContourCompensationChoice::RightG42)
                               : int(ContourCompensationChoice::CamOffsetG40));
    const int index = m_compensationCombo->findData(compensationValue);
    if (index >= 0) {
        m_compensationCombo->setCurrentIndex(index);
    }
    updateSummary();
}

// 函数说明：choice负责当前模块的核心处理。
ContourMachiningChoice ContourMachiningChoiceDialog::choice() const
{
    ContourMachiningChoice result;
    result.direction = ContourTraversalDirection(m_directionCombo->currentData().toInt());
    result.compensation = ContourCompensationChoice(m_compensationCombo->currentData().toInt());
    result.closedContour = m_closed;
    result.startPointIndex = m_startCombo->currentData().toInt();
    result.machiningSide = m_machiningSide;
    return result;
}

void ContourMachiningChoiceDialog::accept()
{
    if (m_startCombo->currentIndex() < 0) return;
    ContourFeature feature = m_feature;
    StrategyParams params = m_params;
    applyContourMachiningChoice(feature, params, choice());
    SelectionChain selection; selection.machiningSide = m_machiningSide;
    const QString error = contourSideOperationError(selection,feature,params,
        m_closed ? "mill_closed_contour" : "mill_open_contour");
    if (!error.isEmpty()) { m_summaryLabel->setText(error); return; }
    QDialog::accept();
}

// 函数说明：setChoiceChangedCallback负责当前模块的核心处理。
void ContourMachiningChoiceDialog::setChoiceChangedCallback(
    // 函数说明：void负责当前模块的核心处理。
    std::function<void(const ContourMachiningChoice &)> callback)
{
    m_choiceChangedCallback = std::move(callback);
    updateSummary();
}

// 函数说明：updateText负责当前模块的核心处理。
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
    if (m_machiningSide != ChainMachiningSide::Mixed) {
        m_compensationCombo->addItem(m_chinese ? QStringLiteral("机床刀补（按已选内外侧计算）")
            : QStringLiteral("Control compensation (selected inside/outside)"), int(ContourCompensationChoice::LeftG41));
    } else {
    m_compensationCombo->addItem(m_chinese ? QStringLiteral("G41 左刀补")
                                           : QStringLiteral("G41 left compensation"),
                                 int(ContourCompensationChoice::LeftG41));
    m_compensationCombo->addItem(m_chinese ? QStringLiteral("G42 右刀补")
                                           : QStringLiteral("G42 right compensation"),
                                 int(ContourCompensationChoice::RightG42));
    }
    m_compensationCombo->addItem(m_chinese ? QStringLiteral("G40 CAM 侧几何偏置")
                                           : QStringLiteral("G40 CAM-side geometric offset"),
                                 int(ContourCompensationChoice::CamOffsetG40));
    const int compensationIndex = m_compensationCombo->findData(oldCompensation);
    if (compensationIndex >= 0) {
        m_compensationCombo->setCurrentIndex(compensationIndex);
    }
    updateSummary();
}

// 函数说明：updateStartOptions负责当前模块的核心处理。
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
            if (m_restrictStartIndices && !m_allowedStartIndices.contains(i)) continue;
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

// 函数说明：updateSummary负责当前模块的核心处理。
void ContourMachiningChoiceDialog::updateSummary()
{
    const ContourMachiningChoice current = choice();
    ContourFeature feature = m_feature;
    StrategyParams resolved = m_params;
    applyContourMachiningChoice(feature, resolved, current);
    SelectionChain selection; selection.machiningSide = m_machiningSide;
    const QString error = contourSideOperationError(selection,feature,resolved,
        m_closed ? "mill_closed_contour" : "mill_open_contour");
    if (m_confirm) m_confirm->setEnabled(error.isEmpty() && m_startCombo->currentIndex() >= 0);
    if (!error.isEmpty()) { m_summaryLabel->setText(error); return; }
    QString side;
    if (resolved.get("compensation") > 0) {
        side = m_chinese ? QStringLiteral("刀具位于编程路径左侧，由机床执行 G41。")
                         : QStringLiteral("The cutter is left of the programmed path; the control applies G41.");
    } else if (resolved.get("compensation") < 0) {
        side = m_chinese ? QStringLiteral("刀具位于编程路径右侧，由机床执行 G42。")
                         : QStringLiteral("The cutter is right of the programmed path; the control applies G42.");
    } else {
        side = m_chinese ? QStringLiteral("输出 G40；CAM 按当前点序%1侧计算实际中心路径，生成时检查余量与完整进退刀。")
            .arg(resolved.get("camOffsetSide",1)>0 ? QStringLiteral("左") : QStringLiteral("右"))
            : QStringLiteral("G40: CAM calculates the %1 cutter-center offset and validates stock allowance and entry/exit.")
            .arg(resolved.get("camOffsetSide",1)>0 ? QStringLiteral("left") : QStringLiteral("right"));
    }

    if (resolved.get("compensation") != 0)
        side += m_chinese ? QStringLiteral(" 实际刀具半径由机床D值决定；预览为名义路径，不代表补偿后的中心轨迹。")
            : QStringLiteral(" The control D value determines the effective radius; this nominal preview is not the compensated center path.");

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

void ContourMachiningChoiceDialog::setAllowedStartIndices(const QVector<int> &indices)
{
    m_allowedStartIndices = indices;
    m_restrictStartIndices = true;
    updateStartOptions();
    updateSummary();
}
