#include "AutoHoleCandidateDialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString StateText(AutoCandidateState state, bool chinese)
{
    switch (state) {
    case AutoCandidateState::Draft:
        return chinese ? QStringLiteral("候选草稿") : QStringLiteral("Draft");
    case AutoCandidateState::Conflict:
        return chinese ? QStringLiteral("冲突") : QStringLiteral("Conflict");
    case AutoCandidateState::Stale:
        return chinese ? QStringLiteral("已过期") : QStringLiteral("Stale");
    case AutoCandidateState::Rejected:
        return chinese ? QStringLiteral("已拒绝") : QStringLiteral("Rejected");
    }
    return QString();
}

} // namespace

AutoHoleCandidateDialog::AutoHoleCandidateDialog(QWidget *parent)
    : QDialog(parent)
    , table_(new QTableWidget(this))
    , hintLabel_(new QLabel(this))
    , evidenceLabel_(new QLabel(this))
    , planCombo_(new QComboBox(this))
    , allowanceSpin_(new QDoubleSpinBox(this))
    , cancelCountersinkCheck_(new QCheckBox(this))
    , confirmButton_(new QPushButton(this))
    , cancelButton_(new QPushButton(this))
{
    setObjectName(QStringLiteral("autoHoleCandidateDialog"));
    setModal(true);
    resize(680, 430);
    setStyleSheet(QStringLiteral(
        "#autoHoleCandidateDialog { background: #ffffff; }"
        "#autoHoleCandidateDialog QTableWidget { border: 1px solid #dbe3ef; gridline-color: #e8edf5; }"
        "#autoHoleCandidateDialog QComboBox { min-height: 28px; border: 1px solid #c6d0df; border-radius: 5px; padding: 2px 8px; }"
        "#autoHoleCandidateDialog QPushButton { min-height: 28px; border: 1px solid #c6d0df; border-radius: 5px; padding: 3px 10px; background: #ffffff; color: #27364d; }"
        "#autoHoleCandidateDialog QPushButton:hover { background: #f3f6fb; border-color: #9fb0c8; }"
        "#autoHoleCandidateDialog QPushButton:disabled { background: #f3f5f8; color: #8995a7; border-color: #dce2eb; }"
        "#autoHoleCandidateDialog QPushButton#confirmButton { background: #2f6fec; color: #ffffff; border-color: #2f6fec; }"
        "#autoHoleCandidateDialog QPushButton#confirmButton:hover { background: #245fd0; }"
        "#autoHoleCandidateDialog QPushButton#confirmButton:disabled { background: #aabce1; border-color: #aabce1; }"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);
    hintLabel_->setWordWrap(true);
    layout->addWidget(hintLabel_);

    table_->setColumnCount(5);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    layout->addWidget(table_, 1);

    evidenceLabel_->setWordWrap(true);
    evidenceLabel_->setMinimumHeight(38);
    layout->addWidget(evidenceLabel_);

    auto *planLayout = new QHBoxLayout;
    auto *planLabel = new QLabel(this);
    planLabel->setObjectName(QStringLiteral("autoHolePlanLabel"));
    planLayout->addWidget(planLabel);
    planLayout->addWidget(planCombo_, 1);
    auto *allowanceLabel = new QLabel(this);
    allowanceLabel->setObjectName(QStringLiteral("autoHoleAllowanceLabel"));
    allowanceSpin_->setRange(0.001, 1000.0);
    allowanceSpin_->setDecimals(3);
    allowanceSpin_->setValue(0.2);
    allowanceSpin_->setSuffix(QStringLiteral(" mm"));
    planLayout->addWidget(allowanceLabel);
    planLayout->addWidget(allowanceSpin_);
    layout->addLayout(planLayout);

    cancelCountersinkCheck_->setObjectName(QStringLiteral("cancelCountersinkCheck"));
    cancelCountersinkCheck_->setVisible(false);
    layout->addWidget(cancelCountersinkCheck_);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch(1);
    confirmButton_->setObjectName(QStringLiteral("confirmButton"));
    buttonLayout->addWidget(confirmButton_);
    buttonLayout->addWidget(cancelButton_);
    layout->addLayout(buttonLayout);

    connect(table_, &QTableWidget::currentCellChanged, this,
            [this](int, int, int, int) { refreshDetails(); });
    connect(planCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                const int row = table_->currentRow();
                if (row >= 0 && row < candidates_.size()) {
                    candidates_[row].selectedPlanId =
                        index > 0 ? planCombo_->currentData().toString() : QString();
                }
                confirmButton_->setEnabled(canConfirm());
            });
    connect(cancelCountersinkCheck_, &QCheckBox::toggled, this,
            [this](bool checked) {
                const int row = table_->currentRow();
                if (row >= 0 && row < candidates_.size()) {
                    candidates_[row].cancelCountersink = checked;
                }
            });
    connect(confirmButton_, &QPushButton::clicked, this,
            [this]() { confirm(); });
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);

    retranslateUi();
}

