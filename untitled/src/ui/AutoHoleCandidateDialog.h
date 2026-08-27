#pragma once

#include "../services/AutoHolePlanningService.h"

#include <QDialog>

class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QDoubleSpinBox;

class AutoHoleCandidateDialog : public QDialog
{
public:
    explicit AutoHoleCandidateDialog(QWidget *parent = nullptr);

    void setChineseUi(bool chinese);
    void setCandidates(const QList<AutoHoleCandidate> &candidates);
    int candidateCount() const;
    bool selectCandidate(int row);
    bool selectPlan(const QString &planId);
    bool canConfirm() const;
    bool confirm();
    AutoHoleCandidate selectedCandidate() const;
    const QList<AutoHoleCandidate> &candidates() const { return candidates_; }
    QString rejectionText(int row) const;
    double allowance() const;

private:
    void refreshTable();
    void refreshDetails();
    void retranslateUi();

    QList<AutoHoleCandidate> candidates_;
    bool chinese_ = true;
    QTableWidget *table_ = nullptr;
    QLabel *hintLabel_ = nullptr;
    QLabel *evidenceLabel_ = nullptr;
    QComboBox *planCombo_ = nullptr;
    QDoubleSpinBox *allowanceSpin_ = nullptr;
    QCheckBox *cancelCountersinkCheck_ = nullptr;
    QPushButton *confirmButton_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
};
