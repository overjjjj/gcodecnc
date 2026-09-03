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
    , m_directionCombo(new QComboBox(this))
    , m_startCombo(new QComboBox(this))
    , m_compensationCombo(new QComboBox(this))
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

ContourMachiningChoice ContourMachiningChoiceDialog::choice() const
{
    ContourMachiningChoice result;
    result.direction = ContourTraversalDirection(m_directionCombo->currentData().toInt());
    result.compensation = ContourCompensationChoice(m_compensationCombo->currentData().toInt());
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