void AutoHoleCandidateDialog::setChineseUi(bool chinese)
{
    chinese_ = chinese;
    retranslateUi();
}

void AutoHoleCandidateDialog::setCandidates(
    const QList<AutoHoleCandidate> &candidates)
{
    candidates_ = candidates;
    refreshTable();
    if (!candidates_.isEmpty()) {
        selectCandidate(0);
    } else {
        refreshDetails();
    }
}

int AutoHoleCandidateDialog::candidateCount() const
{
    return candidates_.size();
}

bool AutoHoleCandidateDialog::selectCandidate(int row)
{
    if (row < 0 || row >= candidates_.size()) {
        return false;
    }
    table_->setCurrentCell(row, 0);
    refreshDetails();
    return true;
}

bool AutoHoleCandidateDialog::selectPlan(const QString &planId)
{
    const int index = planCombo_->findData(planId);
    if (index <= 0) {
        return false;
    }
    planCombo_->setCurrentIndex(index);
    return true;
}

bool AutoHoleCandidateDialog::canConfirm() const
{
    const int row = table_->currentRow();
    return row >= 0 && row < candidates_.size() &&
           candidates_.at(row).state == AutoCandidateState::Draft &&
           !candidates_.at(row).selectedPlanId.trimmed().isEmpty();
}

bool AutoHoleCandidateDialog::confirm()
{
    if (!canConfirm()) {
        return false;
    }
    accept();
    return true;
}

AutoHoleCandidate AutoHoleCandidateDialog::selectedCandidate() const
{
    const int row = table_->currentRow();
    return row >= 0 && row < candidates_.size()
        ? candidates_.at(row) : AutoHoleCandidate();
}

QString AutoHoleCandidateDialog::rejectionText(int row) const
{
    return row >= 0 && row < candidates_.size()
        ? candidates_.at(row).reasons.join(QStringLiteral("; ")) : QString();
}

double AutoHoleCandidateDialog::allowance() const
{
    return allowanceSpin_->value();
}

void AutoHoleCandidateDialog::refreshTable()
{
    table_->setRowCount(candidates_.size());
    for (int row = 0; row < candidates_.size(); ++row) {
        const AutoHoleCandidate &candidate = candidates_.at(row);
        table_->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        table_->setItem(row, 1, new QTableWidgetItem(
            QString::number(candidate.holeFeature.radius * 2.0, 'f', 3)));
        table_->setItem(row, 2, new QTableWidgetItem(
            QString::number(candidate.holeFeature.depth, 'f', 3)));
        table_->setItem(row, 3, new QTableWidgetItem(
            StateText(candidate.state, chinese_)));
        table_->setItem(row, 4, new QTableWidgetItem(
            candidate.reasons.join(QStringLiteral("; "))));
    }
}

void AutoHoleCandidateDialog::refreshDetails()
{
    const QSignalBlocker blocker(planCombo_);
    const QSignalBlocker countersinkBlocker(cancelCountersinkCheck_);
    planCombo_->clear();
    planCombo_->addItem(chinese_ ? QStringLiteral("请选择加工方案")
                                 : QStringLiteral("Select a machining plan"));
    const int row = table_->currentRow();
    if (row < 0 || row >= candidates_.size()) {
        evidenceLabel_->setText(
            chinese_ ? QStringLiteral("未选择候选。")
                     : QStringLiteral("No candidate selected."));
        planCombo_->setEnabled(false);
        cancelCountersinkCheck_->setVisible(false);
        confirmButton_->setEnabled(false);
        return;
    }
    const AutoHoleCandidate &candidate = candidates_.at(row);
    evidenceLabel_->setText(candidate.reasons.isEmpty()
        ? (chinese_ ? QStringLiteral("几何关联有效；确认前仍需选择加工方案。")
                    : QStringLiteral("Geometry link is current; select a plan before confirmation."))
        : candidate.reasons.join(QStringLiteral("; ")));
    for (const AutoHolePlan &plan : candidate.plans) {
        planCombo_->addItem(QStringLiteral("%1 · T%2 · %3")
                                .arg(plan.id)
                                .arg(plan.toolId)
                                .arg(plan.strategyId),
                            plan.id);
    }
    const int selectedIndex = planCombo_->findData(candidate.selectedPlanId);
    planCombo_->setCurrentIndex(selectedIndex > 0 ? selectedIndex : 0);
    planCombo_->setEnabled(candidate.state == AutoCandidateState::Draft &&
                           !candidate.plans.isEmpty());
    const bool supportsCountersink = std::any_of(
        candidate.compoundHole.layers.cbegin(), candidate.compoundHole.layers.cend(),
        [](const HoleLayer &layer) {
            return layer.kind == HoleLayerKind::ConicalCountersink;
        });
    cancelCountersinkCheck_->setVisible(supportsCountersink);
    cancelCountersinkCheck_->setEnabled(candidate.state == AutoCandidateState::Draft);
    cancelCountersinkCheck_->setChecked(candidate.cancelCountersink);
    confirmButton_->setEnabled(canConfirm());
}

void AutoHoleCandidateDialog::retranslateUi()
{
    setWindowTitle(chinese_ ? QStringLiteral("自动孔候选审查")
                            : QStringLiteral("Automatic Hole Candidate Review"));
    hintLabel_->setText(chinese_
        ? QStringLiteral("识别结果仅为候选草稿。只有选择加工方案并确认后，才会创建正式工序。")
        : QStringLiteral("Recognition creates drafts only. A formal operation is created only after plan selection and confirmation."));
    confirmButton_->setText(chinese_ ? QStringLiteral("确认创建工序")
                                     : QStringLiteral("Confirm operation"));
    cancelButton_->setText(chinese_ ? QStringLiteral("取消")
                                    : QStringLiteral("Cancel"));
    if (QLabel *planLabel = findChild<QLabel*>(QStringLiteral("autoHolePlanLabel"))) {
        planLabel->setText(chinese_ ? QStringLiteral("加工方案")
                                    : QStringLiteral("Machining plan"));
    }
    if (QLabel *allowanceLabel = findChild<QLabel*>(
            QStringLiteral("autoHoleAllowanceLabel"))) {
        allowanceLabel->setText(QStringLiteral("AT"));
    }
    cancelCountersinkCheck_->setText(chinese_
        ? QStringLiteral("取消沉头加工，仅处理内孔")
        : QStringLiteral("Skip countersink machining; process the inner hole only"));
    table_->setHorizontalHeaderLabels({
        QStringLiteral("#"),
        chinese_ ? QStringLiteral("孔径") : QStringLiteral("Diameter"),
        chinese_ ? QStringLiteral("孔深") : QStringLiteral("Depth"),
        chinese_ ? QStringLiteral("状态") : QStringLiteral("State"),
        chinese_ ? QStringLiteral("证据 / 拒绝原因") : QStringLiteral("Evidence / rejection")
    });
    refreshTable();
    refreshDetails();
}
